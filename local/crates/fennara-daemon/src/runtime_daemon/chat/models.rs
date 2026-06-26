use reqwest::Client;
use serde::Serialize;
use serde_json::Value;
use std::collections::BTreeMap;
use std::time::Duration;

use super::auth;
use super::providers;
use super::providers::ProviderId;
use super::providers::catalog_cache;
use super::providers::models_dev::{OpenRouterCatalog, OpenRouterCatalogModel};
use super::settings::{self, ChatSettings};

const LOCAL_MODELS_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ModelCatalog {
    pub(crate) models: Vec<ModelInfo>,
    pub(crate) recommended_ids: Vec<&'static str>,
    pub(crate) custom_ids: Vec<String>,
    pub(crate) live: bool,
    pub(crate) error: Option<String>,
    pub(crate) catalog_status: CatalogStatus,
    pub(crate) ollama_status: OllamaStatus,
    pub(crate) local_provider_statuses: BTreeMap<String, LocalProviderStatus>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct CatalogStatus {
    pub(crate) provider: &'static str,
    pub(crate) state: &'static str,
    pub(crate) source_url: Option<String>,
    pub(crate) fetched_at_ms: Option<u128>,
    pub(crate) age_ms: Option<u128>,
    pub(crate) using_stale: bool,
    pub(crate) openrouter_model_count: usize,
    pub(crate) last_error: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct OllamaStatus {
    pub(crate) state: &'static str,
    pub(crate) base_url: String,
    pub(crate) model_count: usize,
    pub(crate) error: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct LocalProviderStatus {
    pub(crate) state: &'static str,
    pub(crate) base_url: String,
    pub(crate) model_count: usize,
    pub(crate) error: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ModelInfo {
    pub(crate) id: String,
    pub(crate) display_name: String,
    pub(crate) provider_id: String,
    pub(crate) provider: String,
    pub(crate) source: &'static str,
    pub(crate) recommended: bool,
    pub(crate) custom: bool,
    pub(crate) verified: bool,
    pub(crate) latest_alias: bool,
    pub(crate) canonical_slug: Option<String>,
    pub(crate) context_length: Option<u64>,
    pub(crate) max_output_tokens: Option<u64>,
    pub(crate) input_cost_per_million: Option<f64>,
    pub(crate) output_cost_per_million: Option<f64>,
    pub(crate) cache_read_cost_per_million: Option<f64>,
    pub(crate) cache_write_cost_per_million: Option<f64>,
    pub(crate) tokens_per_second: Option<f64>,
    pub(crate) modalities: Vec<String>,
    pub(crate) supports_tools: bool,
    pub(crate) supports_reasoning: bool,
    pub(crate) supported_reasoning_efforts: Vec<String>,
    pub(crate) description: Option<String>,
}

pub(crate) async fn list_models(settings: &ChatSettings) -> ModelCatalog {
    let recommended_ids = settings::recommended_model_ids();
    let custom_ids = settings.custom_models.clone();
    catalog_cache::spawn_refresh_if_stale();
    let has_saved_openrouter_key = auth::has_api_key(ProviderId::OPENROUTER)
        || std::env::var("OPENROUTER_API_KEY")
            .ok()
            .is_some_and(|key| !key.trim().is_empty());
    let has_ollama_cloud_key = auth::has_api_key(ProviderId::OLLAMA_CLOUD)
        || std::env::var("OLLAMA_API_KEY")
            .ok()
            .is_some_and(|key| !key.trim().is_empty());
    let has_zai_key = auth::has_api_key(ProviderId::ZAI)
        || std::env::var("ZHIPU_API_KEY")
            .ok()
            .is_some_and(|key| !key.trim().is_empty());
    let has_deepseek_key = auth::has_api_key(ProviderId::DEEPSEEK)
        || std::env::var("DEEPSEEK_API_KEY")
            .ok()
            .is_some_and(|key| !key.trim().is_empty());
    let cached_catalog = catalog_cache::load_disk().await;
    let catalog_status = catalog_status(&cached_catalog);
    let openrouter_error = cached_catalog.as_ref().err().cloned();
    let needs_hosted_catalog =
        has_saved_openrouter_key || has_ollama_cloud_key || has_deepseek_key || has_zai_key;
    let mut models = Vec::new();
    if has_saved_openrouter_key {
        if let Ok(cached_catalog) = &cached_catalog {
            append_openrouter_catalog_models(
                &mut models,
                &cached_catalog.catalog,
                &recommended_ids,
                &custom_ids,
            );
        }
    }
    if has_ollama_cloud_key {
        if let Ok(cached_catalog) = &cached_catalog {
            append_hosted_catalog_models(
                &mut models,
                &cached_catalog.ollama_cloud,
                "Ollama Cloud",
                ProviderId::OLLAMA_CLOUD,
                &custom_ids,
            );
        }
    }
    if has_zai_key {
        if let Ok(cached_catalog) = &cached_catalog {
            append_hosted_catalog_models(
                &mut models,
                &cached_catalog.zai,
                "Z.AI",
                ProviderId::ZAI,
                &custom_ids,
            );
        }
    }
    if has_deepseek_key {
        if let Ok(cached_catalog) = &cached_catalog {
            append_hosted_catalog_models(
                &mut models,
                &cached_catalog.deepseek,
                "DeepSeek",
                ProviderId::DEEPSEEK,
                &custom_ids,
            );
        }
    }

    let ollama_result = tokio::time::timeout(
        LOCAL_MODELS_TIMEOUT,
        fetch_ollama_models(&settings.ollama_base_url),
    )
    .await
    .unwrap_or_else(|_| Err("Ollama models request timed out.".to_string()));
    let ollama_status = match ollama_result {
        Ok(ollama_models) => {
            append_ollama_models(&mut models, &ollama_models, &custom_ids);
            OllamaStatus {
                state: if ollama_models.is_empty() {
                    "empty"
                } else {
                    "ready"
                },
                base_url: settings.ollama_base_url.clone(),
                model_count: ollama_models.len(),
                error: None,
            }
        }
        Err(error) => OllamaStatus {
            state: "offline",
            base_url: settings.ollama_base_url.clone(),
            model_count: 0,
            error: Some(error),
        },
    };

    let lmstudio_base_url = settings.provider_base_url(
        ProviderId::LMSTUDIO,
        providers::lmstudio_v1_base_url("").as_str(),
    );
    let lmstudio_result = tokio::time::timeout(
        LOCAL_MODELS_TIMEOUT,
        fetch_lmstudio_models(&lmstudio_base_url),
    )
    .await
    .unwrap_or_else(|_| Err("LM Studio models request timed out.".to_string()));
    let lmstudio_status = match lmstudio_result {
        Ok(lmstudio_models) => {
            let catalog = cached_catalog.as_ref().ok().map(|cached| &cached.lmstudio);
            append_lmstudio_models(&mut models, &lmstudio_models, catalog, &custom_ids);
            LocalProviderStatus {
                state: if lmstudio_models.is_empty() {
                    "empty"
                } else {
                    "ready"
                },
                base_url: lmstudio_base_url.clone(),
                model_count: lmstudio_models.len(),
                error: None,
            }
        }
        Err(error) => LocalProviderStatus {
            state: "offline",
            base_url: lmstudio_base_url.clone(),
            model_count: 0,
            error: Some(error),
        },
    };

    let mut local_provider_statuses = BTreeMap::new();
    local_provider_statuses.insert(
        ProviderId::OLLAMA.to_string(),
        LocalProviderStatus {
            state: ollama_status.state,
            base_url: ollama_status.base_url.clone(),
            model_count: ollama_status.model_count,
            error: ollama_status.error.clone(),
        },
    );
    local_provider_statuses.insert(ProviderId::LMSTUDIO.to_string(), lmstudio_status);

    let ollama_live = matches!(ollama_status.state, "ready" | "empty");
    let local_live = local_provider_statuses
        .values()
        .any(|status| matches!(status.state, "ready" | "empty"));
    let live = openrouter_error.is_none() || ollama_live || local_live;
    ModelCatalog {
        models,
        recommended_ids,
        custom_ids,
        live,
        error: if needs_hosted_catalog {
            openrouter_error
        } else {
            None
        },
        catalog_status,
        ollama_status,
        local_provider_statuses,
    }
}

pub(crate) async fn refresh_model_catalog(force: bool) -> Result<CatalogStatus, String> {
    let refreshed = catalog_cache::refresh(force).await;
    let status = catalog_status(&refreshed);
    refreshed.map(|_| status)
}

pub(crate) fn spawn_catalog_refresh_if_needed() {
    catalog_cache::spawn_refresh_if_stale();
}

fn catalog_status(
    cached_catalog: &Result<catalog_cache::CachedOpenRouterCatalog, String>,
) -> CatalogStatus {
    match cached_catalog {
        Ok(cached) => CatalogStatus {
            provider: "openrouter",
            state: if cached.stale { "stale" } else { "ready" },
            source_url: Some(cached.meta.source_url.clone()),
            fetched_at_ms: Some(cached.meta.fetched_at_ms),
            age_ms: cached.meta.age_ms(),
            using_stale: cached.stale,
            openrouter_model_count: cached.catalog.models.len(),
            last_error: None,
        },
        Err(error) => CatalogStatus {
            provider: "openrouter",
            state: "empty",
            source_url: Some(catalog_cache::DEFAULT_MODELS_DEV_URL.to_string()),
            fetched_at_ms: None,
            age_ms: None,
            using_stale: false,
            openrouter_model_count: 0,
            last_error: Some(error.clone()),
        },
    }
}

fn append_openrouter_catalog_models(
    models: &mut Vec<ModelInfo>,
    catalog: &OpenRouterCatalog,
    recommended_ids: &[&'static str],
    custom_ids: &[String],
) {
    for model in &catalog.models {
        let id = model.definition.id.as_str();
        let prefixed_id = openrouter_model_id(id);
        models.push(openrouter_catalog_model_info(
            model,
            recommended_ids.contains(&id),
            custom_ids
                .iter()
                .any(|custom| custom == id || custom == &prefixed_id),
        ));
    }
    for id in custom_ids.iter().filter(|id| {
        !id.starts_with("ollama/")
            && !id.starts_with("ollama-cloud/")
            && !id.starts_with("lmstudio/")
            && !id.starts_with("deepseek/")
            && !id.starts_with("zai/")
    }) {
        if models
            .iter()
            .any(|model| model.id == *id || model.canonical_slug.as_deref() == Some(id.as_str()))
        {
            continue;
        }
        let mut model = model_info(id, None, recommended_ids.contains(&id.as_str()), true);
        model.provider_id = ProviderId::OPENROUTER.to_string();
        model.provider = "OpenRouter".to_string();
        models.push(model);
    }
}

fn append_hosted_catalog_models(
    models: &mut Vec<ModelInfo>,
    catalog: &OpenRouterCatalog,
    provider_label: &str,
    provider_id: &str,
    custom_ids: &[String],
) {
    for catalog_model in &catalog.models {
        let mut model = openrouter_catalog_model_info(
            catalog_model,
            false,
            custom_ids
                .iter()
                .any(|custom| custom == &format!("{provider_id}/{}", catalog_model.definition.id)),
        );
        model.id = format!("{provider_id}/{}", catalog_model.definition.id);
        model.provider_id = provider_id.to_string();
        model.provider = provider_label.to_string();
        model.canonical_slug = Some(catalog_model.definition.id.to_string());
        models.push(model);
    }
}

fn openrouter_catalog_model_info(
    catalog_model: &OpenRouterCatalogModel,
    recommended: bool,
    custom: bool,
) -> ModelInfo {
    let definition = &catalog_model.definition;
    let mut modalities = definition
        .capabilities
        .input
        .iter()
        .map(|modality| format!("in:{modality}"))
        .chain(
            definition
                .capabilities
                .output
                .iter()
                .map(|modality| format!("out:{modality}")),
        )
        .collect::<Vec<_>>();
    modalities.sort();
    modalities.dedup();
    ModelInfo {
        id: openrouter_model_id(definition.id.as_str()),
        display_name: definition.display_name.clone(),
        provider_id: ProviderId::OPENROUTER.to_string(),
        provider: "OpenRouter".to_string(),
        source: if custom { "custom" } else { "catalog" },
        recommended,
        custom,
        verified: true,
        latest_alias: false,
        canonical_slug: Some(definition.id.to_string()),
        context_length: definition.limits.context_tokens.map(u64::from),
        max_output_tokens: definition.limits.output_tokens.map(u64::from),
        input_cost_per_million: catalog_model.input_cost_per_million,
        output_cost_per_million: catalog_model.output_cost_per_million,
        cache_read_cost_per_million: catalog_model.cache_read_cost_per_million,
        cache_write_cost_per_million: catalog_model.cache_write_cost_per_million,
        tokens_per_second: None,
        modalities,
        supports_tools: definition.capabilities.tools,
        supports_reasoning: definition.capabilities.reasoning,
        supported_reasoning_efforts: if definition.capabilities.reasoning {
            vec!["low".to_string(), "medium".to_string(), "high".to_string()]
        } else {
            Vec::new()
        },
        description: catalog_model.family.as_ref().map(|family| {
            if catalog_model.status == "beta" {
                format!("{family} family. Beta model.")
            } else {
                format!("{family} family.")
            }
        }),
    }
}

fn openrouter_model_id(model_id: &str) -> String {
    let clean = model_id.trim();
    if clean.starts_with("openrouter/") {
        clean.to_string()
    } else {
        format!("openrouter/{clean}")
    }
}

async fn fetch_ollama_models(base_url: &str) -> Result<Vec<Value>, String> {
    let client = Client::builder()
        .connect_timeout(Duration::from_secs(2))
        .timeout(LOCAL_MODELS_TIMEOUT)
        .build()
        .map_err(|error| format!("Failed to create Ollama HTTP client: {error}"))?;
    let response = client
        .get(format!(
            "{}/api/tags",
            providers::ollama_api_base_url(base_url)
        ))
        .send()
        .await
        .map_err(|error| format!("Failed to fetch Ollama models: {error}"))?;
    if !response.status().is_success() {
        return Err(format!(
            "Ollama models request failed: {}",
            response.status()
        ));
    }
    let body: Value = response
        .json()
        .await
        .map_err(|error| format!("Ollama models response was invalid: {error}"))?;
    Ok(body
        .get("models")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default())
}

async fn fetch_lmstudio_models(base_url: &str) -> Result<Vec<Value>, String> {
    let client = Client::builder()
        .connect_timeout(Duration::from_secs(2))
        .timeout(LOCAL_MODELS_TIMEOUT)
        .build()
        .map_err(|error| format!("Failed to create LM Studio HTTP client: {error}"))?;
    let response = client
        .get(format!(
            "{}/models",
            providers::lmstudio_v1_base_url(base_url)
        ))
        .send()
        .await
        .map_err(|error| format!("Failed to fetch LM Studio models: {error}"))?;
    if !response.status().is_success() {
        return Err(format!(
            "LM Studio models request failed: {}",
            response.status()
        ));
    }
    let body: Value = response
        .json()
        .await
        .map_err(|error| format!("LM Studio models response was invalid: {error}"))?;
    Ok(body
        .get("data")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default())
}

fn append_ollama_models(models: &mut Vec<ModelInfo>, raw_models: &[Value], custom_ids: &[String]) {
    for raw in raw_models {
        let Some(local_id) = raw
            .get("name")
            .or_else(|| raw.get("model"))
            .and_then(Value::as_str)
        else {
            continue;
        };
        let id = format!("ollama/{local_id}");
        if models.iter().any(|model| model.id == id) {
            continue;
        }
        models.push(ollama_model_info(
            &id,
            Some(raw),
            custom_ids.iter().any(|custom| custom == &id),
            true,
        ));
    }
}

fn append_lmstudio_models(
    models: &mut Vec<ModelInfo>,
    raw_models: &[Value],
    catalog: Option<&OpenRouterCatalog>,
    custom_ids: &[String],
) {
    for raw in raw_models {
        let Some(local_id) = raw
            .get("id")
            .or_else(|| raw.get("model"))
            .and_then(Value::as_str)
        else {
            continue;
        };
        let id = format!("lmstudio/{local_id}");
        if models.iter().any(|model| model.id == id) {
            continue;
        }
        models.push(lmstudio_model_info(
            &id,
            Some(raw),
            catalog.and_then(|catalog| catalog.model(local_id)),
            custom_ids.iter().any(|custom| custom == &id),
        ));
    }
}

fn model_info(id: &str, raw: Option<&Value>, recommended: bool, custom: bool) -> ModelInfo {
    let display_name = raw
        .and_then(|model| model.get("name"))
        .and_then(Value::as_str)
        .map(clean_display_name)
        .unwrap_or_else(|| fallback_display_name(id));
    let provider = raw
        .and_then(|model| model.get("name"))
        .and_then(Value::as_str)
        .and_then(|name| name.split(':').next())
        .map(str::trim)
        .filter(|provider| !provider.is_empty())
        .map(ToString::to_string)
        .unwrap_or_else(|| fallback_provider(id));
    let architecture = raw.and_then(|model| model.get("architecture"));
    let input_modalities = string_array(
        architecture
            .and_then(|value| value.get("input_modalities"))
            .or_else(|| raw.and_then(|value| value.get("input_modalities"))),
    );
    let output_modalities = string_array(
        architecture
            .and_then(|value| value.get("output_modalities"))
            .or_else(|| raw.and_then(|value| value.get("output_modalities"))),
    );
    let mut modalities = input_modalities
        .iter()
        .map(|modality| format!("in:{modality}"))
        .chain(
            output_modalities
                .iter()
                .map(|modality| format!("out:{modality}")),
        )
        .collect::<Vec<_>>();
    modalities.sort();
    modalities.dedup();

    let supported_parameters =
        string_array(raw.and_then(|model| model.get("supported_parameters")));
    let reasoning = raw.and_then(|model| model.get("reasoning"));
    let supported_reasoning_efforts = string_array(
        reasoning
            .and_then(|value| value.get("supported_efforts"))
            .or_else(|| reasoning.and_then(|value| value.get("efforts"))),
    );

    ModelInfo {
        id: id.to_string(),
        display_name,
        provider_id: fallback_provider_id(id).to_string(),
        provider,
        source: if custom { "custom" } else { "recommended" },
        recommended,
        custom,
        verified: raw.is_some_and(model_supports_text_chat),
        latest_alias: id.starts_with('~') || id.ends_with("-latest"),
        canonical_slug: raw
            .and_then(|model| model.get("canonical_slug"))
            .and_then(Value::as_str)
            .map(ToString::to_string),
        context_length: raw
            .and_then(|model| model.get("context_length"))
            .and_then(Value::as_u64),
        max_output_tokens: raw
            .and_then(|model| model.get("top_provider"))
            .and_then(|provider| provider.get("max_completion_tokens"))
            .and_then(Value::as_u64),
        input_cost_per_million: raw
            .and_then(|model| model.get("pricing"))
            .and_then(|pricing| pricing.get("prompt"))
            .and_then(price_per_million),
        output_cost_per_million: raw
            .and_then(|model| model.get("pricing"))
            .and_then(|pricing| pricing.get("completion"))
            .and_then(price_per_million),
        cache_read_cost_per_million: None,
        cache_write_cost_per_million: None,
        tokens_per_second: raw
            .and_then(|model| model.get("top_provider"))
            .and_then(|provider| provider.get("throughput"))
            .or_else(|| raw.and_then(|model| model.get("throughput")))
            .and_then(Value::as_f64),
        modalities,
        supports_tools: supported_parameters
            .iter()
            .any(|parameter| parameter == "tools" || parameter == "tool_choice"),
        supports_reasoning: reasoning.is_some()
            || supported_parameters
                .iter()
                .any(|parameter| parameter == "reasoning" || parameter == "include_reasoning"),
        supported_reasoning_efforts,
        description: raw
            .and_then(|model| model.get("description"))
            .and_then(Value::as_str)
            .map(|description| description.trim().to_string()),
    }
}

fn ollama_model_info(id: &str, raw: Option<&Value>, custom: bool, verified: bool) -> ModelInfo {
    let local_id = providers::ollama_model_id(id).unwrap_or(id);
    let capabilities = string_array(raw.and_then(|model| model.get("capabilities")));
    let mut modalities = vec!["in:text".to_string(), "out:text".to_string()];
    if capabilities.iter().any(|capability| capability == "vision") {
        modalities.push("in:image".to_string());
    }
    ModelInfo {
        id: id.to_string(),
        display_name: fallback_display_name(local_id),
        provider_id: ProviderId::OLLAMA.to_string(),
        provider: "Ollama".to_string(),
        source: if custom { "custom" } else { "local" },
        recommended: false,
        custom,
        verified,
        latest_alias: false,
        canonical_slug: raw
            .and_then(|model| model.get("name").or_else(|| model.get("model")))
            .and_then(Value::as_str)
            .map(ToString::to_string),
        context_length: raw
            .and_then(|model| model.get("details"))
            .and_then(|details| details.get("context_length"))
            .and_then(Value::as_u64),
        max_output_tokens: None,
        input_cost_per_million: Some(0.0),
        output_cost_per_million: Some(0.0),
        cache_read_cost_per_million: Some(0.0),
        cache_write_cost_per_million: Some(0.0),
        tokens_per_second: None,
        modalities,
        supports_tools: capabilities.iter().any(|capability| capability == "tools"),
        supports_reasoning: false,
        supported_reasoning_efforts: Vec::new(),
        description: raw.and_then(ollama_description),
    }
}

fn lmstudio_model_info(
    id: &str,
    raw: Option<&Value>,
    catalog_model: Option<&OpenRouterCatalogModel>,
    custom: bool,
) -> ModelInfo {
    let local_id = providers::lmstudio_model_id(id).unwrap_or(id);
    if let Some(catalog_model) = catalog_model {
        let mut model = openrouter_catalog_model_info(catalog_model, false, custom);
        model.id = id.to_string();
        model.provider_id = ProviderId::LMSTUDIO.to_string();
        model.provider = "LM Studio".to_string();
        model.source = "local";
        model.verified = true;
        model.canonical_slug = Some(local_id.to_string());
        return model;
    }

    ModelInfo {
        id: id.to_string(),
        display_name: raw
            .and_then(|model| model.get("id"))
            .and_then(Value::as_str)
            .map(fallback_display_name)
            .unwrap_or_else(|| fallback_display_name(local_id)),
        provider_id: ProviderId::LMSTUDIO.to_string(),
        provider: "LM Studio".to_string(),
        source: if custom { "custom" } else { "local" },
        recommended: false,
        custom,
        verified: true,
        latest_alias: false,
        canonical_slug: Some(local_id.to_string()),
        context_length: None,
        max_output_tokens: None,
        input_cost_per_million: Some(0.0),
        output_cost_per_million: Some(0.0),
        cache_read_cost_per_million: Some(0.0),
        cache_write_cost_per_million: Some(0.0),
        tokens_per_second: None,
        modalities: vec!["in:text".to_string(), "out:text".to_string()],
        supports_tools: true,
        supports_reasoning: false,
        supported_reasoning_efforts: Vec::new(),
        description: Some("Loaded from the local LM Studio server.".to_string()),
    }
}

fn ollama_description(model: &Value) -> Option<String> {
    let details = model.get("details")?;
    let size = details.get("parameter_size").and_then(Value::as_str)?;
    let quant = details.get("quantization_level").and_then(Value::as_str);
    Some(match quant {
        Some(quant) => format!("{size} local model, {quant}."),
        None => format!("{size} local model."),
    })
}

pub(crate) fn model_supports_text_chat(model: &Value) -> bool {
    let architecture = model.get("architecture");
    if architecture
        .and_then(|value| value.get("modality"))
        .and_then(Value::as_str)
        .is_some_and(|modality| modality.to_ascii_lowercase().contains("text->text"))
    {
        return true;
    }

    let has_text_input = string_array_contains(
        architecture.and_then(|value| value.get("input_modalities")),
        "text",
    ) || string_array_contains(model.get("input_modalities"), "text");
    let has_text_output = string_array_contains(
        architecture.and_then(|value| value.get("output_modalities")),
        "text",
    ) || string_array_contains(model.get("output_modalities"), "text");
    has_text_input && has_text_output
}

pub(crate) fn model_supports_image_chat(model: &Value) -> bool {
    if !model_supports_text_chat(model) {
        return false;
    }
    let architecture = model.get("architecture");
    string_array_contains(
        architecture.and_then(|value| value.get("input_modalities")),
        "image",
    ) || string_array_contains(model.get("input_modalities"), "image")
        || architecture
            .and_then(|value| value.get("modality"))
            .and_then(Value::as_str)
            .is_some_and(|modality| modality.to_ascii_lowercase().contains("image"))
}

fn clean_display_name(name: &str) -> String {
    name.split_once(':')
        .map(|(_, display)| display.trim())
        .unwrap_or(name.trim())
        .to_string()
}

fn fallback_display_name(id: &str) -> String {
    id.trim_start_matches('~')
        .split('/')
        .next_back()
        .unwrap_or(id)
        .replace('-', " ")
        .replace("latest", "Latest")
}

fn fallback_provider(id: &str) -> String {
    let provider = id
        .trim_start_matches('~')
        .split('/')
        .next()
        .unwrap_or("OpenRouter");
    match provider {
        "z-ai" => "Z.ai".to_string(),
        "x-ai" => "xAI".to_string(),
        "moonshotai" => "MoonshotAI".to_string(),
        other => {
            let mut chars = other.chars();
            match chars.next() {
                Some(first) => format!("{}{}", first.to_uppercase(), chars.as_str()),
                None => "OpenRouter".to_string(),
            }
        }
    }
}

fn fallback_provider_id(id: &str) -> &'static str {
    if id.starts_with("ollama/") {
        ProviderId::OLLAMA
    } else if id.starts_with("ollama-cloud/") {
        ProviderId::OLLAMA_CLOUD
    } else if id.starts_with("lmstudio/") {
        ProviderId::LMSTUDIO
    } else if id.starts_with("deepseek/") {
        ProviderId::DEEPSEEK
    } else if id.starts_with("zai/") {
        ProviderId::ZAI
    } else {
        ProviderId::OPENROUTER
    }
}

fn price_per_million(value: &Value) -> Option<f64> {
    let price = value
        .as_str()
        .and_then(|raw| raw.parse::<f64>().ok())
        .or_else(|| value.as_f64())?;
    if price < 0.0 {
        return None;
    }
    Some(price * 1_000_000.0)
}

fn string_array(value: Option<&Value>) -> Vec<String> {
    value
        .and_then(Value::as_array)
        .map(|items| {
            items
                .iter()
                .filter_map(Value::as_str)
                .map(ToString::to_string)
                .collect()
        })
        .unwrap_or_default()
}

fn string_array_contains(value: Option<&Value>, needle: &str) -> bool {
    value
        .and_then(Value::as_array)
        .is_some_and(|items| items.iter().any(|item| item.as_str() == Some(needle)))
}
