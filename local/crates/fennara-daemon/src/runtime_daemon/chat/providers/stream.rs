use serde::{Deserialize, Serialize};
use serde_json::Value;

use super::error::LlmError;
use super::usage::NormalizedUsage;

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub(crate) struct Usage {
    pub(crate) input_tokens: Option<u64>,
    pub(crate) output_tokens: Option<u64>,
    pub(crate) total_tokens: Option<u64>,
    pub(crate) reasoning_tokens: Option<u64>,
    pub(crate) cache_read_tokens: Option<u64>,
    pub(crate) cache_write_tokens: Option<u64>,
    pub(crate) raw: Option<Value>,
}

impl Usage {
    pub(crate) fn from_provider_value(value: &Value) -> Self {
        let normalized = NormalizedUsage::from_provider_value(Some(value));
        Self {
            input_tokens: Some(normalized.input_tokens),
            output_tokens: Some(normalized.visible_output_tokens()),
            total_tokens: Some(normalized.total_tokens),
            reasoning_tokens: Some(normalized.reasoning_tokens),
            cache_read_tokens: Some(normalized.cache_read_tokens),
            cache_write_tokens: Some(normalized.cache_write_tokens),
            raw: Some(value.clone()),
        }
    }
}

#[allow(dead_code)]
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum FinishReason {
    Stop,
    Length,
    ToolCalls,
    ContentFilter,
    Cancelled,
    Unknown(String),
}

impl FinishReason {
    pub(crate) fn from_provider(value: Option<&str>) -> Self {
        match value.unwrap_or_default() {
            "stop" => Self::Stop,
            "length" | "max_tokens" => Self::Length,
            "tool_calls" | "function_call" => Self::ToolCalls,
            "content_filter" => Self::ContentFilter,
            "" => Self::Stop,
            other => Self::Unknown(other.to_string()),
        }
    }
}

#[allow(dead_code)]
#[derive(Clone, Debug)]
pub(crate) enum StreamEvent {
    StepStart {
        index: u32,
    },
    TextDelta {
        id: String,
        text: String,
    },
    ReasoningDelta {
        id: String,
        text: String,
    },
    ToolCallDelta {
        id: String,
        name: String,
        arguments: String,
    },
    ToolCall {
        id: String,
        name: String,
        arguments: String,
        raw: Value,
    },
    Usage(Usage),
    ProviderError(LlmError),
    Finish {
        reason: FinishReason,
        usage: Option<Usage>,
    },
}
