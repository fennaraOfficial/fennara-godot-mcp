use super::error::LlmError;
use super::request::LlmRequest;
use super::types::Limits;

const TOKEN_CHAR_APPROX: usize = 4;
const DEFAULT_RESERVED_BUFFER: u32 = 2_000;

#[allow(dead_code)]
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum ContextDecision {
    Send {
        estimated_input_tokens: u32,
        usable_input_tokens: Option<u32>,
    },
    TooLarge {
        estimated_input_tokens: u32,
        usable_input_tokens: u32,
    },
}

pub(crate) fn preflight(request: &LlmRequest) -> Result<ContextDecision, LlmError> {
    let estimated = estimate_request_tokens(request);
    let usable = usable_input_tokens(
        &request.model.model.limits,
        request.model.request.generation.max_output_tokens,
    );
    let Some(usable) = usable else {
        return Ok(ContextDecision::Send {
            estimated_input_tokens: estimated,
            usable_input_tokens: None,
        });
    };
    if estimated > usable {
        return Err(LlmError::ContextOverflow {
            provider: request.model.provider.id.to_string(),
            message: format!(
                "This chat is estimated at {estimated} input tokens, which exceeds the selected model's usable input budget of {usable} tokens."
            ),
        });
    }
    Ok(ContextDecision::Send {
        estimated_input_tokens: estimated,
        usable_input_tokens: Some(usable),
    })
}

pub(crate) fn estimate_request_tokens(request: &LlmRequest) -> u32 {
    let message_chars = serde_json::to_string(&request.messages)
        .map(|value| value.len())
        .unwrap_or_default();
    let tool_chars = serde_json::to_string(&request.tools)
        .map(|value| value.len())
        .unwrap_or_default();
    ((message_chars + tool_chars).max(1) / TOKEN_CHAR_APPROX)
        .max(1)
        .min(u32::MAX as usize) as u32
}

fn usable_input_tokens(limits: &Limits, requested_output: Option<u32>) -> Option<u32> {
    let context = limits.context_tokens?;
    let output = requested_output.unwrap_or(DEFAULT_RESERVED_BUFFER);
    let reserved = DEFAULT_RESERVED_BUFFER.max(output);
    if let Some(input) = limits.input_tokens {
        return Some(input.saturating_sub(reserved));
    }
    Some(context.saturating_sub(reserved))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::runtime_daemon::chat::providers::catalog::Catalog;
    use crate::runtime_daemon::chat::providers::request::LlmRequest;
    use crate::runtime_daemon::chat::providers::types::{
        ChatRequest, ProviderSettings, RequestDefaults,
    };
    use serde_json::json;

    #[test]
    fn known_limits_can_block_request_before_provider_call() {
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
        let catalog = Catalog::from_settings(&settings);
        let model_ref = super::super::catalog::model_ref_from_selection(
            "openrouter/google/gemini-3.5-flash",
            &catalog,
        )
        .unwrap();
        let mut resolved = catalog.resolve(&model_ref).unwrap();
        resolved.model.limits.context_tokens = Some(100);
        resolved.model.limits.output_tokens = Some(50);
        resolved.request = RequestDefaults::default();
        let request = LlmRequest {
            model: resolved,
            messages: vec![json!({ "role": "user", "content": "x".repeat(2000) })],
            tools: Vec::new(),
        };

        assert!(matches!(
            preflight(&request),
            Err(LlmError::ContextOverflow { .. })
        ));
    }

    #[test]
    fn unknown_limits_allow_provider_to_decide() {
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
        let request = LlmRequest::from_chat(
            &settings,
            &ChatRequest {
                model: "ollama/llama3.1:8b".to_string(),
                reasoning_effort: "medium".to_string(),
                messages: vec![json!({ "role": "user", "content": "hello" })],
                tools: Vec::new(),
            },
        )
        .unwrap();

        assert!(matches!(
            preflight(&request).unwrap(),
            ContextDecision::Send {
                usable_input_tokens: None,
                ..
            }
        ));
    }

    #[test]
    fn catalog_output_limit_does_not_reserve_the_whole_context() {
        let limits = Limits {
            context_tokens: Some(1_048_576),
            input_tokens: None,
            output_tokens: Some(1_048_576),
        };

        assert_eq!(
            usable_input_tokens(&limits, None),
            Some(1_048_576 - DEFAULT_RESERVED_BUFFER)
        );
    }

    #[test]
    fn historical_image_placeholders_do_not_inflate_context_estimate() {
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
        let catalog = Catalog::from_settings(&settings);
        let model_ref = super::super::catalog::model_ref_from_selection(
            "openrouter/google/gemini-3.5-flash",
            &catalog,
        )
        .unwrap();
        let mut resolved = catalog.resolve(&model_ref).unwrap();
        resolved.request = RequestDefaults::default();
        let placeholder_request = LlmRequest {
            model: resolved.clone(),
            messages: vec![json!({
                "role": "user",
                "content": [
                    { "type": "text", "text": "old screenshot" },
                    { "type": "text", "text": "[Attached image/png: old.png]" }
                ]
            })],
            tools: Vec::new(),
        };
        let base64_request = LlmRequest {
            model: resolved,
            messages: vec![json!({
                "role": "user",
                "content": [
                    { "type": "text", "text": "old screenshot" },
                    {
                        "type": "image_url",
                        "image_url": {
                            "url": format!("data:image/png;base64,{}", "a".repeat(320_000))
                        }
                    }
                ]
            })],
            tools: Vec::new(),
        };

        assert!(estimate_request_tokens(&placeholder_request) < 100);
        assert!(estimate_request_tokens(&base64_request) > 50_000);
    }
}
