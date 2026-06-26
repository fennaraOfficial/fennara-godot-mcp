use serde::Deserialize;
use std::collections::BTreeMap;

use super::openrouter;
use super::types::{
    Capabilities, GenerationDefaults, Limits, ModelDefinition, ModelId, ProviderId, RequestDefaults,
};

pub(crate) const OPENROUTER_PROVIDER_ID: &str = ProviderId::OPENROUTER;
pub(crate) const OLLAMA_CLOUD_PROVIDER_ID: &str = ProviderId::OLLAMA_CLOUD;
pub(crate) const LMSTUDIO_PROVIDER_ID: &str = ProviderId::LMSTUDIO;
pub(crate) const DEEPSEEK_PROVIDER_ID: &str = ProviderId::DEEPSEEK;
pub(crate) const ZAI_PROVIDER_ID: &str = ProviderId::ZAI;

#[derive(Clone, Debug, Default)]
pub(crate) struct OpenRouterCatalog {
    pub(crate) models: Vec<OpenRouterCatalogModel>,
}

impl OpenRouterCatalog {
    pub(crate) fn model(&self, model_id: &str) -> Option<&OpenRouterCatalogModel> {
        self.models
            .iter()
            .find(|model| model.definition.id.as_str() == model_id)
    }
}

#[derive(Clone, Debug)]
pub(crate) struct OpenRouterCatalogModel {
    pub(crate) definition: ModelDefinition,
    pub(crate) family: Option<String>,
    pub(crate) status: String,
    pub(crate) input_cost_per_million: Option<f64>,
    pub(crate) output_cost_per_million: Option<f64>,
    pub(crate) cache_read_cost_per_million: Option<f64>,
    pub(crate) cache_write_cost_per_million: Option<f64>,
    cost: CostRates,
    context_over_200k_cost: Option<CostRates>,
    cost_tiers: Vec<CostTier>,
}

impl OpenRouterCatalogModel {
    pub(crate) fn pricing_for_context(&self, context_tokens: u64) -> CostRates {
        self.cost_tiers
            .iter()
            .filter(|tier| tier.kind == "context" && context_tokens > tier.size)
            .max_by_key(|tier| tier.size)
            .map(|tier| tier.rates)
            .or_else(|| {
                (context_tokens > 200_000)
                    .then_some(self.context_over_200k_cost)
                    .flatten()
            })
            .unwrap_or(self.cost)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub(crate) struct CostRates {
    pub(crate) input: f64,
    pub(crate) output: f64,
    pub(crate) cache_read: f64,
    pub(crate) cache_write: f64,
}

impl CostRates {
    fn from_wire(cost: &CostRatesWire) -> Self {
        Self {
            input: clean_cost(cost.input),
            output: clean_cost(cost.output),
            cache_read: clean_cost(cost.cache_read),
            cache_write: clean_cost(cost.cache_write),
        }
    }
}

#[derive(Clone, Debug)]
struct CostTier {
    kind: String,
    size: u64,
    rates: CostRates,
}

#[derive(Debug, Deserialize)]
struct ProviderWire {
    #[serde(default)]
    id: String,
    #[serde(default)]
    models: BTreeMap<String, ModelWire>,
}

#[derive(Debug, Default, Deserialize)]
struct ModelWire {
    id: Option<String>,
    name: Option<String>,
    family: Option<String>,
    status: Option<String>,
    reasoning: Option<bool>,
    temperature: Option<bool>,
    tool_call: Option<bool>,
    #[serde(default)]
    limit: LimitWire,
    #[serde(default)]
    modalities: ModalitiesWire,
    #[serde(default)]
    cost: CostWire,
}

#[derive(Debug, Default, Deserialize)]
struct LimitWire {
    context: Option<u32>,
    input: Option<u32>,
    output: Option<u32>,
}

#[derive(Debug, Default, Deserialize)]
struct ModalitiesWire {
    #[serde(default)]
    input: Vec<String>,
    #[serde(default)]
    output: Vec<String>,
}

#[derive(Debug, Default, Deserialize)]
struct CostWire {
    #[serde(flatten)]
    rates: CostRatesWire,
    #[serde(default)]
    tiers: Vec<CostTierWire>,
    context_over_200k: Option<CostRatesWire>,
}

#[derive(Debug, Default, Deserialize)]
struct CostRatesWire {
    input: Option<f64>,
    output: Option<f64>,
    cache_read: Option<f64>,
    cache_write: Option<f64>,
}

#[derive(Debug, Deserialize)]
struct CostTierWire {
    #[serde(flatten)]
    rates: CostRatesWire,
    tier: TierWire,
}

#[derive(Debug, Deserialize)]
struct TierWire {
    #[serde(rename = "type")]
    kind: String,
    size: u64,
}

pub(crate) fn parse_openrouter_catalog(bytes: &[u8]) -> Result<OpenRouterCatalog, String> {
    parse_provider_catalog(bytes, OPENROUTER_PROVIDER_ID)
}

pub(crate) fn parse_ollama_cloud_catalog(bytes: &[u8]) -> Result<OpenRouterCatalog, String> {
    parse_provider_catalog(bytes, OLLAMA_CLOUD_PROVIDER_ID)
}

pub(crate) fn parse_lmstudio_catalog(bytes: &[u8]) -> Result<OpenRouterCatalog, String> {
    parse_provider_catalog(bytes, LMSTUDIO_PROVIDER_ID)
}

pub(crate) fn parse_deepseek_catalog(bytes: &[u8]) -> Result<OpenRouterCatalog, String> {
    parse_provider_catalog(bytes, DEEPSEEK_PROVIDER_ID)
}

pub(crate) fn parse_zai_catalog(bytes: &[u8]) -> Result<OpenRouterCatalog, String> {
    parse_provider_catalog(bytes, ZAI_PROVIDER_ID)
}

pub(crate) fn parse_provider_catalog(
    bytes: &[u8],
    provider_id: &str,
) -> Result<OpenRouterCatalog, String> {
    let snapshot: BTreeMap<String, ProviderWire> = serde_json::from_slice(bytes)
        .map_err(|error| format!("Models.dev catalog JSON was invalid: {error}"))?;
    let provider = snapshot
        .get(provider_id)
        .ok_or_else(|| format!("Models.dev catalog did not contain the {provider_id} provider."))?;
    normalize_provider(provider, provider_id)
}

fn normalize_provider(
    provider: &ProviderWire,
    provider_id: &str,
) -> Result<OpenRouterCatalog, String> {
    if provider.id != provider_id {
        return Err(format!(
            "Models.dev {provider_id} provider had unexpected id {:?}.",
            provider.id,
        ));
    }

    let mut models = provider
        .models
        .iter()
        .filter_map(|(key, model)| normalize_model(provider_id, key, model))
        .collect::<Vec<_>>();
    models.sort_by(|left, right| {
        left.definition
            .display_name
            .cmp(&right.definition.display_name)
    });
    Ok(OpenRouterCatalog { models })
}

fn normalize_model(
    provider_id: &str,
    key: &str,
    model: &ModelWire,
) -> Option<OpenRouterCatalogModel> {
    let id = model.id.as_deref()?.trim();
    if id.is_empty() || id != key {
        return None;
    }
    let status = model.status.as_deref().unwrap_or("active");
    if matches!(status, "alpha" | "deprecated") {
        return None;
    }
    let context_tokens = non_zero(model.limit.context)?;
    let output_tokens = non_zero(model.limit.output)?;
    if !contains_modality(&model.modalities.input, "text")
        || !contains_modality(&model.modalities.output, "text")
    {
        return None;
    }

    let mut request = RequestDefaults::default();
    request.generation = GenerationDefaults {
        temperature: model.temperature.unwrap_or(false).then_some(0.7),
        max_output_tokens: None,
        reasoning_effort: None,
    };

    let display_name = model
        .name
        .as_deref()
        .map(str::trim)
        .filter(|name| !name.is_empty())
        .unwrap_or(id)
        .to_string();
    let mut input = normalized_modalities(&model.modalities.input);
    let mut output = normalized_modalities(&model.modalities.output);
    if input.is_empty() {
        input.push("text".to_string());
    }
    if output.is_empty() {
        output.push("text".to_string());
    }

    let definition = ModelDefinition {
        id: ModelId::new(id).expect("validated Models.dev id is non-empty"),
        provider: ProviderId::unchecked(static_provider_id(provider_id)),
        display_name,
        adapter_model_id: adapter_model_id(provider_id, id),
        capabilities: Capabilities {
            tools: model.tool_call.unwrap_or(false),
            input,
            output,
            reasoning: model.reasoning.unwrap_or(false),
        },
        limits: Limits {
            context_tokens: Some(context_tokens),
            input_tokens: model.limit.input,
            output_tokens: Some(output_tokens),
        },
        request,
        enabled: true,
    };
    Some(OpenRouterCatalogModel {
        definition,
        family: model.family.clone(),
        status: status.to_string(),
        input_cost_per_million: model
            .cost
            .rates
            .input
            .filter(|value| value.is_finite() && *value >= 0.0),
        output_cost_per_million: model
            .cost
            .rates
            .output
            .filter(|value| value.is_finite() && *value >= 0.0),
        cache_read_cost_per_million: model
            .cost
            .rates
            .cache_read
            .filter(|value| value.is_finite() && *value >= 0.0),
        cache_write_cost_per_million: model
            .cost
            .rates
            .cache_write
            .filter(|value| value.is_finite() && *value >= 0.0),
        cost: CostRates::from_wire(&model.cost.rates),
        context_over_200k_cost: model
            .cost
            .context_over_200k
            .as_ref()
            .map(CostRates::from_wire),
        cost_tiers: model
            .cost
            .tiers
            .iter()
            .map(|tier| CostTier {
                kind: tier.tier.kind.clone(),
                size: tier.tier.size,
                rates: CostRates::from_wire(&tier.rates),
            })
            .collect(),
    })
}

fn static_provider_id(provider_id: &str) -> &'static str {
    match provider_id {
        ProviderId::OLLAMA_CLOUD => ProviderId::OLLAMA_CLOUD,
        ProviderId::LMSTUDIO => ProviderId::LMSTUDIO,
        ProviderId::DEEPSEEK => ProviderId::DEEPSEEK,
        ProviderId::ZAI => ProviderId::ZAI,
        _ => ProviderId::OPENROUTER,
    }
}

fn adapter_model_id(provider_id: &str, id: &str) -> String {
    match provider_id {
        ProviderId::OLLAMA_CLOUD
        | ProviderId::LMSTUDIO
        | ProviderId::DEEPSEEK
        | ProviderId::ZAI => id.to_string(),
        _ => openrouter::adapter_model_id(id),
    }
}

fn clean_cost(value: Option<f64>) -> f64 {
    value
        .filter(|value| value.is_finite() && *value >= 0.0)
        .unwrap_or(0.0)
}

fn non_zero(value: Option<u32>) -> Option<u32> {
    value.filter(|value| *value > 0)
}

fn contains_modality(values: &[String], needle: &str) -> bool {
    values
        .iter()
        .any(|value| value.trim().eq_ignore_ascii_case(needle))
}

fn normalized_modalities(values: &[String]) -> Vec<String> {
    let mut normalized = values
        .iter()
        .map(|value| value.trim().to_ascii_lowercase())
        .filter(|value| !value.is_empty())
        .collect::<Vec<_>>();
    normalized.sort();
    normalized.dedup();
    normalized
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn filters_to_safe_openrouter_chat_models() {
        let raw = br#"{
            "openrouter": {
                "id": "openrouter",
                "models": {
                    "ok/model": {
                        "id": "ok/model",
                        "name": "OK Model",
                        "tool_call": false,
                        "reasoning": true,
                        "temperature": false,
                        "limit": { "context": 100000, "output": 4096 },
                        "modalities": { "input": ["text", "image"], "output": ["text"] }
                    },
                    "old/model": {
                        "id": "old/model",
                        "status": "deprecated",
                        "tool_call": true,
                        "limit": { "context": 100000, "output": 4096 },
                        "modalities": { "input": ["text"], "output": ["text"] }
                    },
                    "alpha/model": {
                        "id": "alpha/model",
                        "status": "alpha",
                        "tool_call": true,
                        "limit": { "context": 100000, "output": 4096 },
                        "modalities": { "input": ["text"], "output": ["text"] }
                    },
                    "missing/limits": {
                        "id": "missing/limits",
                        "tool_call": true,
                        "modalities": { "input": ["text"], "output": ["text"] }
                    },
                    "image/out": {
                        "id": "image/out",
                        "tool_call": true,
                        "limit": { "context": 100000, "output": 4096 },
                        "modalities": { "input": ["image"], "output": ["text"] }
                    }
                }
            },
            "other": { "id": "other", "models": {} }
        }"#;

        let catalog = parse_openrouter_catalog(raw).unwrap();

        assert_eq!(catalog.models.len(), 1);
        let model = &catalog.models[0].definition;
        assert_eq!(model.id.as_str(), "ok/model");
        assert_eq!(model.display_name, "OK Model");
        assert!(!model.capabilities.tools);
        assert!(model.capabilities.reasoning);
        assert!(
            model
                .capabilities
                .input
                .iter()
                .any(|value| value == "image")
        );
        assert_eq!(model.request.generation.temperature, None);
        assert_eq!(model.limits.context_tokens, Some(100000));
        assert_eq!(model.limits.output_tokens, Some(4096));
    }

    #[test]
    fn keeps_beta_and_uses_id_when_name_is_missing() {
        let raw = br#"{
            "openrouter": {
                "id": "openrouter",
                "models": {
                    "beta/model": {
                        "id": "beta/model",
                        "status": "beta",
                        "tool_call": true,
                        "temperature": true,
                        "limit": { "context": 100000, "output": 4096 },
                        "modalities": { "input": ["text"], "output": ["text"] }
                    }
                }
            }
        }"#;

        let catalog = parse_openrouter_catalog(raw).unwrap();

        assert_eq!(catalog.models.len(), 1);
        assert_eq!(catalog.models[0].definition.display_name, "beta/model");
        assert_eq!(
            catalog.models[0].definition.request.generation.temperature,
            Some(0.7)
        );
    }

    #[test]
    fn parses_zai_models_with_raw_adapter_ids() {
        let raw = br#"{
            "zai": {
                "id": "zai",
                "models": {
                    "glm-5.2": {
                        "id": "glm-5.2",
                        "name": "GLM 5.2",
                        "tool_call": true,
                        "temperature": true,
                        "limit": { "context": 200000, "output": 32768 },
                        "modalities": { "input": ["text"], "output": ["text"] },
                        "cost": { "input": 0.5, "output": 1.5 }
                    }
                }
            }
        }"#;

        let catalog = parse_zai_catalog(raw).unwrap();
        let model = &catalog.models[0].definition;

        assert_eq!(model.provider.as_str(), ProviderId::ZAI);
        assert_eq!(model.id.as_str(), "glm-5.2");
        assert_eq!(model.adapter_model_id, "glm-5.2");
        assert!(model.capabilities.tools);
    }

    #[test]
    fn parses_lmstudio_models_with_raw_adapter_ids() {
        let raw = br#"{
            "lmstudio": {
                "id": "lmstudio",
                "models": {
                    "openai/gpt-oss-20b": {
                        "id": "openai/gpt-oss-20b",
                        "name": "GPT OSS 20B",
                        "tool_call": true,
                        "reasoning": true,
                        "temperature": true,
                        "limit": { "context": 131072, "output": 32768 },
                        "modalities": { "input": ["text"], "output": ["text"] },
                        "cost": { "input": 0, "output": 0 }
                    }
                }
            }
        }"#;

        let catalog = parse_lmstudio_catalog(raw).unwrap();
        let model = &catalog.models[0].definition;

        assert_eq!(model.provider.as_str(), ProviderId::LMSTUDIO);
        assert_eq!(model.id.as_str(), "openai/gpt-oss-20b");
        assert_eq!(model.adapter_model_id, "openai/gpt-oss-20b");
        assert!(model.capabilities.reasoning);
    }

    #[test]
    fn parses_deepseek_models_with_raw_adapter_ids() {
        let raw = br#"{
            "deepseek": {
                "id": "deepseek",
                "models": {
                    "deepseek-chat": {
                        "id": "deepseek-chat",
                        "name": "DeepSeek Chat",
                        "tool_call": true,
                        "reasoning": false,
                        "temperature": true,
                        "limit": { "context": 64000, "output": 8192 },
                        "modalities": { "input": ["text"], "output": ["text"] },
                        "cost": { "input": 0.27, "output": 1.1 }
                    }
                }
            }
        }"#;

        let catalog = parse_deepseek_catalog(raw).unwrap();
        let model = &catalog.models[0].definition;

        assert_eq!(model.provider.as_str(), ProviderId::DEEPSEEK);
        assert_eq!(model.id.as_str(), "deepseek-chat");
        assert_eq!(model.adapter_model_id, "deepseek-chat");
        assert!(model.capabilities.tools);
    }

    #[test]
    fn selects_context_tier_pricing_before_over_200k_pricing() {
        let raw = br#"{
            "openrouter": {
                "id": "openrouter",
                "models": {
                    "priced/model": {
                        "id": "priced/model",
                        "tool_call": true,
                        "limit": { "context": 300000, "output": 4096 },
                        "modalities": { "input": ["text"], "output": ["text"] },
                        "cost": {
                            "input": 1,
                            "output": 2,
                            "cache_read": 0.1,
                            "cache_write": 0.5,
                            "context_over_200k": {
                                "input": 3,
                                "output": 4,
                                "cache_read": 0.3,
                                "cache_write": 0.7
                            },
                            "tiers": [
                                {
                                    "input": 5,
                                    "output": 6,
                                    "cache_read": 0.4,
                                    "cache_write": 0.8,
                                    "tier": { "type": "context", "size": 200000 }
                                }
                            ]
                        }
                    }
                }
            }
        }"#;

        let catalog = parse_openrouter_catalog(raw).unwrap();
        let model = catalog.model("priced/model").unwrap();

        assert_eq!(
            model.pricing_for_context(250_000),
            CostRates {
                input: 5.0,
                output: 6.0,
                cache_read: 0.4,
                cache_write: 0.8
            }
        );
        assert_eq!(
            model.pricing_for_context(150_000),
            CostRates {
                input: 1.0,
                output: 2.0,
                cache_read: 0.1,
                cache_write: 0.5
            }
        );
    }
}
