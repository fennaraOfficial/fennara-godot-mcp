use futures_util::StreamExt;
use reqwest::Client;
use reqwest::header::{AUTHORIZATION, CONTENT_TYPE, HeaderMap, HeaderName, HeaderValue};
use serde_json::{Map, Value, json};
use std::collections::HashMap;
use std::time::Duration;

use super::super::error::LlmError;
use super::super::request::LlmRequest;
use super::super::sse::{data_lines, parse_sse_payloads};
use super::super::stream::{FinishReason, StreamEvent, Usage};
use super::super::types::{AdapterKind, Auth, ChatCompletion};

const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
const REQUEST_TIMEOUT: Duration = Duration::from_secs(120);

pub(crate) async fn stream_chat<F, Fut>(
    request: &LlmRequest,
    mut on_event: F,
) -> Result<ChatCompletion, LlmError>
where
    F: FnMut(StreamEvent) -> Fut + Send,
    Fut: std::future::Future<Output = Result<bool, LlmError>> + Send,
{
    if request.model.provider.adapter != AdapterKind::OpenAiCompatibleChat {
        return Err(LlmError::ProviderInit {
            provider: request.model.provider.id.to_string(),
            message: format!(
                "{} does not use the OpenAI-compatible chat adapter.",
                request.model.provider.name
            ),
        });
    }

    let provider_id = request.model.provider.id.to_string();
    let base_url = request
        .model
        .provider
        .base_url
        .as_deref()
        .ok_or_else(|| LlmError::Config {
            message: format!("{} is missing a base URL.", request.model.provider.name),
        })?
        .trim_end_matches('/')
        .to_string();
    let client = Client::builder()
        .connect_timeout(CONNECT_TIMEOUT)
        .timeout(REQUEST_TIMEOUT)
        .build()
        .map_err(|error| LlmError::ProviderInit {
            provider: provider_id.clone(),
            message: format!("Failed to create HTTP client: {error}"),
        })?;

    let response = client
        .post(format!("{base_url}/chat/completions"))
        .headers(headers(request)?)
        .json(&body(request))
        .send()
        .await
        .map_err(|error| {
            LlmError::from_reqwest(
                &provider_id,
                &format!("Failed to connect to {}", request.model.provider.name),
                error,
            )
        })?;

    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(LlmError::from_http_response(&provider_id, status, &text));
    }

    let mut stream = response.bytes_stream();
    let mut buffer = String::new();
    let mut text_parts: Vec<String> = Vec::new();
    let mut reasoning_details: Vec<Value> = Vec::new();
    let mut reasoning_text_parts: Vec<String> = Vec::new();
    let mut tool_entries: HashMap<usize, Value> = HashMap::new();
    let mut final_usage: Option<Usage> = None;
    let mut finish_reason = FinishReason::Stop;

    if !on_event(StreamEvent::StepStart { index: 0 }).await? {
        return Ok(ChatCompletion {
            content: String::new(),
            tool_calls: Vec::new(),
        });
    }

    while let Some(next) = stream.next().await {
        let chunk = next.map_err(|error| {
            LlmError::from_reqwest(
                &provider_id,
                &format!("{} stream failed", request.model.provider.name),
                error,
            )
        })?;
        buffer.push_str(&String::from_utf8_lossy(&chunk));
        let parsed = parse_sse_payloads(&buffer);
        buffer = parsed.rest;

        for event in parsed.events {
            for data in data_lines(&event) {
                if data.is_empty() || data == "[DONE]" {
                    continue;
                }
                let chunk: Value = serde_json::from_str(&data).map_err(|error| {
                    LlmError::InvalidProviderOutput {
                        provider: provider_id.clone(),
                        message: format!(
                            "{} sent invalid stream JSON: {error}",
                            request.model.provider.name
                        ),
                        raw: Some(data.clone()),
                    }
                })?;
                if let Some(error) = chunk.get("error") {
                    let normalized = LlmError::from_stream_error(&provider_id, error);
                    let _ = on_event(StreamEvent::ProviderError(normalized.clone())).await?;
                    return Err(normalized);
                }
                if let Some(usage) = chunk.get("usage").filter(|usage| usage.is_object()) {
                    let usage = Usage::from_provider_value(usage);
                    final_usage = Some(usage.clone());
                    if !on_event(StreamEvent::Usage(usage)).await? {
                        return Ok(partial_completion(text_parts, tool_entries));
                    }
                }

                let Some(choice) = chunk
                    .get("choices")
                    .and_then(Value::as_array)
                    .and_then(|choices| choices.first())
                else {
                    continue;
                };
                if let Some(reason) = choice.get("finish_reason").and_then(Value::as_str) {
                    finish_reason = FinishReason::from_provider(Some(reason));
                }

                let delta = choice.get("delta");
                if let Some(delta) = delta {
                    if let Some(raw_reasoning) = delta.get("reasoning").and_then(Value::as_str) {
                        if !raw_reasoning.is_empty() {
                            reasoning_text_parts.push(raw_reasoning.to_string());
                            if !on_event(StreamEvent::ReasoningDelta {
                                id: "reasoning".to_string(),
                                text: raw_reasoning.to_string(),
                            })
                            .await?
                            {
                                return Ok(partial_completion(text_parts, tool_entries));
                            }
                        }
                    }
                    if let Some(raw_details) = delta.get("reasoning_details") {
                        if let Some(items) = raw_details.as_array() {
                            reasoning_details.extend(items.iter().cloned());
                        } else if raw_details.is_object() {
                            reasoning_details.push(raw_details.clone());
                        }
                    }
                    if !reasoning_details.is_empty() {
                        let reasoning = readable_reasoning_text(
                            &reasoning_details,
                            &reasoning_text_parts.join(""),
                        );
                        if !reasoning.is_empty()
                            && !on_event(StreamEvent::ReasoningDelta {
                                id: "reasoning".to_string(),
                                text: reasoning,
                            })
                            .await?
                        {
                            return Ok(partial_completion(text_parts, tool_entries));
                        }
                    }
                }

                if let Some(content) = delta
                    .and_then(|delta| delta.get("content"))
                    .and_then(Value::as_str)
                {
                    if !content.is_empty() {
                        text_parts.push(content.to_string());
                        if !on_event(StreamEvent::TextDelta {
                            id: "assistant".to_string(),
                            text: content.to_string(),
                        })
                        .await?
                        {
                            return Ok(partial_completion(text_parts, tool_entries));
                        }
                    }
                }

                if let Some(raw_tools) = delta
                    .and_then(|delta| delta.get("tool_calls"))
                    .and_then(Value::as_array)
                {
                    for raw_tool in raw_tools {
                        let index = raw_tool
                            .get("index")
                            .and_then(Value::as_u64)
                            .map(|value| value as usize)
                            .unwrap_or(tool_entries.len());
                        let entry = tool_entries.entry(index).or_insert_with(|| {
                            json!({
                                "id": format!("tool_call_{index}"),
                                "type": "function",
                                "function": { "name": "", "arguments": "" }
                            })
                        });
                        merge_tool_delta(entry, raw_tool);
                        let (id, name, arguments) = tool_parts(entry);
                        if !on_event(StreamEvent::ToolCallDelta {
                            id,
                            name,
                            arguments,
                        })
                        .await?
                        {
                            return Ok(partial_completion(text_parts, tool_entries));
                        }
                    }
                }
            }
        }
    }

    let tool_calls = sorted_tool_calls(tool_entries);
    for call in &tool_calls {
        let (id, name, arguments) = tool_parts(call);
        on_event(StreamEvent::ToolCall {
            id,
            name,
            arguments,
            raw: call.clone(),
        })
        .await?;
    }
    on_event(StreamEvent::Finish {
        reason: finish_reason,
        usage: final_usage,
    })
    .await?;

    Ok(ChatCompletion {
        content: text_parts.join(""),
        tool_calls,
    })
}

fn headers(request: &LlmRequest) -> Result<HeaderMap, LlmError> {
    let provider_id = request.model.provider.id.to_string();
    let mut headers = HeaderMap::new();
    headers.insert(CONTENT_TYPE, HeaderValue::from_static("application/json"));
    if let Some(value) = auth_header_value(&request.model.provider.auth, &provider_id)? {
        headers.insert(AUTHORIZATION, value);
    }
    for (key, value) in &request.model.request.headers {
        let name = HeaderName::from_bytes(key.as_bytes()).map_err(|error| LlmError::Config {
            message: format!("Invalid header name for {provider_id}: {key}: {error}"),
        })?;
        let value = HeaderValue::from_str(value).map_err(|error| LlmError::Config {
            message: format!("Invalid header value for {provider_id}: {key}: {error}"),
        })?;
        headers.insert(name, value);
    }
    Ok(headers)
}

fn auth_header_value(auth: &Auth, provider: &str) -> Result<Option<HeaderValue>, LlmError> {
    let token = match auth {
        Auth::None => return Ok(None),
        Auth::Env { var } => std::env::var(var).ok(),
        Auth::Bearer { secret_name } => {
            return Err(LlmError::Auth {
                provider: provider.to_string(),
                message: format!(
                    "Bearer secret {secret_name} is not wired into chat settings yet."
                ),
            });
        }
        Auth::InlineBearer { value } => Some(value.clone()),
    };
    let Some(token) = token.filter(|value| !value.trim().is_empty()) else {
        return Err(LlmError::Auth {
            provider: provider.to_string(),
            message: if provider == "openrouter" {
                "Add your OpenRouter API key in settings first.".to_string()
            } else {
                format!("Missing bearer token for {provider}.")
            },
        });
    };
    let value = format!("Bearer {}", token.trim());
    HeaderValue::from_str(&value)
        .map(Some)
        .map_err(|_| LlmError::Auth {
            provider: provider.to_string(),
            message: format!("{provider} bearer token contains invalid header characters."),
        })
}

fn body(request: &LlmRequest) -> Value {
    let mut body = request.model.request.body.clone();
    body.insert(
        "model".to_string(),
        Value::String(request.model.model.adapter_model_id.clone()),
    );
    body.insert(
        "messages".to_string(),
        Value::Array(request.messages.clone()),
    );
    body.insert("stream".to_string(), Value::Bool(true));
    body.insert(
        "stream_options".to_string(),
        json!({ "include_usage": true }),
    );
    if !request.tools.is_empty() && request.model.model.capabilities.tools {
        body.insert("tools".to_string(), Value::Array(request.tools.clone()));
    }
    if let Some(temperature) = request.model.request.generation.temperature {
        body.insert("temperature".to_string(), json!(temperature));
    }
    if let Some(max_tokens) = request.model.request.generation.max_output_tokens {
        body.insert("max_tokens".to_string(), json!(max_tokens));
    }
    if request.model.model.capabilities.reasoning {
        if let Some(reasoning_effort) = request
            .model
            .request
            .generation
            .reasoning_effort
            .as_deref()
            .filter(|effort| !effort.trim().is_empty())
        {
            body.insert(
                "reasoning".to_string(),
                json!({ "effort": reasoning_effort }),
            );
        }
    }
    Value::Object(body)
}

fn merge_tool_delta(entry: &mut Value, raw_tool: &Value) {
    if let Some(id) = raw_tool.get("id").and_then(Value::as_str) {
        entry["id"] = json!(id);
    }
    if let Some(tool_type) = raw_tool.get("type").and_then(Value::as_str) {
        entry["type"] = json!(tool_type);
    }
    let raw_function = raw_tool.get("function").unwrap_or(&Value::Null);
    if let Some(name) = raw_function.get("name").and_then(Value::as_str) {
        entry["function"]["name"] = json!(name);
    }
    if let Some(arguments) = raw_function.get("arguments").and_then(Value::as_str) {
        let current = entry["function"]["arguments"].as_str().unwrap_or_default();
        entry["function"]["arguments"] = json!(format!("{current}{arguments}"));
    } else if let Some(arguments) = raw_function.get("arguments") {
        entry["function"]["arguments"] = json!(arguments.to_string());
    }
}

fn tool_parts(value: &Value) -> (String, String, String) {
    let id = value
        .get("id")
        .and_then(Value::as_str)
        .unwrap_or("tool_call")
        .to_string();
    let function = value.get("function").unwrap_or(&Value::Null);
    let name = function
        .get("name")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    let arguments = function
        .get("arguments")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    (id, name, arguments)
}

fn partial_completion(
    text_parts: Vec<String>,
    tool_entries: HashMap<usize, Value>,
) -> ChatCompletion {
    ChatCompletion {
        content: text_parts.join(""),
        tool_calls: sorted_tool_calls(tool_entries),
    }
}

fn sorted_tool_calls(tool_entries: HashMap<usize, Value>) -> Vec<Value> {
    let mut tool_calls = tool_entries.into_iter().collect::<Vec<(usize, Value)>>();
    tool_calls.sort_by_key(|(index, _)| *index);
    tool_calls
        .into_iter()
        .filter_map(|(_, value)| normalize_tool_call(value))
        .collect()
}

fn normalize_tool_call(value: Value) -> Option<Value> {
    let function = value.get("function")?;
    let name = function.get("name").and_then(Value::as_str)?.trim();
    if name.is_empty() {
        return None;
    }
    let arguments = function
        .get("arguments")
        .and_then(Value::as_str)
        .unwrap_or_default();
    if !arguments.trim().is_empty() && serde_json::from_str::<Value>(arguments).is_err() {
        return None;
    }
    Some(value)
}

fn readable_reasoning_text(reasoning_details: &[Value], reasoning_text: &str) -> String {
    let mut parts = Vec::new();
    if !reasoning_text.is_empty() {
        parts.push(reasoning_text.to_string());
    }
    for entry in reasoning_details {
        let Some(row) = entry.as_object() else {
            continue;
        };
        let text = row
            .get("summary")
            .or_else(|| row.get("text"))
            .or_else(|| row.get("content"))
            .and_then(Value::as_str)
            .unwrap_or_default();
        if text.is_empty() || text == "[REDACTED]" {
            continue;
        }
        if !reasoning_text.is_empty()
            && (text.contains(reasoning_text) || reasoning_text.contains(text))
        {
            continue;
        }
        parts.push(text.to_string());
    }
    parts.join("\n")
}

#[allow(dead_code)]
fn _assert_body_is_object(value: &Value) -> Option<&Map<String, Value>> {
    value.as_object()
}
