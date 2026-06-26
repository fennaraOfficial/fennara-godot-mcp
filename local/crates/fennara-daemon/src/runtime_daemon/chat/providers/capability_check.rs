use serde_json::Value;

use super::error::LlmError;
use super::request::LlmRequest;

pub(crate) fn preflight(request: &LlmRequest) -> Result<(), LlmError> {
    let provider = request.model.provider.id.to_string();
    let model = request.model.model.id.to_string();
    let capabilities = &request.model.model.capabilities;

    if !capabilities.output.iter().any(|value| value == "text") {
        return Err(LlmError::ProviderApi {
            provider,
            status: None,
            message: format!("{model} does not advertise text output."),
            retryable: false,
        });
    }
    if !request.tools.is_empty() && !capabilities.tools {
        return Err(LlmError::ProviderApi {
            provider,
            status: None,
            message: format!("{model} does not support tool calling."),
            retryable: false,
        });
    }
    if messages_include_images(&request.messages)
        && !capabilities.input.iter().any(|value| value == "image")
    {
        return Err(LlmError::ProviderApi {
            provider,
            status: None,
            message: format!("{model} does not advertise image input."),
            retryable: false,
        });
    }
    Ok(())
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::runtime_daemon::chat::providers::catalog::Catalog;
    use crate::runtime_daemon::chat::providers::request::LlmRequest;
    use crate::runtime_daemon::chat::providers::types::ProviderSettings;
    use serde_json::json;

    fn request_for_model(model_id: &str) -> LlmRequest {
        let settings = ProviderSettings {
            openrouter_api_key: Some("test".to_string()),
            ollama_cloud_api_key: None,
            lmstudio_api_key: None,
            deepseek_api_key: None,
            zai_api_key: None,
            ollama_base_url: "http://127.0.0.1:11434".to_string(),
            lmstudio_base_url: "http://127.0.0.1:1234/v1".to_string(),
            custom_models: Vec::new(),
        };
        let catalog = Catalog::from_settings(&settings);
        let model_ref = crate::runtime_daemon::chat::providers::catalog::model_ref_from_selection(
            model_id, &catalog,
        )
        .unwrap();
        let resolved = catalog.resolve(&model_ref).unwrap();
        LlmRequest {
            model: resolved,
            messages: vec![json!({ "role": "user", "content": "hello" })],
            tools: Vec::new(),
        }
    }

    #[test]
    fn tools_are_rejected_when_model_cannot_call_tools() {
        let mut request = request_for_model("openrouter/google/gemini-3.5-flash");
        request.model.model.capabilities.tools = false;
        request.tools = vec![json!({ "type": "function", "function": { "name": "run" } })];

        assert!(matches!(
            preflight(&request),
            Err(LlmError::ProviderApi { .. })
        ));
    }

    #[test]
    fn images_are_rejected_when_model_lacks_image_input() {
        let mut request = request_for_model("openrouter/google/gemini-3.5-flash");
        request.model.model.capabilities.input = vec!["text".to_string()];
        request.messages = vec![json!({
            "role": "user",
            "content": [
                { "type": "text", "text": "look" },
                { "type": "image_url", "image_url": { "url": "data:image/png;base64,abc" } }
            ]
        })];

        assert!(matches!(
            preflight(&request),
            Err(LlmError::ProviderApi { .. })
        ));
    }

    #[test]
    fn historical_image_placeholders_do_not_require_image_input() {
        let mut request = request_for_model("openrouter/google/gemini-3.5-flash");
        request.model.model.capabilities.input = vec!["text".to_string()];
        request.messages = vec![json!({
            "role": "user",
            "content": [
                { "type": "text", "text": "previous image" },
                { "type": "text", "text": "[Attached image/png: old.png]" }
            ]
        })];

        preflight(&request).unwrap();
    }
}
