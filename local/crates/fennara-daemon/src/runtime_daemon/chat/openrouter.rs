use futures_util::StreamExt;
use reqwest::Client;
use serde_json::{Value, json};
use std::collections::HashMap;

use super::models::model_supports_text_chat;
use super::settings::DEFAULT_MODEL;

const OPENROUTER_API_BASE: &str = "https://openrouter.ai/api/v1";
const SYSTEM_PROMPT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../prompts/plugin_chat_system.md"
));

#[derive(Clone, Debug)]
pub(crate) enum StreamItem {
    Text {
        content: String,
        done: bool,
    },
    Reasoning {
        content: String,
        done: bool,
    },
    FunctionCall {
        id: String,
        name: String,
        arguments: String,
        done: bool,
    },
    Usage(Value),
}

#[derive(Clone, Debug)]
pub(crate) struct ChatCompletion {
    pub(crate) content: String,
    pub(crate) tool_calls: Vec<Value>,
}

pub(crate) async fn stream_chat<F, Fut>(
    api_key: &str,
    model: &str,
    reasoning_effort: &str,
    messages: &[Value],
    tools: &[Value],
    mut on_item: F,
) -> Result<ChatCompletion, String>
where
    F: FnMut(StreamItem) -> Fut,
    Fut: std::future::Future<Output = Result<bool, String>>,
{
    let client = Client::new();
    validate_text_chat_model(&client, model).await?;

    let response = client
        .post(format!("{OPENROUTER_API_BASE}/chat/completions"))
        .headers(openrouter_headers(api_key)?)
        .json(&json!({
            "model": apply_nitro_variant(model),
            "messages": messages,
            "tools": tools,
            "temperature": 0.7,
            "reasoning": { "effort": reasoning_effort },
            "stream": true,
            "stream_options": { "include_usage": true },
            "provider": { "allow_fallbacks": true, "sort": "throughput" }
        }))
        .send()
        .await
        .map_err(|error| format!("Failed to connect to OpenRouter: {error}"))?;

    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(format!("{status}: {}", openrouter_error_message(&text)));
    }

    let mut stream = response.bytes_stream();
    let mut buffer = String::new();
    let mut text_parts: Vec<String> = Vec::new();
    let mut reasoning_details: Vec<Value> = Vec::new();
    let mut reasoning_text_parts: Vec<String> = Vec::new();
    let mut last_emit_len = 0usize;
    let mut last_reasoning_emit_len = 0usize;
    let mut reasoning_done_sent = false;
    let mut tool_entries: HashMap<usize, Value> = HashMap::new();
    let mut last_tool_emit_len: HashMap<usize, usize> = HashMap::new();

    while let Some(next) = stream.next().await {
        let chunk = next.map_err(|error| format!("OpenRouter stream failed: {error}"))?;
        buffer.push_str(&String::from_utf8_lossy(&chunk));
        let parsed = parse_sse_payloads(&buffer);
        buffer = parsed.rest;

        for event in parsed.events {
            for data in data_lines(&event) {
                if data.is_empty() || data == "[DONE]" {
                    continue;
                }
                let chunk: Value = serde_json::from_str(&data)
                    .map_err(|error| format!("OpenRouter sent invalid stream JSON: {error}"))?;
                if let Some(error) = chunk.get("error") {
                    return Err(error
                        .get("message")
                        .and_then(Value::as_str)
                        .unwrap_or("OpenRouter stream failed.")
                        .to_string());
                }
                if let Some(usage) = chunk.get("usage").filter(|usage| usage.is_object()) {
                    if !on_item(StreamItem::Usage(usage.clone())).await? {
                        return Ok(ChatCompletion {
                            content: text_parts.join(""),
                            tool_calls: Vec::new(),
                        });
                    }
                }

                let delta = chunk
                    .get("choices")
                    .and_then(Value::as_array)
                    .and_then(|choices| choices.first())
                    .and_then(|choice| choice.get("delta"));

                if let Some(delta) = delta {
                    if let Some(raw_reasoning) = delta.get("reasoning").and_then(Value::as_str) {
                        if !raw_reasoning.is_empty() {
                            reasoning_text_parts.push(raw_reasoning.to_string());
                        }
                    }
                    if let Some(raw_details) = delta.get("reasoning_details") {
                        if let Some(items) = raw_details.as_array() {
                            reasoning_details.extend(items.iter().cloned());
                        } else if raw_details.is_object() {
                            reasoning_details.push(raw_details.clone());
                        }
                    }
                    if delta.get("reasoning").is_some() || delta.get("reasoning_details").is_some()
                    {
                        let reasoning = readable_reasoning_text(
                            &reasoning_details,
                            &reasoning_text_parts.join(""),
                        );
                        if !reasoning.is_empty()
                            && reasoning.len().saturating_sub(last_reasoning_emit_len) >= 48
                        {
                            last_reasoning_emit_len = reasoning.len();
                            if !on_item(StreamItem::Reasoning {
                                content: reasoning,
                                done: false,
                            })
                            .await?
                            {
                                return Ok(ChatCompletion {
                                    content: text_parts.join(""),
                                    tool_calls: Vec::new(),
                                });
                            }
                        }
                    }
                }

                let content = delta
                    .and_then(|delta| delta.get("content"))
                    .and_then(Value::as_str);
                if let Some(content) = content {
                    if content.is_empty() {
                        continue;
                    }
                    if !reasoning_done_sent {
                        let final_reasoning = readable_reasoning_text(
                            &reasoning_details,
                            &reasoning_text_parts.join(""),
                        );
                        if !final_reasoning.is_empty() {
                            reasoning_done_sent = true;
                            on_item(StreamItem::Reasoning {
                                content: final_reasoning,
                                done: true,
                            })
                            .await?;
                        }
                    }
                    text_parts.push(content.to_string());
                    let full_text = text_parts.join("");
                    if full_text.len().saturating_sub(last_emit_len) >= 24 {
                        last_emit_len = full_text.len();
                        if !on_item(StreamItem::Text {
                            content: full_text,
                            done: false,
                        })
                        .await?
                        {
                            return Ok(ChatCompletion {
                                content: text_parts.join(""),
                                tool_calls: Vec::new(),
                            });
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
                        let id = entry
                            .get("id")
                            .and_then(Value::as_str)
                            .unwrap_or("tool_call")
                            .to_string();
                        let function = entry.get("function").unwrap_or(&Value::Null);
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
                        let last_len = last_tool_emit_len.get(&index).copied().unwrap_or(0);
                        if arguments.len().saturating_sub(last_len) >= 24 || !name.is_empty() {
                            last_tool_emit_len.insert(index, arguments.len());
                            if !on_item(StreamItem::FunctionCall {
                                id,
                                name,
                                arguments,
                                done: false,
                            })
                            .await?
                            {
                                return Ok(ChatCompletion {
                                    content: text_parts.join(""),
                                    tool_calls: Vec::new(),
                                });
                            }
                        }
                    }
                }
            }
        }
    }

    let final_text = text_parts.join("");
    let final_reasoning =
        readable_reasoning_text(&reasoning_details, &reasoning_text_parts.join(""));
    if !final_reasoning.is_empty() && !reasoning_done_sent {
        on_item(StreamItem::Reasoning {
            content: final_reasoning,
            done: true,
        })
        .await?;
    }
    on_item(StreamItem::Text {
        content: final_text.clone(),
        done: true,
    })
    .await?;
    let mut tool_calls = tool_entries.into_iter().collect::<Vec<(usize, Value)>>();
    tool_calls.sort_by_key(|(index, _)| *index);
    let tool_calls = tool_calls
        .into_iter()
        .map(|(_, value)| value)
        .collect::<Vec<_>>();
    for call in &tool_calls {
        let id = call
            .get("id")
            .and_then(Value::as_str)
            .unwrap_or("tool_call");
        let function = call.get("function").unwrap_or(&Value::Null);
        on_item(StreamItem::FunctionCall {
            id: id.to_string(),
            name: function
                .get("name")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string(),
            arguments: function
                .get("arguments")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string(),
            done: true,
        })
        .await?;
    }
    Ok(ChatCompletion {
        content: final_text,
        tool_calls,
    })
}

pub(crate) fn build_messages(history: &[Value], user_message: &str) -> Vec<Value> {
    let mut messages = vec![json!({ "role": "system", "content": SYSTEM_PROMPT })];
    messages.extend(history.iter().cloned());
    messages.push(json!({ "role": "user", "content": user_message }));
    messages
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
    }
}

fn openrouter_headers(api_key: &str) -> Result<reqwest::header::HeaderMap, String> {
    let mut headers = reqwest::header::HeaderMap::new();
    headers.insert(
        reqwest::header::AUTHORIZATION,
        format!("Bearer {api_key}")
            .parse()
            .map_err(|_| "OpenRouter API key contains invalid header characters.".to_string())?,
    );
    headers.insert(
        reqwest::header::CONTENT_TYPE,
        "application/json"
            .parse()
            .map_err(|error| format!("invalid content type header: {error}"))?,
    );
    headers.insert(
        "HTTP-Referer",
        "https://github.com/fennaraOfficial/fennara-godot-mcp"
            .parse()
            .map_err(|error| format!("invalid referer header: {error}"))?,
    );
    headers.insert(
        "X-Title",
        "Fennara OSS"
            .parse()
            .map_err(|error| format!("invalid title header: {error}"))?,
    );
    Ok(headers)
}

fn apply_nitro_variant(model: &str) -> String {
    let clean = model.trim();
    if clean.ends_with(":nitro") {
        clean.to_string()
    } else if has_route_variant(clean) {
        clean.to_string()
    } else {
        format!("{clean}:nitro")
    }
}

async fn validate_text_chat_model(client: &Client, model: &str) -> Result<(), String> {
    let clean = model.trim().trim_end_matches(":nitro");
    if clean == DEFAULT_MODEL {
        return Ok(());
    }
    if !clean.contains('/') {
        return Err("Choose an OpenRouter chat model id like provider/model.".to_string());
    }

    let response = client
        .get(format!("{OPENROUTER_API_BASE}/models"))
        .send()
        .await
        .map_err(|error| format!("Failed to fetch OpenRouter model metadata: {error}"))?;
    if !response.status().is_success() {
        return Err(format!(
            "OpenRouter model metadata request failed: {}",
            response.status()
        ));
    }
    let body: Value = response
        .json()
        .await
        .map_err(|error| format!("OpenRouter model metadata was invalid: {error}"))?;
    let Some(model_info) = body
        .get("data")
        .and_then(Value::as_array)
        .and_then(|models| {
            models
                .iter()
                .find(|entry| entry.get("id").and_then(Value::as_str) == Some(clean))
        })
    else {
        return Err(format!("OpenRouter model not found: {clean}"));
    };

    if model_supports_text_chat(model_info) {
        return Ok(());
    }
    Err(format!(
        "{clean} does not advertise text input and text output in OpenRouter metadata."
    ))
}

fn has_route_variant(model: &str) -> bool {
    model
        .rsplit_once('/')
        .and_then(|(_, model_name)| model_name.rsplit_once(':'))
        .is_some()
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

fn openrouter_error_message(text: &str) -> String {
    serde_json::from_str::<Value>(text)
        .ok()
        .and_then(|body| {
            body.get("error")
                .and_then(|error| error.get("message"))
                .or_else(|| body.get("message"))
                .and_then(Value::as_str)
                .map(ToString::to_string)
        })
        .unwrap_or_else(|| {
            if text.trim().is_empty() {
                "OpenRouter request failed.".to_string()
            } else {
                text.to_string()
            }
        })
}

struct SseParts {
    events: Vec<String>,
    rest: String,
}

fn parse_sse_payloads(buffer: &str) -> SseParts {
    let parts: Vec<&str> = buffer.split("\n\n").collect();
    let rest = parts.last().copied().unwrap_or_default().to_string();
    let events = parts
        .iter()
        .take(parts.len().saturating_sub(1))
        .map(|part| part.to_string())
        .collect();
    SseParts { events, rest }
}

fn data_lines(event: &str) -> Vec<String> {
    event
        .lines()
        .map(str::trim)
        .filter_map(|line| line.strip_prefix("data:"))
        .map(str::trim)
        .map(ToString::to_string)
        .collect()
}
