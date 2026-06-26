use serde_json::{Value, json};

use super::super::providers::{ProviderSettings, pricing_for_model, usage::NormalizedUsage};

const TOKENS_PER_MILLION: f64 = 1_000_000.0;

pub(super) fn usage_for_model(
    settings: &ProviderSettings,
    model: &str,
    raw_usage: Option<&Value>,
) -> Value {
    let normalized = NormalizedUsage::from_provider_value(raw_usage);
    let mut usage = normalized.to_value(raw_usage);
    let cost = calculated_cost(settings, model, &normalized);
    let upstream_cost = raw_usage.and_then(provider_cost);

    if let Some(object) = usage.as_object_mut() {
        object
            .entry("model".to_string())
            .or_insert_with(|| json!(model));
        object.insert("cost".to_string(), json!(cost));
        object.insert("cost_estimated".to_string(), json!(true));
        object.insert(
            "cost_source".to_string(),
            json!("catalog_pricing_provider_usage"),
        );
        if let Some(upstream_cost) = upstream_cost {
            object.insert("upstream_cost".to_string(), json!(upstream_cost));
        }
    }
    usage
}

fn calculated_cost(settings: &ProviderSettings, model: &str, usage: &NormalizedUsage) -> f64 {
    let Some(pricing) = pricing_for_model(settings, model, usage.input_tokens) else {
        return 0.0;
    };
    let fresh_input = usage.non_cached_input_tokens() as f64;
    let visible_output = usage.visible_output_tokens() as f64;
    let reasoning = usage.reasoning_tokens as f64;
    let cache_read = usage.cache_read_tokens as f64;
    let cache_write = usage.cache_write_tokens as f64;

    [
        fresh_input * pricing.input,
        visible_output * pricing.output,
        reasoning * pricing.output,
        cache_read * pricing.cache_read,
        cache_write * pricing.cache_write,
    ]
    .into_iter()
    .sum::<f64>()
        / TOKENS_PER_MILLION
}

fn provider_cost(usage: &Value) -> Option<f64> {
    usage
        .get("cost")
        .or_else(|| usage.get("total_cost"))
        .or_else(|| usage.get("totalCost"))
        .and_then(Value::as_f64)
        .filter(|value| value.is_finite() && *value >= 0.0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::runtime_daemon::chat::providers::ProviderSettings;

    #[test]
    fn preserves_upstream_cost_as_audit_field() {
        let settings = ProviderSettings {
            openrouter_api_key: None,
            ollama_cloud_api_key: None,
            lmstudio_api_key: None,
            deepseek_api_key: None,
            zai_api_key: None,
            ollama_base_url: "http://127.0.0.1:11434".to_string(),
            lmstudio_base_url: "http://127.0.0.1:1234/v1".to_string(),
            custom_models: Vec::new(),
        };

        let usage = usage_for_model(
            &settings,
            "ollama/llama3.1",
            Some(&json!({
                "prompt_tokens": 100,
                "completion_tokens": 10,
                "cost": 9.0
            })),
        );

        assert_eq!(usage["cost"], json!(0.0));
        assert_eq!(usage["upstream_cost"], json!(9.0));
        assert_eq!(usage["cost_estimated"], json!(true));
    }
}
