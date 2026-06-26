use reqwest::Client;
use serde_json::{Value, json};

use super::error::LlmError;
use super::request::LlmRequest;
use super::types::{
    AdapterKind, Auth, Capabilities, GenerationDefaults, Limits, ModelDefinition, ModelId,
    ProviderDefinition, ProviderId, RequestDefaults,
};
use crate::runtime_daemon::chat::models::{model_supports_image_chat, model_supports_text_chat};
use crate::runtime_daemon::chat::settings::DEFAULT_MODEL;

pub(crate) const PROVIDER_ID: &str = ProviderId::OPENROUTER;
pub(crate) const API_BASE: &str = "https://openrouter.ai/api/v1";
const MODELS_URL: &str = "https://openrouter.ai/api/v1/models";

pub(crate) fn provider_definition(api_key: Option<&str>) -> ProviderDefinition {
    let mut request = RequestDefaults::default();
    request.headers.insert(
        "HTTP-Referer".to_string(),
        "https://github.com/fennaraOfficial/fennara-godot-ai".to_string(),
    );
    request
        .headers
        .insert("X-Title".to_string(), "Fennara OSS".to_string());
    request.body.insert(
        "provider".to_string(),
        json!({ "allow_fallbacks": true, "sort": "throughput" }),
    );

    ProviderDefinition {
        id: ProviderId::unchecked(PROVIDER_ID),
        name: "OpenRouter".to_string(),
        adapter: AdapterKind::OpenAiCompatibleChat,
        base_url: Some(API_BASE.to_string()),
        auth: api_key
            .filter(|key| !key.trim().is_empty())
            .map(|key| Auth::InlineBearer {
                value: key.trim().to_string(),
            })
            .unwrap_or_else(|| Auth::Env {
                var: "OPENROUTER_API_KEY".to_string(),
            }),
        request,
        disabled: false,
    }
}

pub(crate) fn model_definition(model_id: &str, display_name: Option<String>) -> ModelDefinition {
    let mut request = RequestDefaults::default();
    request.generation = GenerationDefaults {
        temperature: Some(0.7),
        max_output_tokens: None,
        reasoning_effort: None,
    };

    ModelDefinition {
        id: ModelId::new(model_id).expect("built-in OpenRouter model id is valid"),
        provider: ProviderId::unchecked(PROVIDER_ID),
        display_name: display_name.unwrap_or_else(|| fallback_display_name(model_id)),
        adapter_model_id: adapter_model_id(model_id),
        capabilities: Capabilities::text_image_tools_reasoning(),
        limits: if model_id == DEFAULT_MODEL {
            Limits {
                context_tokens: Some(1_048_576),
                input_tokens: None,
                output_tokens: Some(8_192),
            }
        } else {
            Limits::default()
        },
        request,
        enabled: true,
    }
}

pub(crate) fn adapter_model_id(model_id: &str) -> String {
    let clean = model_id.trim();
    if clean.ends_with(":nitro") || has_route_variant(clean) {
        clean.to_string()
    } else {
        format!("{clean}:nitro")
    }
}

pub(crate) async fn validate_request(request: &LlmRequest) -> Result<(), LlmError> {
    let clean = request
        .model
        .model
        .id
        .as_str()
        .trim()
        .trim_end_matches(":nitro");
    if clean == DEFAULT_MODEL {
        return Ok(());
    }
    if request.model.model.limits.context_tokens.is_some()
        && request.model.model.limits.output_tokens.is_some()
    {
        return Ok(());
    }

    let client = Client::new();
    let response = client.get(MODELS_URL).send().await.map_err(|error| {
        LlmError::from_reqwest(
            PROVIDER_ID,
            "Failed to fetch OpenRouter model metadata",
            error,
        )
    })?;
    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(LlmError::from_http_response(PROVIDER_ID, status, &text));
    }
    let body: Value = response
        .json()
        .await
        .map_err(|error| LlmError::InvalidProviderOutput {
            provider: PROVIDER_ID.to_string(),
            message: format!("OpenRouter model metadata was invalid: {error}"),
            raw: None,
        })?;
    let Some(model_info) = body
        .get("data")
        .and_then(Value::as_array)
        .and_then(|models| {
            models
                .iter()
                .find(|entry| entry.get("id").and_then(Value::as_str) == Some(clean))
        })
    else {
        return Err(LlmError::ModelNotFound {
            provider: PROVIDER_ID.to_string(),
            model: clean.to_string(),
        });
    };

    if messages_include_images(&request.messages) && !model_supports_image_chat(model_info) {
        return Err(LlmError::ProviderApi {
            provider: PROVIDER_ID.to_string(),
            status: None,
            message: format!("{clean} does not advertise image input in OpenRouter metadata."),
            retryable: false,
        });
    }
    if model_supports_text_chat(model_info) {
        return Ok(());
    }
    Err(LlmError::ProviderApi {
        provider: PROVIDER_ID.to_string(),
        status: None,
        message: format!(
            "{clean} does not advertise text input and text output in OpenRouter metadata."
        ),
        retryable: false,
    })
}

fn has_route_variant(model: &str) -> bool {
    model
        .rsplit_once('/')
        .and_then(|(_, model_name)| model_name.rsplit_once(':'))
        .is_some()
}

fn messages_include_images(messages: &[Value]) -> bool {
    messages.iter().any(value_includes_image)
}

fn value_includes_image(value: &Value) -> bool {
    match value {
        Value::Object(object) => {
            object
                .get("type")
                .and_then(Value::as_str)
                .is_some_and(|kind| kind == "image_url")
                || object.values().any(value_includes_image)
        }
        Value::Array(items) => items.iter().any(value_includes_image),
        _ => false,
    }
}

fn fallback_display_name(id: &str) -> String {
    id.trim_start_matches('~')
        .split('/')
        .next_back()
        .unwrap_or(id)
        .replace('-', " ")
        .replace("latest", "Latest")
}
