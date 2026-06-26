use serde_json::{Value, json};

use super::error::LlmError;
use super::resolver;
use super::types::{ChatRequest, ProviderSettings, ResolvedModel};

const SYSTEM_PROMPT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../prompts/plugin_chat_system.md"
));

#[derive(Clone, Debug)]
pub(crate) struct LlmRequest {
    pub(crate) model: ResolvedModel,
    pub(crate) messages: Vec<Value>,
    pub(crate) tools: Vec<Value>,
}

impl LlmRequest {
    pub(crate) fn from_chat(
        settings: &ProviderSettings,
        request: &ChatRequest,
    ) -> Result<Self, LlmError> {
        let mut model = resolver::resolve_request_model(settings, request)?;
        if model.model.capabilities.reasoning {
            model.request.generation.reasoning_effort = Some(request.reasoning_effort.clone());
        } else {
            model.request.generation.reasoning_effort = None;
        }
        Ok(Self {
            model,
            messages: request.messages.clone(),
            tools: request.tools.clone(),
        })
    }
}

pub(crate) fn build_messages(
    history: &[Value],
    user_message: &str,
    user_images: &[super::super::images::ChatImage],
) -> Vec<Value> {
    let mut messages = vec![json!({ "role": "system", "content": SYSTEM_PROMPT })];
    messages.extend(history.iter().cloned());
    messages.push(json!({
        "role": "user",
        "content": super::super::images::user_content_value(user_message, user_images)
    }));
    messages
}
