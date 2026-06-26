mod adapters;
mod capability_check;
mod catalog;
pub(crate) mod catalog_cache;
mod context;
mod deepseek;
mod error;
mod lmstudio;
pub(crate) mod models_dev;
mod ollama;
mod ollama_cloud;
mod openrouter;
mod request;
mod resolver;
mod sse;
mod stream;
mod types;
pub(crate) mod usage;
mod zai;

use std::collections::HashMap;
use std::sync::Arc;

use serde::Serialize;

use super::auth;
use super::settings::ChatSettings;
use request::LlmRequest;
use stream::StreamEvent;
use tokio::sync::Mutex;

pub(crate) use error::LlmError;
pub(crate) use request::build_messages;
pub(crate) use types::{ChatCompletion, ChatRequest, ProviderId, ProviderSettings, StreamItem};

#[derive(Clone, Debug, Serialize)]
pub(crate) struct PublicProvider {
    pub(crate) id: String,
    pub(crate) name: String,
    pub(crate) kind: &'static str,
    pub(crate) auth: PublicProviderAuth,
    pub(crate) connected: bool,
    pub(crate) model_prefix: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) setup: Option<PublicProviderSetup>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct PublicProviderAuth {
    #[serde(rename = "type")]
    pub(crate) kind: &'static str,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) env: Option<&'static str>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct PublicProviderSetup {
    #[serde(rename = "type")]
    pub(crate) kind: &'static str,
    pub(crate) default_base_url: &'static str,
    pub(crate) base_url: String,
}

pub(crate) fn settings_from_chat(settings: &ChatSettings) -> ProviderSettings {
    ProviderSettings {
        openrouter_api_key: auth::api_key(types::ProviderId::OPENROUTER),
        ollama_cloud_api_key: auth::api_key(types::ProviderId::OLLAMA_CLOUD),
        lmstudio_api_key: auth::api_key(types::ProviderId::LMSTUDIO),
        deepseek_api_key: auth::api_key(types::ProviderId::DEEPSEEK),
        zai_api_key: auth::api_key(types::ProviderId::ZAI),
        ollama_base_url: settings.ollama_base_url.clone(),
        lmstudio_base_url: settings
            .provider_base_url(types::ProviderId::LMSTUDIO, lmstudio::DEFAULT_BASE_URL),
        custom_models: settings.custom_models.clone(),
    }
}

pub(crate) fn public_provider_registry(settings: &ChatSettings) -> Vec<PublicProvider> {
    vec![
        api_key_provider(
            openrouter::provider_definition(None),
            "cloud",
            "OPENROUTER_API_KEY",
        ),
        local_provider(
            ollama::provider_definition(&settings.ollama_base_url),
            "local",
            super::settings::DEFAULT_OLLAMA_BASE_URL,
            settings.ollama_base_url.clone(),
        ),
        local_provider(
            lmstudio::provider_definition(
                &settings
                    .provider_base_url(types::ProviderId::LMSTUDIO, lmstudio::DEFAULT_BASE_URL),
                None,
            ),
            "local",
            lmstudio::DEFAULT_BASE_URL,
            settings.provider_base_url(types::ProviderId::LMSTUDIO, lmstudio::DEFAULT_BASE_URL),
        ),
        api_key_provider(
            ollama_cloud::provider_definition(None),
            "cloud",
            "OLLAMA_API_KEY",
        ),
        api_key_provider(
            deepseek::provider_definition(None),
            "cloud",
            deepseek::API_KEY_ENV,
        ),
        api_key_provider(zai::provider_definition(None), "cloud", zai::API_KEY_ENV),
    ]
}

pub(crate) fn provider_has_api_key(provider_id: &str, env_var: &str) -> bool {
    auth::has_api_key(provider_id)
        || std::env::var(env_var)
            .ok()
            .is_some_and(|key| !key.trim().is_empty())
}

fn api_key_provider(
    provider: types::ProviderDefinition,
    kind: &'static str,
    env_var: &'static str,
) -> PublicProvider {
    let provider_id = provider.id.to_string();
    PublicProvider {
        id: provider_id.clone(),
        name: provider.name,
        kind,
        auth: PublicProviderAuth {
            kind: "api_key",
            env: Some(env_var),
        },
        connected: provider_has_api_key(&provider_id, env_var),
        model_prefix: format!("{provider_id}/"),
        setup: None,
    }
}

fn local_provider(
    provider: types::ProviderDefinition,
    kind: &'static str,
    default_base_url: &'static str,
    base_url: String,
) -> PublicProvider {
    let provider_id = provider.id.to_string();
    PublicProvider {
        id: provider_id.clone(),
        name: provider.name,
        kind,
        auth: PublicProviderAuth {
            kind: "none",
            env: None,
        },
        connected: true,
        model_prefix: format!("{provider_id}/"),
        setup: Some(PublicProviderSetup {
            kind: "base_url",
            default_base_url,
            base_url,
        }),
    }
}

pub(crate) fn missing_auth_for_model(_settings: &ChatSettings, model: &str) -> Option<LlmError> {
    let (provider_id, provider_name, env_var) = auth_provider_for_model(model)?;
    (!provider_has_api_key(provider_id, env_var)).then(|| LlmError::Auth {
        provider: provider_id.to_string(),
        message: format!("Add your {provider_name} API key first."),
    })
}

fn auth_provider_for_model(model: &str) -> Option<(&'static str, &'static str, &'static str)> {
    match selected_provider_for_model(model) {
        types::ProviderId::OPENROUTER => Some((
            types::ProviderId::OPENROUTER,
            "OpenRouter",
            "OPENROUTER_API_KEY",
        )),
        types::ProviderId::OLLAMA_CLOUD => Some((
            types::ProviderId::OLLAMA_CLOUD,
            "Ollama Cloud",
            "OLLAMA_API_KEY",
        )),
        types::ProviderId::DEEPSEEK => Some((
            types::ProviderId::DEEPSEEK,
            "DeepSeek",
            deepseek::API_KEY_ENV,
        )),
        types::ProviderId::ZAI => Some((types::ProviderId::ZAI, "Z.AI", zai::API_KEY_ENV)),
        _ => None,
    }
}

fn selected_provider_for_model(model: &str) -> &'static str {
    let clean = model.trim();
    [
        types::ProviderId::OPENROUTER,
        types::ProviderId::OLLAMA_CLOUD,
        types::ProviderId::OLLAMA,
        types::ProviderId::LMSTUDIO,
        types::ProviderId::DEEPSEEK,
        types::ProviderId::ZAI,
    ]
    .into_iter()
    .find(|provider| has_provider_prefix(clean, provider))
    .unwrap_or(types::ProviderId::OPENROUTER)
}

fn has_provider_prefix(model: &str, provider: &str) -> bool {
    model
        .strip_prefix(provider)
        .is_some_and(|rest| rest.starts_with('/'))
}

pub(crate) async fn stream_chat<F, Fut>(
    settings: &ProviderSettings,
    request: &ChatRequest,
    on_item: F,
) -> Result<ChatCompletion, LlmError>
where
    F: FnMut(StreamItem) -> Fut + Send,
    Fut: std::future::Future<Output = Result<bool, LlmError>> + Send,
{
    let llm_request = LlmRequest::from_chat(settings, request)?;
    capability_check::preflight(&llm_request)?;
    let _ = context::preflight(&llm_request)?;
    validate_provider_request(&llm_request).await?;

    let accumulator = Arc::new(Mutex::new(StreamAccumulator::default()));
    let on_item = Arc::new(Mutex::new(on_item));
    adapters::openai_compatible::stream_chat(&llm_request, {
        let accumulator = Arc::clone(&accumulator);
        let on_item = Arc::clone(&on_item);
        move |event| {
            let accumulator = Arc::clone(&accumulator);
            let on_item = Arc::clone(&on_item);
            async move {
                let items = {
                    let mut accumulator = accumulator.lock().await;
                    accumulator.items_for_event(event)?
                };
                for item in items {
                    let mut on_item = on_item.lock().await;
                    if !on_item(item).await? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
        }
    })
    .await
}

#[derive(Default)]
struct StreamAccumulator {
    text: String,
    reasoning: String,
    emit_lens: HashMap<String, usize>,
}

impl StreamAccumulator {
    fn items_for_event(&mut self, event: StreamEvent) -> Result<Vec<StreamItem>, LlmError> {
        let mut items = Vec::new();
        match event {
            StreamEvent::TextDelta { text: delta, .. } => {
                self.text.push_str(&delta);
                if self.text.len().saturating_sub(self.emit_len("__text__")) >= 24 {
                    self.emit_lens
                        .insert("__text__".to_string(), self.text.len());
                    items.push(StreamItem::Text {
                        content: self.text.clone(),
                        done: false,
                    });
                }
            }
            StreamEvent::ReasoningDelta {
                text: reasoning_delta,
                ..
            } => {
                if reasoning_delta.starts_with(self.reasoning.as_str()) {
                    self.reasoning = reasoning_delta;
                } else {
                    self.reasoning.push_str(&reasoning_delta);
                }
                if self
                    .reasoning
                    .len()
                    .saturating_sub(self.emit_len("__reasoning__"))
                    >= 48
                {
                    self.emit_lens
                        .insert("__reasoning__".to_string(), self.reasoning.len());
                    items.push(StreamItem::Reasoning {
                        content: self.reasoning.clone(),
                        done: false,
                    });
                }
            }
            StreamEvent::ToolCallDelta {
                id,
                name,
                arguments,
            } => {
                let last_len = self.emit_len(&id);
                if arguments.len().saturating_sub(last_len) >= 24 || !name.is_empty() {
                    self.emit_lens.insert(id.clone(), arguments.len());
                    items.push(StreamItem::FunctionCall {
                        id,
                        name,
                        arguments,
                        done: false,
                    });
                }
            }
            StreamEvent::ToolCall {
                id,
                name,
                arguments,
                ..
            } => {
                items.push(StreamItem::FunctionCall {
                    id,
                    name,
                    arguments,
                    done: true,
                });
            }
            StreamEvent::Usage(usage) => {
                if let Some(raw) = usage.raw {
                    items.push(StreamItem::Usage(raw));
                }
            }
            StreamEvent::ProviderError(error) => return Err(error),
            StreamEvent::Finish { usage, .. } => {
                if !self.reasoning.is_empty() {
                    items.push(StreamItem::Reasoning {
                        content: self.reasoning.clone(),
                        done: true,
                    });
                }
                if !self.text.is_empty() {
                    items.push(StreamItem::Text {
                        content: self.text.clone(),
                        done: true,
                    });
                }
                if let Some(usage) = usage.and_then(|usage| usage.raw) {
                    items.push(StreamItem::Usage(usage));
                }
            }
            StreamEvent::StepStart { .. } => {}
        }
        Ok(items)
    }

    fn emit_len(&self, key: &str) -> usize {
        *self.emit_lens.get(key).unwrap_or(&0)
    }
}

async fn validate_provider_request(request: &LlmRequest) -> Result<(), LlmError> {
    match request.model.provider.id.as_str() {
        types::ProviderId::OPENROUTER => openrouter::validate_request(request).await,
        types::ProviderId::OLLAMA_CLOUD => Ok(()),
        types::ProviderId::LMSTUDIO => lmstudio::validate_request(request).await,
        types::ProviderId::DEEPSEEK => Ok(()),
        types::ProviderId::ZAI => Ok(()),
        types::ProviderId::OLLAMA | types::ProviderId::LOCAL => {
            ollama::validate_request(request).await
        }
        _ => Ok(()),
    }
}

pub(crate) fn ollama_model_id(model: &str) -> Option<&str> {
    ollama::model_id(model)
}

pub(crate) fn lmstudio_model_id(model: &str) -> Option<&str> {
    lmstudio::model_id(model)
}

pub(crate) fn ollama_api_base_url(base_url: &str) -> String {
    ollama::api_base_url(base_url)
}

pub(crate) fn lmstudio_v1_base_url(base_url: &str) -> String {
    lmstudio::v1_base_url(base_url)
}

pub(crate) fn pricing_for_model(
    settings: &ProviderSettings,
    model: &str,
    context_tokens: u64,
) -> Option<models_dev::CostRates> {
    let catalog = catalog::Catalog::from_settings(settings);
    let model_ref = catalog::model_ref_from_selection(model, &catalog).ok()?;
    let cached = catalog_cache::load_disk_blocking().ok()?;
    let provider_catalog = match model_ref.provider.as_str() {
        types::ProviderId::OPENROUTER => &cached.catalog,
        types::ProviderId::OLLAMA_CLOUD => &cached.ollama_cloud,
        types::ProviderId::LMSTUDIO => &cached.lmstudio,
        types::ProviderId::DEEPSEEK => &cached.deepseek,
        types::ProviderId::ZAI => &cached.zai,
        _ => return None,
    };
    provider_catalog
        .model(model_ref.model.as_str())
        .map(|model| model.pricing_for_context(context_tokens))
}

#[allow(dead_code)]
pub(crate) fn parse_model_ref(model: &str) -> Result<String, LlmError> {
    let catalog = catalog::Catalog::from_settings(&ProviderSettings {
        openrouter_api_key: None,
        ollama_cloud_api_key: None,
        lmstudio_api_key: None,
        deepseek_api_key: None,
        zai_api_key: None,
        ollama_base_url: super::settings::DEFAULT_OLLAMA_BASE_URL.to_string(),
        lmstudio_base_url: lmstudio::DEFAULT_BASE_URL.to_string(),
        custom_models: Vec::new(),
    });
    catalog::model_ref_from_selection(model, &catalog).map(|model_ref| model_ref.canonical())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn openrouter_vendor_namespaces_are_not_provider_prefixes() {
        assert_eq!(
            selected_provider_for_model("google/gemini-3.5-flash"),
            types::ProviderId::OPENROUTER
        );
        assert_eq!(
            selected_provider_for_model("openai/gpt-5.5"),
            types::ProviderId::OPENROUTER
        );
        assert_eq!(
            selected_provider_for_model("openrouter/google/gemini-3.5-flash"),
            types::ProviderId::OPENROUTER
        );
        assert_eq!(
            selected_provider_for_model("ollama/llama3.2"),
            types::ProviderId::OLLAMA
        );
    }
}
