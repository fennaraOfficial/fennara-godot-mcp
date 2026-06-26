use reqwest::StatusCode;
use serde_json::Value;
use std::fmt;

#[allow(dead_code)]
#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) enum LlmError {
    Config {
        message: String,
    },
    ProviderNotFound {
        provider: String,
    },
    ModelNotFound {
        provider: String,
        model: String,
    },
    ProviderInit {
        provider: String,
        message: String,
    },
    Auth {
        provider: String,
        message: String,
    },
    Network {
        provider: String,
        message: String,
        retryable: bool,
    },
    Timeout {
        provider: String,
        message: String,
    },
    RateLimit {
        provider: String,
        message: String,
        retry_after_ms: Option<u64>,
    },
    ContextOverflow {
        provider: String,
        message: String,
    },
    InvalidProviderOutput {
        provider: String,
        message: String,
        raw: Option<String>,
    },
    ProviderApi {
        provider: String,
        status: Option<u16>,
        message: String,
        retryable: bool,
    },
    UnknownProvider {
        provider: String,
        message: String,
    },
}

impl LlmError {
    pub(crate) fn code(&self) -> &'static str {
        match self {
            Self::Config { .. } => "provider_config_error",
            Self::ProviderNotFound { .. } => "provider_not_found",
            Self::ModelNotFound { .. } => "model_not_found",
            Self::ProviderInit { .. } => "provider_init_error",
            Self::Auth { .. } => "provider_auth_error",
            Self::Network { .. } => "provider_network_error",
            Self::Timeout { .. } => "provider_timeout",
            Self::RateLimit { .. } => "provider_rate_limited",
            Self::ContextOverflow { .. } => "context_overflow",
            Self::InvalidProviderOutput { .. } => "invalid_provider_output",
            Self::ProviderApi { .. } => "provider_api_error",
            Self::UnknownProvider { .. } => "unknown_provider_error",
        }
    }

    pub(crate) fn user_message(&self) -> String {
        match self {
            Self::Config { message }
            | Self::ProviderInit { message, .. }
            | Self::Auth { message, .. }
            | Self::Network { message, .. }
            | Self::Timeout { message, .. }
            | Self::RateLimit { message, .. }
            | Self::ContextOverflow { message, .. }
            | Self::InvalidProviderOutput { message, .. }
            | Self::ProviderApi { message, .. }
            | Self::UnknownProvider { message, .. } => message.clone(),
            Self::ProviderNotFound { provider } => {
                format!("Unknown chat provider: {provider}")
            }
            Self::ModelNotFound { provider, model } => {
                format!("Unknown model for {provider}: {model}")
            }
        }
    }

    pub(crate) fn from_reqwest(provider: &str, message: &str, error: reqwest::Error) -> Self {
        if error.is_timeout() {
            return Self::Timeout {
                provider: provider.to_string(),
                message: format!("{message}: {error}"),
            };
        }
        Self::Network {
            provider: provider.to_string(),
            message: format!("{message}: {error}"),
            retryable: error.is_connect() || error.is_request(),
        }
    }

    pub(crate) fn from_http_response(provider: &str, status: StatusCode, body: &str) -> Self {
        let message = provider_error_message(body, provider);
        if status == StatusCode::UNAUTHORIZED || status == StatusCode::FORBIDDEN {
            return Self::Auth {
                provider: provider.to_string(),
                message,
            };
        }
        if status == StatusCode::TOO_MANY_REQUESTS {
            return Self::RateLimit {
                provider: provider.to_string(),
                message,
                retry_after_ms: None,
            };
        }
        if status == StatusCode::REQUEST_TIMEOUT || status == StatusCode::GATEWAY_TIMEOUT {
            return Self::Timeout {
                provider: provider.to_string(),
                message,
            };
        }
        if status == StatusCode::PAYLOAD_TOO_LARGE || is_context_overflow_text(&message) {
            return Self::ContextOverflow {
                provider: provider.to_string(),
                message,
            };
        }
        Self::ProviderApi {
            provider: provider.to_string(),
            status: Some(status.as_u16()),
            message,
            retryable: status.is_server_error(),
        }
    }

    pub(crate) fn from_stream_error(provider: &str, error: &Value) -> Self {
        let message = error
            .get("message")
            .and_then(Value::as_str)
            .or_else(|| error.as_str())
            .unwrap_or("Provider stream failed.")
            .to_string();
        let code = error
            .get("code")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_ascii_lowercase();
        if code.contains("context") || is_context_overflow_text(&message) {
            return Self::ContextOverflow {
                provider: provider.to_string(),
                message,
            };
        }
        Self::ProviderApi {
            provider: provider.to_string(),
            status: None,
            message,
            retryable: false,
        }
    }
}

impl fmt::Display for LlmError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.user_message().fmt(formatter)
    }
}

impl std::error::Error for LlmError {}

pub(crate) fn is_context_overflow_text(message: &str) -> bool {
    let lower = message.to_ascii_lowercase();
    [
        "context length",
        "context_length",
        "context window",
        "maximum context",
        "too many tokens",
        "token limit",
        "prompt is too long",
        "input is too long",
        "context overflow",
    ]
    .iter()
    .any(|needle| lower.contains(needle))
}

fn provider_error_message(body: &str, provider: &str) -> String {
    serde_json::from_str::<Value>(body)
        .ok()
        .and_then(|value| {
            value
                .get("error")
                .and_then(|error| {
                    error
                        .get("message")
                        .and_then(Value::as_str)
                        .or_else(|| error.as_str())
                })
                .or_else(|| value.get("message").and_then(Value::as_str))
                .map(ToString::to_string)
        })
        .unwrap_or_else(|| {
            if body.trim().is_empty() {
                format!("{provider} request failed.")
            } else {
                body.to_string()
            }
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_context_overflow_text() {
        assert!(is_context_overflow_text(
            "This model's maximum context length is 8192 tokens."
        ));
    }
}
