use std::collections::BTreeMap;

use super::catalog_cache;
use super::deepseek;
use super::lmstudio;
use super::models_dev::OpenRouterCatalog;
use super::ollama;
use super::ollama_cloud;
use super::openrouter;
use super::types::{
    ModelDefinition, ModelId, ModelRef, ProviderDefinition, ProviderId, ProviderSettings,
    ResolvedModel,
};
use super::zai;
use crate::runtime_daemon::chat::settings;

#[derive(Clone, Debug, Default)]
pub(crate) struct Catalog {
    providers: BTreeMap<ProviderId, ProviderDefinition>,
    models: BTreeMap<(ProviderId, ModelId), ModelDefinition>,
    default_model: Option<ModelRef>,
}

impl Catalog {
    pub(crate) fn from_settings(settings: &ProviderSettings) -> Self {
        let needs_hosted_catalog =
            key_or_env_present(settings.openrouter_api_key.as_ref(), "OPENROUTER_API_KEY")
                || key_or_env_present(settings.ollama_cloud_api_key.as_ref(), "OLLAMA_API_KEY")
                || key_or_env_present(settings.lmstudio_api_key.as_ref(), lmstudio::API_KEY_ENV)
                || key_or_env_present(settings.deepseek_api_key.as_ref(), deepseek::API_KEY_ENV)
                || key_or_env_present(settings.zai_api_key.as_ref(), zai::API_KEY_ENV);
        let hosted_catalog = needs_hosted_catalog
            .then(catalog_cache::load_disk_blocking)
            .and_then(Result::ok);
        Self::from_settings_and_openrouter(
            settings,
            hosted_catalog.as_ref().map(|cached| &cached.catalog),
            hosted_catalog.as_ref().map(|cached| &cached.ollama_cloud),
            hosted_catalog.as_ref().map(|cached| &cached.lmstudio),
            hosted_catalog.as_ref().map(|cached| &cached.deepseek),
            hosted_catalog.as_ref().map(|cached| &cached.zai),
        )
    }

    pub(crate) fn from_settings_and_openrouter(
        settings: &ProviderSettings,
        hosted_openrouter: Option<&OpenRouterCatalog>,
        hosted_ollama_cloud: Option<&OpenRouterCatalog>,
        hosted_lmstudio: Option<&OpenRouterCatalog>,
        hosted_deepseek: Option<&OpenRouterCatalog>,
        hosted_zai: Option<&OpenRouterCatalog>,
    ) -> Self {
        let mut catalog = Self::default();
        catalog.insert_provider(openrouter::provider_definition(
            settings.openrouter_api_key.as_deref(),
        ));
        catalog.insert_provider(ollama_cloud::provider_definition(
            settings.ollama_cloud_api_key.as_deref(),
        ));
        catalog.insert_provider(lmstudio::provider_definition(
            &settings.lmstudio_base_url,
            settings.lmstudio_api_key.as_deref(),
        ));
        catalog.insert_provider(deepseek::provider_definition(
            settings.deepseek_api_key.as_deref(),
        ));
        catalog.insert_provider(zai::provider_definition(settings.zai_api_key.as_deref()));
        catalog.insert_provider(ollama::provider_definition(&settings.ollama_base_url));
        catalog.insert_provider(local_provider_alias(&settings.ollama_base_url));

        if let Some(hosted_openrouter) = hosted_openrouter {
            for model in &hosted_openrouter.models {
                catalog.insert_model(model.definition.clone());
            }
        } else {
            catalog.insert_model(openrouter::model_definition(
                settings::DEFAULT_MODEL,
                Some("Gemini 3.5 Flash".to_string()),
            ));
            for model in settings::recommended_model_ids()
                .into_iter()
                .filter(|model| *model != settings::DEFAULT_MODEL)
            {
                catalog.insert_model(openrouter::model_definition(model, None));
            }
        }
        if let Some(hosted_ollama_cloud) = hosted_ollama_cloud {
            for model in &hosted_ollama_cloud.models {
                catalog.insert_model(model.definition.clone());
            }
        }
        if let Some(hosted_lmstudio) = hosted_lmstudio {
            for model in &hosted_lmstudio.models {
                catalog.insert_model(model.definition.clone());
            }
        }
        if let Some(hosted_deepseek) = hosted_deepseek {
            for model in &hosted_deepseek.models {
                catalog.insert_model(model.definition.clone());
            }
        }
        if let Some(hosted_zai) = hosted_zai {
            for model in &hosted_zai.models {
                catalog.insert_model(model.definition.clone());
            }
        }
        for model in &settings.custom_models {
            if let Ok(model_ref) = model_ref_from_selection(model, &catalog) {
                catalog.ensure_model_for_ref(&model_ref);
            }
        }
        catalog.default_model = Some(ModelRef::new(
            ProviderId::unchecked(ProviderId::OPENROUTER),
            ModelId::new(settings::DEFAULT_MODEL).expect("default model id is valid"),
        ));
        catalog
    }

    pub(crate) fn provider(&self, id: &ProviderId) -> Option<&ProviderDefinition> {
        self.providers.get(id)
    }

    pub(crate) fn resolve(
        &self,
        model_ref: &ModelRef,
    ) -> Result<ResolvedModel, super::error::LlmError> {
        let provider = self
            .providers
            .get(&model_ref.provider)
            .cloned()
            .ok_or_else(|| super::error::LlmError::ProviderNotFound {
                provider: model_ref.provider.to_string(),
            })?;
        if provider.disabled {
            return Err(super::error::LlmError::ProviderApi {
                provider: provider.id.to_string(),
                status: None,
                message: format!("{} is disabled.", provider.name),
                retryable: false,
            });
        }

        let model = self
            .models
            .get(&(model_ref.provider.clone(), model_ref.model.clone()))
            .cloned()
            .unwrap_or_else(|| dynamic_model(&model_ref.provider, &model_ref.model));
        if !model.enabled {
            return Err(super::error::LlmError::ModelNotFound {
                provider: provider.id.to_string(),
                model: model.id.to_string(),
            });
        }

        Ok(resolve_model(provider, model, model_ref.clone()))
    }

    pub(crate) fn default_model(&self) -> Option<&ModelRef> {
        self.default_model.as_ref()
    }

    fn insert_provider(&mut self, provider: ProviderDefinition) {
        self.providers.insert(provider.id.clone(), provider);
    }

    fn insert_model(&mut self, model: ModelDefinition) {
        self.models
            .insert((model.provider.clone(), model.id.clone()), model);
    }

    fn ensure_model_for_ref(&mut self, model_ref: &ModelRef) {
        if self
            .models
            .contains_key(&(model_ref.provider.clone(), model_ref.model.clone()))
        {
            return;
        }
        self.insert_model(dynamic_model(&model_ref.provider, &model_ref.model));
    }
}

pub(crate) fn model_ref_from_selection(
    model: &str,
    catalog: &Catalog,
) -> Result<ModelRef, super::error::LlmError> {
    let clean = model.trim();
    if clean.is_empty() {
        return catalog
            .default_model()
            .cloned()
            .ok_or_else(|| super::error::LlmError::Config {
                message: "No default chat model is configured.".to_string(),
            });
    }

    if let Ok(parsed) = ModelRef::parse(clean) {
        if catalog.provider(&parsed.provider).is_some() {
            return Ok(parsed);
        }
    }

    ModelId::new(clean)
        .map(|model| ModelRef::new(ProviderId::unchecked(ProviderId::OPENROUTER), model))
        .ok_or_else(|| super::error::LlmError::Config {
            message: "Model id is empty.".to_string(),
        })
}

fn resolve_model(
    provider: ProviderDefinition,
    model: ModelDefinition,
    reference: ModelRef,
) -> ResolvedModel {
    let request = provider.request.merged(&model.request);
    ResolvedModel {
        reference,
        provider,
        model,
        request,
    }
}

fn dynamic_model(provider_id: &ProviderId, model_id: &ModelId) -> ModelDefinition {
    match provider_id.as_str() {
        ProviderId::OPENROUTER => openrouter::model_definition(model_id.as_str(), None),
        ProviderId::OLLAMA => ollama::model_definition(model_id.as_str(), None),
        ProviderId::OLLAMA_CLOUD => ollama_cloud::model_definition(model_id.as_str(), None),
        ProviderId::LMSTUDIO => lmstudio::model_definition(model_id.as_str(), None),
        ProviderId::DEEPSEEK => deepseek::model_definition(model_id.as_str(), None),
        ProviderId::ZAI => zai::model_definition(model_id.as_str(), None),
        ProviderId::LOCAL => {
            let mut model = ollama::model_definition(model_id.as_str(), None);
            model.provider = ProviderId::unchecked(ProviderId::LOCAL);
            model
        }
        _ => ModelDefinition {
            id: model_id.clone(),
            provider: provider_id.clone(),
            display_name: model_id.to_string(),
            adapter_model_id: model_id.to_string(),
            capabilities: super::types::Capabilities::text_tools(),
            limits: super::types::Limits::default(),
            request: super::types::RequestDefaults::default(),
            enabled: true,
        },
    }
}

fn local_provider_alias(base_url: &str) -> ProviderDefinition {
    let mut provider = ollama::provider_definition(base_url);
    provider.id = ProviderId::unchecked(ProviderId::LOCAL);
    provider.name = "Local OpenAI-compatible".to_string();
    provider
}

fn key_or_env_present(key: Option<&String>, env_var: &str) -> bool {
    key.is_some_and(|key| !key.trim().is_empty())
        || std::env::var(env_var)
            .ok()
            .is_some_and(|key| !key.trim().is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_catalog() -> Catalog {
        Catalog::from_settings(&ProviderSettings {
            openrouter_api_key: None,
            ollama_cloud_api_key: None,
            lmstudio_api_key: None,
            deepseek_api_key: None,
            zai_api_key: None,
            ollama_base_url: "http://127.0.0.1:11434".to_string(),
            lmstudio_base_url: lmstudio::DEFAULT_BASE_URL.to_string(),
            custom_models: Vec::new(),
        })
    }

    #[test]
    fn canonical_model_ref_uses_provider_segment() {
        let catalog = test_catalog();
        let model_ref =
            model_ref_from_selection("openrouter/google/gemini-3.5-flash", &catalog).unwrap();

        assert_eq!(model_ref.provider.as_str(), "openrouter");
        assert_eq!(model_ref.model.as_str(), "google/gemini-3.5-flash");
    }

    #[test]
    fn legacy_openrouter_model_ids_still_resolve() {
        let catalog = test_catalog();
        let model_ref = model_ref_from_selection("google/gemini-3.5-flash", &catalog).unwrap();

        assert_eq!(model_ref.provider.as_str(), "openrouter");
        assert_eq!(model_ref.model.as_str(), "google/gemini-3.5-flash");
    }
}
