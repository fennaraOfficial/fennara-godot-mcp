use reqwest::Client;
use serde_json::Value;
use std::time::Duration;

use super::error::LlmError;
use super::request::LlmRequest;
use super::types::{
    AdapterKind, Auth, Capabilities, GenerationDefaults, Limits, ModelDefinition, ModelId,
    ProviderDefinition, ProviderId, RequestDefaults,
};

pub(crate) const PROVIDER_ID: &str = ProviderId::LMSTUDIO;
pub(crate) const DEFAULT_BASE_URL: &str = "http://127.0.0.1:1234/v1";
pub(crate) const API_KEY_ENV: &str = "LMSTUDIO_API_KEY";
const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
const REQUEST_TIMEOUT: Duration = Duration::from_secs(120);

pub(crate) fn provider_definition(base_url: &str, api_key: Option<&str>) -> ProviderDefinition {
    let mut request = RequestDefaults::default();
    request.generation = GenerationDefaults {
        temperature: Some(0.7),
        max_output_tokens: None,
        reasoning_effort: None,
    };

    ProviderDefinition {
        id: ProviderId::unchecked(PROVIDER_ID),
        name: "LM Studio".to_string(),
        adapter: AdapterKind::OpenAiCompatibleChat,
        base_url: Some(v1_base_url(base_url)),
        auth: auth(api_key),
        request,
        disabled: false,
    }
}

pub(crate) fn model_definition(model_id: &str, display_name: Option<String>) -> ModelDefinition {
    ModelDefinition {
        id: ModelId::new(model_id).expect("LM Studio model id is valid"),
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
        .strip_prefix("lmstudio/")
        .filter(|id| !id.trim().is_empty())
}

pub(crate) fn v1_base_url(base_url: &str) -> String {
    let trimmed = base_url.trim().trim_end_matches('/');
    if trimmed.is_empty() {
        DEFAULT_BASE_URL.to_string()
    } else if trimmed.ends_with("/v1") {
        trimmed.to_string()
    } else {
        format!("{trimmed}/v1")
    }
}

pub(crate) async fn validate_request(request: &LlmRequest) -> Result<(), LlmError> {
    let client = Client::builder()
        .connect_timeout(CONNECT_TIMEOUT)
        .timeout(REQUEST_TIMEOUT)
        .build()
        .map_err(|error| LlmError::ProviderInit {
            provider: PROVIDER_ID.to_string(),
            message: format!("Failed to create LM Studio HTTP client: {error}"),
        })?;

    let base_url = request
        .model
        .provider
        .base_url
        .as_deref()
        .unwrap_or(DEFAULT_BASE_URL);
    validate_model_available(&client, base_url, &request.model.model.adapter_model_id).await
}

async fn validate_model_available(
    client: &Client,
    base_url: &str,
    model: &str,
) -> Result<(), LlmError> {
    let response = client
        .get(format!("{}/models", v1_base_url(base_url)))
        .send()
        .await
        .map_err(|error| LlmError::from_reqwest(PROVIDER_ID, "Failed to reach LM Studio", error))?;
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
            message: format!("LM Studio models response was invalid: {error}"),
            raw: None,
        })?;
    let models = body
        .get("data")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default();
    if models.is_empty() {
        return Err(LlmError::ProviderApi {
            provider: PROVIDER_ID.to_string(),
            status: None,
            message: "LM Studio is running but no models are loaded.".to_string(),
            retryable: false,
        });
    }
    if models.iter().any(|entry| {
        entry
            .get("id")
            .or_else(|| entry.get("model"))
            .and_then(Value::as_str)
            .is_some_and(|id| id == model)
    }) {
        return Ok(());
    }
    Err(LlmError::ModelNotFound {
        provider: PROVIDER_ID.to_string(),
        model: model.to_string(),
    })
}

fn auth(api_key: Option<&str>) -> Auth {
    if let Some(key) = api_key.map(str::trim).filter(|key| !key.is_empty()) {
        return Auth::InlineBearer {
            value: key.to_string(),
        };
    }
    if std::env::var(API_KEY_ENV)
        .ok()
        .is_some_and(|key| !key.trim().is_empty())
    {
        return Auth::Env {
            var: API_KEY_ENV.to_string(),
        };
    }
    Auth::None
}

fn fallback_display_name(id: &str) -> String {
    id.split('/').next_back().unwrap_or(id).replace('-', " ")
}
