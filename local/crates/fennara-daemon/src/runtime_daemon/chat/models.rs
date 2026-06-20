use futures_util::future::join_all;
use reqwest::Client;
use serde::Serialize;
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::time::Duration;

use super::settings::{self, ChatSettings};

const OPENROUTER_MODELS_URL: &str = "https://openrouter.ai/api/v1/models";
const OPENROUTER_MODEL_URL: &str = "https://openrouter.ai/api/v1/models";
const ENDPOINT_METADATA_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Clone, Debug)]
struct EndpointMetadata {
    context_length: Option<u64>,
    input_cost_per_million: Option<f64>,
    output_cost_per_million: Option<f64>,
    tokens_per_second: Option<f64>,
    max_output_tokens: Option<u64>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ModelCatalog {
    pub(crate) models: Vec<ModelInfo>,
    pub(crate) recommended_ids: Vec<&'static str>,
    pub(crate) custom_ids: Vec<String>,
    pub(crate) live: bool,
    pub(crate) error: Option<String>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ModelInfo {
    pub(crate) id: String,
    pub(crate) display_name: String,
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
    let ids = recommended_ids
        .iter()
        .map(|id| id.to_string())
        .chain(custom_ids.iter().cloned())
        .collect::<Vec<_>>();

    match fetch_openrouter_models(settings.openrouter_api_key.as_deref()).await {
        Ok(openrouter_models) => {
            let client = Client::new();
            let by_id = openrouter_models
                .iter()
                .filter_map(|model| {
                    model
                        .get("id")
                        .and_then(Value::as_str)
                        .map(|id| (id, model))
                })
                .collect::<HashMap<_, _>>();
            let mut seen = HashSet::new();
            let mut model_requests = Vec::new();
            for id in ids {
                if !seen.insert(id.clone()) {
                    continue;
                }
                let raw = by_id.get(id.as_str()).copied().cloned();
                let api_key = settings.openrouter_api_key.clone();
                let client = client.clone();
                model_requests.push(async move {
                    let endpoint_metadata = tokio::time::timeout(
                        ENDPOINT_METADATA_TIMEOUT,
                        fetch_endpoint_metadata(&client, &id, api_key.as_deref()),
                    )
                    .await
                    .ok()
                    .flatten();
                    (id, raw, endpoint_metadata)
                });
            }
            let models = join_all(model_requests)
                .await
                .into_iter()
                .map(|(id, raw, endpoint_metadata)| {
                    model_info(
                        &id,
                        raw.as_ref(),
                        endpoint_metadata.as_ref(),
                        recommended_ids.contains(&id.as_str()),
                        custom_ids.iter().any(|custom| custom == &id),
                    )
                })
                .collect();
            ModelCatalog {
                models,
                recommended_ids,
                custom_ids,
                live: true,
                error: None,
            }
        }
        Err(error) => {
            let mut seen = HashSet::new();
            let mut models = Vec::new();
            for id in ids {
                if !seen.insert(id.clone()) {
                    continue;
                }
                models.push(model_info(
                    &id,
                    None,
                    None,
                    recommended_ids.contains(&id.as_str()),
                    custom_ids.iter().any(|custom| custom == &id),
                ));
            }
            ModelCatalog {
                models,
                recommended_ids,
                custom_ids,
                live: false,
                error: Some(error),
            }
        }
    }
}

async fn fetch_openrouter_models(api_key: Option<&str>) -> Result<Vec<Value>, String> {
    let client = Client::new();
    let mut request = client.get(OPENROUTER_MODELS_URL);
    if let Some(api_key) = api_key.filter(|key| !key.trim().is_empty()) {
        request = request.bearer_auth(api_key.trim());
    }
    let response = request
        .send()
        .await
        .map_err(|error| format!("Failed to fetch OpenRouter models: {error}"))?;
    if !response.status().is_success() {
        return Err(format!(
            "OpenRouter models request failed: {}",
            response.status()
        ));
    }
    let body: Value = response
        .json()
        .await
        .map_err(|error| format!("OpenRouter models response was invalid: {error}"))?;
    Ok(body
        .get("data")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default())
}

async fn fetch_endpoint_metadata(
    client: &Client,
    model_id: &str,
    api_key: Option<&str>,
) -> Option<EndpointMetadata> {
    let mut request = client.get(model_endpoint_url(model_id));
    if let Some(api_key) = api_key.filter(|key| !key.trim().is_empty()) {
        request = request.bearer_auth(api_key.trim());
    }
    let response = request.send().await.ok()?;
    if !response.status().is_success() {
        return None;
    }
    let body = response.json::<Value>().await.ok()?;
    let endpoints = body
        .get("data")
        .and_then(|data| data.get("endpoints"))
        .and_then(Value::as_array)?;
    let active = endpoints
        .iter()
        .filter(|endpoint| endpoint.get("status").and_then(Value::as_i64).unwrap_or(0) == 0)
        .collect::<Vec<_>>();
    let mut candidates = if active.is_empty() {
        endpoints.iter().collect::<Vec<_>>()
    } else {
        active
    };
    candidates.sort_by(|a, b| {
        let a_tps = throughput_tokens_per_second(a.get("throughput_last_30m")).unwrap_or(-1.0);
        let b_tps = throughput_tokens_per_second(b.get("throughput_last_30m")).unwrap_or(-1.0);
        b_tps
            .partial_cmp(&a_tps)
            .unwrap_or(std::cmp::Ordering::Equal)
    });
    let endpoint = candidates.first().copied()?;
    Some(EndpointMetadata {
        context_length: endpoint.get("context_length").and_then(Value::as_u64),
        input_cost_per_million: endpoint
            .get("pricing")
            .and_then(|pricing| pricing.get("prompt"))
            .and_then(price_per_million),
        output_cost_per_million: endpoint
            .get("pricing")
            .and_then(|pricing| pricing.get("completion"))
            .and_then(price_per_million),
        tokens_per_second: average_top_throughput(&candidates),
        max_output_tokens: endpoint
            .get("max_completion_tokens")
            .and_then(Value::as_u64),
    })
}

fn model_endpoint_url(model_id: &str) -> String {
    let encoded = model_id
        .split('/')
        .map(encode_url_component)
        .collect::<Vec<_>>()
        .join("/");
    format!("{OPENROUTER_MODEL_URL}/{encoded}/endpoints")
}

fn model_info(
    id: &str,
    raw: Option<&Value>,
    endpoint: Option<&EndpointMetadata>,
    recommended: bool,
    custom: bool,
) -> ModelInfo {
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
        context_length: endpoint
            .and_then(|metadata| metadata.context_length)
            .or_else(|| {
                raw.and_then(|model| model.get("context_length"))
                    .and_then(Value::as_u64)
            }),
        max_output_tokens: endpoint
            .and_then(|metadata| metadata.max_output_tokens)
            .or_else(|| {
                raw.and_then(|model| model.get("top_provider"))
                    .and_then(|provider| provider.get("max_completion_tokens"))
                    .and_then(Value::as_u64)
            }),
        input_cost_per_million: endpoint
            .and_then(|metadata| metadata.input_cost_per_million)
            .or_else(|| {
                raw.and_then(|model| model.get("pricing"))
                    .and_then(|pricing| pricing.get("prompt"))
                    .and_then(price_per_million)
            }),
        output_cost_per_million: endpoint
            .and_then(|metadata| metadata.output_cost_per_million)
            .or_else(|| {
                raw.and_then(|model| model.get("pricing"))
                    .and_then(|pricing| pricing.get("completion"))
                    .and_then(price_per_million)
            }),
        tokens_per_second: endpoint
            .and_then(|metadata| metadata.tokens_per_second)
            .or_else(|| {
                raw.and_then(|model| model.get("top_provider"))
                    .and_then(|provider| provider.get("throughput"))
                    .or_else(|| raw.and_then(|model| model.get("throughput")))
                    .and_then(Value::as_f64)
            }),
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

fn throughput_tokens_per_second(value: Option<&Value>) -> Option<f64> {
    let value = value?;
    if let Some(object) = value.as_object() {
        for key in ["p50", "p75", "p90", "p99"] {
            if let Some(parsed) = number_value(object.get(key)) {
                return Some(parsed);
            }
        }
        return None;
    }
    number_value(Some(value))
}

fn average_top_throughput(endpoints: &[&Value]) -> Option<f64> {
    let mut values = endpoints
        .iter()
        .filter_map(|endpoint| throughput_tokens_per_second(endpoint.get("throughput_last_30m")))
        .collect::<Vec<_>>();
    values.sort_by(|a, b| b.partial_cmp(a).unwrap_or(std::cmp::Ordering::Equal));
    values.truncate(3);
    if values.is_empty() {
        return None;
    }
    let total = values.iter().sum::<f64>();
    Some(((total / values.len() as f64) * 10.0).round() / 10.0)
}

fn number_value(value: Option<&Value>) -> Option<f64> {
    let value = value?;
    value
        .as_f64()
        .or_else(|| value.as_str().and_then(|raw| raw.parse::<f64>().ok()))
        .filter(|number| number.is_finite())
}

fn encode_url_component(value: &str) -> String {
    let mut encoded = String::new();
    for byte in value.bytes() {
        let keep = byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b'~');
        if keep {
            encoded.push(byte as char);
        } else {
            encoded.push_str(&format!("%{byte:02X}"));
        }
    }
    encoded
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
