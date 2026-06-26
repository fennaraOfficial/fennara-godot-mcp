use reqwest::Client;
use serde_json::Value;
use std::time::Duration;

use super::error::LlmError;
use super::request::LlmRequest;
use super::types::{
    AdapterKind, Auth, Capabilities, Limits, ModelDefinition, ModelId, ProviderDefinition,
    ProviderId, RequestDefaults,
};
use crate::runtime_daemon::chat::settings::DEFAULT_OLLAMA_BASE_URL;

pub(crate) const PROVIDER_ID: &str = ProviderId::OLLAMA;
const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
const REQUEST_TIMEOUT: Duration = Duration::from_secs(120);

pub(crate) fn provider_definition(base_url: &str) -> ProviderDefinition {
    let mut request = RequestDefaults::default();
    request.generation.temperature = Some(0.7);

    ProviderDefinition {
        id: ProviderId::unchecked(PROVIDER_ID),
        name: "Ollama (local)".to_string(),
        adapter: AdapterKind::OpenAiCompatibleChat,
        base_url: Some(v1_base_url(base_url)),
        auth: Auth::None,
        request,
        disabled: false,
    }
}

pub(crate) fn model_definition(model_id: &str, display_name: Option<String>) -> ModelDefinition {
    ModelDefinition {
        id: ModelId::new(model_id).expect("Ollama model id is valid"),
        provider: ProviderId::unchecked(PROVIDER_ID),
        display_name: display_name.unwrap_or_else(|| fallback_display_name(model_id)),
        adapter_model_id: model_id.to_string(),
        capabilities: Capabilities::text_tools(),
        limits: Limits::default(),
        request: RequestDefaults::default(),
        enabled: true,
    }
}

pub(crate) fn model_id(model: &str) -> Option<&str> {
    model
        .trim()
        .strip_prefix("ollama/")
        .filter(|id| !id.trim().is_empty())
}

pub(crate) fn v1_base_url(base_url: &str) -> String {
    let trimmed = base_url.trim().trim_end_matches('/');
    if trimmed.ends_with("/v1") {
        trimmed.to_string()
    } else if trimmed.is_empty() {
        format!("{DEFAULT_OLLAMA_BASE_URL}/v1")
    } else {
        format!("{trimmed}/v1")
    }
}

pub(crate) fn api_base_url(base_url: &str) -> String {
    let trimmed = base_url.trim().trim_end_matches('/');
    let clean = trimmed.strip_suffix("/v1").unwrap_or(trimmed);
    if clean.is_empty() {
        DEFAULT_OLLAMA_BASE_URL.to_string()
    } else {
        clean.to_string()
    }
}

pub(crate) async fn validate_request(request: &LlmRequest) -> Result<(), LlmError> {
    if messages_include_images(&request.messages) {
        return Err(LlmError::ProviderApi {
            provider: PROVIDER_ID.to_string(),
            status: None,
            message: "Ollama chat in Fennara does not support image input yet.".to_string(),
            retryable: false,
        });
    }

    let client = Client::builder()
        .connect_timeout(CONNECT_TIMEOUT)
        .timeout(REQUEST_TIMEOUT)
        .build()
        .map_err(|error| LlmError::ProviderInit {
            provider: PROVIDER_ID.to_string(),
            message: format!("Failed to create Ollama HTTP client: {error}"),
        })?;

    let base_url = request
        .model
        .provider
        .base_url
        .as_deref()
        .unwrap_or(DEFAULT_OLLAMA_BASE_URL);
    validate_model_available(&client, base_url, &request.model.model.adapter_model_id).await
}

async fn validate_model_available(
    client: &Client,
    base_url: &str,
    model: &str,
) -> Result<(), LlmError> {
    let response = client
        .get(format!("{}/api/tags", api_base_url(base_url)))
        .send()
        .await
        .map_err(|error| LlmError::from_reqwest(PROVIDER_ID, "Failed to reach Ollama", error))?;
    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(LlmError::from_http_response(PROVIDER_ID, status, &text));
    }
    let body = response
        .json::<Value>()
        .await
        .map_err(|error| LlmError::InvalidProviderOutput {
            provider: PROVIDER_ID.to_string(),
            message: format!("Ollama models response was invalid: {error}"),
            raw: None,
        })?;
    let models = body
        .get("models")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default();
    if models.is_empty() {
        return Err(LlmError::ProviderApi {
            provider: PROVIDER_ID.to_string(),
            status: None,
            message: "Ollama is running but no models are installed. Pull a model first."
                .to_string(),
            retryable: false,
        });
    }
    if models.iter().any(|entry| {
        entry
            .get("name")
            .or_else(|| entry.get("model"))
            .and_then(Value::as_str)
            .is_some_and(|id| model_name_matches(id, model))
    }) {
        return Ok(());
    }
    Err(LlmError::ModelNotFound {
        provider: PROVIDER_ID.to_string(),
        model: model.to_string(),
    })
}

fn model_name_matches(candidate: &str, requested: &str) -> bool {
    candidate == requested
        || candidate.strip_suffix(":latest") == Some(requested)
        || requested.strip_suffix(":latest") == Some(candidate)
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
    id.split('/').next_back().unwrap_or(id).replace('-', " ")
}
