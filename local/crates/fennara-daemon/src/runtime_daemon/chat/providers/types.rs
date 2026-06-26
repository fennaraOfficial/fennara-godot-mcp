use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::BTreeMap;
use std::fmt;

#[derive(Clone, Debug, Deserialize, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize)]
pub(crate) struct ProviderId(String);

impl ProviderId {
    pub(crate) const OPENROUTER: &'static str = "openrouter";
    pub(crate) const OLLAMA: &'static str = "ollama";
    pub(crate) const OLLAMA_CLOUD: &'static str = "ollama-cloud";
    pub(crate) const LMSTUDIO: &'static str = "lmstudio";
    pub(crate) const DEEPSEEK: &'static str = "deepseek";
    pub(crate) const ZAI: &'static str = "zai";
    pub(crate) const LOCAL: &'static str = "local";

    pub(crate) fn new(value: impl Into<String>) -> Option<Self> {
        let value = value.into();
        let clean = value.trim().to_ascii_lowercase();
        if clean.is_empty()
            || !clean
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
        {
            return None;
        }
        Some(Self(clean))
    }

    pub(crate) fn unchecked(value: &'static str) -> Self {
        Self(value.to_string())
    }

    pub(crate) fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for ProviderId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(formatter)
    }
}

#[derive(Clone, Debug, Deserialize, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize)]
pub(crate) struct ModelId(String);

impl ModelId {
    pub(crate) fn new(value: impl Into<String>) -> Option<Self> {
        let value = value.into();
        let clean = value.trim();
        if clean.is_empty() {
            return None;
        }
        Some(Self(clean.to_string()))
    }

    pub(crate) fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for ModelId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(formatter)
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub(crate) struct ModelRef {
    pub(crate) provider: ProviderId,
    pub(crate) model: ModelId,
    pub(crate) variant: Option<String>,
}

impl ModelRef {
    pub(crate) fn parse(value: &str) -> Result<Self, String> {
        let clean = value.trim();
        let Some((provider, model)) = clean.split_once('/') else {
            return Err("Model references must be shaped like provider/model.".to_string());
        };
        let provider = ProviderId::new(provider)
            .ok_or_else(|| "Model provider id is empty or invalid.".to_string())?;
        let model =
            ModelId::new(model).ok_or_else(|| "Model id is empty after provider/.".to_string())?;
        Ok(Self {
            provider,
            model,
            variant: None,
        })
    }

    pub(crate) fn new(provider: ProviderId, model: ModelId) -> Self {
        Self {
            provider,
            model,
            variant: None,
        }
    }

    pub(crate) fn canonical(&self) -> String {
        format!("{}/{}", self.provider, self.model)
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum AdapterKind {
    OpenAiCompatibleChat,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum Auth {
    None,
    Env { var: String },
    Bearer { secret_name: String },
    InlineBearer { value: String },
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub(crate) struct GenerationDefaults {
    pub(crate) temperature: Option<f32>,
    pub(crate) max_output_tokens: Option<u32>,
    pub(crate) reasoning_effort: Option<String>,
}

impl GenerationDefaults {
    pub(crate) fn merge(&mut self, patch: &Self) {
        if patch.temperature.is_some() {
            self.temperature = patch.temperature;
        }
        if patch.max_output_tokens.is_some() {
            self.max_output_tokens = patch.max_output_tokens;
        }
        if patch.reasoning_effort.is_some() {
            self.reasoning_effort = patch.reasoning_effort.clone();
        }
    }
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub(crate) struct RequestDefaults {
    pub(crate) headers: BTreeMap<String, String>,
    pub(crate) body: Map<String, Value>,
    pub(crate) generation: GenerationDefaults,
    pub(crate) provider_options: Map<String, Value>,
}

impl RequestDefaults {
    pub(crate) fn merged(&self, patch: &Self) -> Self {
        let mut next = self.clone();
        next.merge(patch);
        next
    }

    pub(crate) fn merge(&mut self, patch: &Self) {
        for (key, value) in &patch.headers {
            self.headers.insert(key.clone(), value.clone());
        }
        merge_json_map(&mut self.body, &patch.body);
        self.generation.merge(&patch.generation);
        merge_json_map(&mut self.provider_options, &patch.provider_options);
    }
}

fn merge_json_map(target: &mut Map<String, Value>, patch: &Map<String, Value>) {
    for (key, value) in patch {
        match (target.get_mut(key), value) {
            (Some(Value::Object(target_object)), Value::Object(patch_object)) => {
                merge_json_map(target_object, patch_object);
            }
            _ => {
                target.insert(key.clone(), value.clone());
            }
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub(crate) struct Capabilities {
    pub(crate) tools: bool,
    pub(crate) input: Vec<String>,
    pub(crate) output: Vec<String>,
    pub(crate) reasoning: bool,
}

impl Capabilities {
    pub(crate) fn text_tools() -> Self {
        Self {
            tools: true,
            input: vec!["text".to_string()],
            output: vec!["text".to_string()],
            reasoning: false,
        }
    }

    pub(crate) fn text_image_tools_reasoning() -> Self {
        Self {
            tools: true,
            input: vec!["text".to_string(), "image".to_string()],
            output: vec!["text".to_string()],
            reasoning: true,
        }
    }
}

#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub(crate) struct Limits {
    pub(crate) context_tokens: Option<u32>,
    pub(crate) input_tokens: Option<u32>,
    pub(crate) output_tokens: Option<u32>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub(crate) struct ProviderDefinition {
    pub(crate) id: ProviderId,
    pub(crate) name: String,
    pub(crate) adapter: AdapterKind,
    pub(crate) base_url: Option<String>,
    pub(crate) auth: Auth,
    pub(crate) request: RequestDefaults,
    pub(crate) disabled: bool,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub(crate) struct ModelDefinition {
    pub(crate) id: ModelId,
    pub(crate) provider: ProviderId,
    pub(crate) display_name: String,
    pub(crate) adapter_model_id: String,
    pub(crate) capabilities: Capabilities,
    pub(crate) limits: Limits,
    pub(crate) request: RequestDefaults,
    pub(crate) enabled: bool,
}

#[allow(dead_code)]
#[derive(Clone, Debug)]
pub(crate) struct ResolvedModel {
    pub(crate) reference: ModelRef,
    pub(crate) provider: ProviderDefinition,
    pub(crate) model: ModelDefinition,
    pub(crate) request: RequestDefaults,
}

#[derive(Clone, Debug)]
pub(crate) struct ProviderSettings {
    pub(crate) openrouter_api_key: Option<String>,
    pub(crate) ollama_cloud_api_key: Option<String>,
    pub(crate) lmstudio_api_key: Option<String>,
    pub(crate) deepseek_api_key: Option<String>,
    pub(crate) zai_api_key: Option<String>,
    pub(crate) ollama_base_url: String,
    pub(crate) lmstudio_base_url: String,
    pub(crate) custom_models: Vec<String>,
}

#[derive(Clone, Debug)]
pub(crate) struct ChatRequest {
    pub(crate) model: String,
    pub(crate) reasoning_effort: String,
    pub(crate) messages: Vec<Value>,
    pub(crate) tools: Vec<Value>,
}

#[derive(Clone, Debug)]
pub(crate) enum StreamItem {
    Text {
        content: String,
        done: bool,
    },
    Reasoning {
        content: String,
        done: bool,
    },
    FunctionCall {
        id: String,
        name: String,
        arguments: String,
        done: bool,
    },
    Usage(Value),
}

#[derive(Clone, Debug)]
pub(crate) struct ChatCompletion {
    pub(crate) content: String,
    pub(crate) tool_calls: Vec<Value>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn model_ref_uses_first_slash_for_provider() {
        let model_ref = ModelRef::parse("openrouter/google/gemini-3.5-flash").unwrap();

        assert_eq!(model_ref.provider.as_str(), "openrouter");
        assert_eq!(model_ref.model.as_str(), "google/gemini-3.5-flash");
    }

    #[test]
    fn request_defaults_merge_headers_and_nested_body() {
        let mut base = RequestDefaults::default();
        base.headers.insert("A".to_string(), "one".to_string());
        base.body
            .insert("provider".to_string(), json!({ "sort": "latency" }));
        base.generation.temperature = Some(0.2);

        let mut patch = RequestDefaults::default();
        patch.headers.insert("A".to_string(), "two".to_string());
        patch.headers.insert("B".to_string(), "three".to_string());
        patch
            .body
            .insert("provider".to_string(), json!({ "allow_fallbacks": true }));
        patch.generation.max_output_tokens = Some(4096);

        let merged = base.merged(&patch);

        assert_eq!(merged.headers.get("A").unwrap(), "two");
        assert_eq!(merged.headers.get("B").unwrap(), "three");
        assert_eq!(merged.body["provider"]["sort"], json!("latency"));
        assert_eq!(merged.body["provider"]["allow_fallbacks"], json!(true));
        assert_eq!(merged.generation.temperature, Some(0.2));
        assert_eq!(merged.generation.max_output_tokens, Some(4096));
    }
}
