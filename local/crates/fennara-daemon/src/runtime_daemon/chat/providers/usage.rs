use serde_json::{Map, Value, json};

#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub(crate) struct NormalizedUsage {
    pub(crate) input_tokens: u64,
    pub(crate) output_tokens: u64,
    pub(crate) total_tokens: u64,
    pub(crate) reasoning_tokens: u64,
    pub(crate) cache_read_tokens: u64,
    pub(crate) cache_write_tokens: u64,
    pub(crate) provider_reported: bool,
}

impl NormalizedUsage {
    pub(crate) fn from_provider_value(value: Option<&Value>) -> Self {
        let provider_reported = value.is_some_and(Value::is_object);
        let value = value.unwrap_or(&Value::Null);
        let has_inclusive_prompt_tokens = first_u64(
            value,
            &[
                &["prompt_tokens"],
                &["promptTokens"],
                &["usage", "prompt_tokens"],
            ],
        )
        .is_some();
        let mut input_tokens = first_u64(
            value,
            &[
                &["prompt_tokens"],
                &["promptTokens"],
                &["input_tokens"],
                &["inputTokens"],
                &["usage", "input_tokens"],
                &["usage", "inputTokens"],
            ],
        )
        .unwrap_or_default();
        let output_tokens = first_u64(
            value,
            &[
                &["completion_tokens"],
                &["output_tokens"],
                &["outputTokens"],
                &["usage", "output_tokens"],
                &["usage", "outputTokens"],
            ],
        )
        .unwrap_or_default();
        let reasoning_tokens = first_u64(
            value,
            &[
                &["reasoning_tokens"],
                &["reasoningTokens"],
                &["completion_tokens_details", "reasoning_tokens"],
                &["output_tokens_details", "reasoning_tokens"],
                &["outputTokenDetails", "reasoningTokens"],
                &["usage", "reasoning_tokens"],
                &["usage", "reasoningTokens"],
            ],
        )
        .unwrap_or_default();
        let cache_read_tokens = first_u64(
            value,
            &[
                &["cache_read_tokens"],
                &["cache_read_input_tokens"],
                &["cacheReadTokens"],
                &["cacheReadInputTokens"],
                &["cached_tokens"],
                &["cachedTokens"],
                &["cachedInputTokens"],
                &["prompt_tokens_details", "cached_tokens"],
                &["input_tokens_details", "cached_tokens"],
                &["input_tokens_details", "cache_read_tokens"],
                &["inputTokenDetails", "cacheReadTokens"],
                &["usage", "cacheReadInputTokens"],
            ],
        )
        .unwrap_or_default();
        let cache_write_tokens = first_u64(
            value,
            &[
                &["cache_write_tokens"],
                &["cache_creation_input_tokens"],
                &["cacheWriteTokens"],
                &["cacheWriteInputTokens"],
                &["input_tokens_details", "cache_write_tokens"],
                &["inputTokenDetails", "cacheWriteTokens"],
                &["usage", "cacheWriteInputTokens"],
            ],
        )
        .unwrap_or_default();
        if !has_inclusive_prompt_tokens && (cache_read_tokens > 0 || cache_write_tokens > 0) {
            input_tokens = input_tokens
                .saturating_add(cache_read_tokens)
                .saturating_add(cache_write_tokens);
        }
        let total_tokens = first_u64(
            value,
            &[
                &["total_tokens"],
                &["totalTokens"],
                &["usage", "total_tokens"],
                &["usage", "totalTokens"],
            ],
        )
        .unwrap_or_else(|| input_tokens.saturating_add(output_tokens));

        Self {
            input_tokens,
            output_tokens,
            total_tokens,
            reasoning_tokens,
            cache_read_tokens,
            cache_write_tokens,
            provider_reported,
        }
    }

    pub(crate) fn non_cached_input_tokens(&self) -> u64 {
        self.input_tokens
            .saturating_sub(self.cache_read_tokens)
            .saturating_sub(self.cache_write_tokens)
    }

    pub(crate) fn visible_output_tokens(&self) -> u64 {
        self.output_tokens.saturating_sub(self.reasoning_tokens)
    }

    pub(crate) fn to_value(&self, raw: Option<&Value>) -> Value {
        let mut object = raw
            .and_then(Value::as_object)
            .cloned()
            .unwrap_or_else(Map::new);
        object.insert("prompt_tokens".to_string(), json!(self.input_tokens));
        object.insert("input_tokens".to_string(), json!(self.input_tokens));
        object.insert(
            "non_cached_input_tokens".to_string(),
            json!(self.non_cached_input_tokens()),
        );
        object.insert(
            "completion_tokens".to_string(),
            json!(self.visible_output_tokens()),
        );
        object.insert(
            "output_tokens".to_string(),
            json!(self.visible_output_tokens()),
        );
        object.insert("total_tokens".to_string(), json!(self.total_tokens));
        object.insert("reasoning_tokens".to_string(), json!(self.reasoning_tokens));
        object.insert("cached_tokens".to_string(), json!(self.cache_read_tokens));
        object.insert(
            "cache_read_tokens".to_string(),
            json!(self.cache_read_tokens),
        );
        object.insert(
            "cache_write_tokens".to_string(),
            json!(self.cache_write_tokens),
        );
        object.insert(
            "provider_usage_reported".to_string(),
            json!(self.provider_reported),
        );
        Value::Object(object)
    }
}

fn first_u64(value: &Value, paths: &[&[&str]]) -> Option<u64> {
    paths.iter().find_map(|path| u64_at(value, path))
}

fn u64_at(value: &Value, path: &[&str]) -> Option<u64> {
    let mut cursor = value;
    for segment in path {
        cursor = cursor.get(*segment)?;
    }
    if let Some(value) = cursor.as_u64() {
        return Some(value);
    }
    cursor.as_f64().and_then(|value| {
        if value.is_finite() && value >= 0.0 {
            Some(value.round() as u64)
        } else {
            None
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalizes_openai_style_cached_usage() {
        let usage = NormalizedUsage::from_provider_value(Some(&json!({
            "prompt_tokens": 100,
            "completion_tokens": 20,
            "completion_tokens_details": { "reasoning_tokens": 4 },
            "prompt_tokens_details": { "cached_tokens": 60 }
        })));

        assert_eq!(usage.input_tokens, 100);
        assert_eq!(usage.visible_output_tokens(), 16);
        assert_eq!(usage.reasoning_tokens, 4);
        assert_eq!(usage.cache_read_tokens, 60);
        assert_eq!(usage.non_cached_input_tokens(), 40);
    }

    #[test]
    fn normalizes_anthropic_cache_write_usage() {
        let usage = NormalizedUsage::from_provider_value(Some(&json!({
            "input_tokens": 25,
            "output_tokens": 5,
            "cache_creation_input_tokens": 100,
            "cache_read_input_tokens": 50
        })));

        assert_eq!(usage.input_tokens, 175);
        assert_eq!(usage.cache_write_tokens, 100);
        assert_eq!(usage.cache_read_tokens, 50);
        assert_eq!(usage.non_cached_input_tokens(), 25);
    }
}
