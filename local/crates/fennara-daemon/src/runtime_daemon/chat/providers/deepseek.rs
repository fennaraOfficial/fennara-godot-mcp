use super::types::{
    AdapterKind, Auth, Capabilities, GenerationDefaults, Limits, ModelDefinition, ModelId,
    ProviderDefinition, ProviderId, RequestDefaults,
};

pub(crate) const PROVIDER_ID: &str = ProviderId::DEEPSEEK;
pub(crate) const API_BASE: &str = "https://api.deepseek.com";
pub(crate) const API_KEY_ENV: &str = "DEEPSEEK_API_KEY";

pub(crate) fn provider_definition(api_key: Option<&str>) -> ProviderDefinition {
    ProviderDefinition {
        id: ProviderId::unchecked(PROVIDER_ID),
        name: "DeepSeek".to_string(),
        adapter: AdapterKind::OpenAiCompatibleChat,
        base_url: Some(API_BASE.to_string()),
        auth: api_key
            .filter(|key| !key.trim().is_empty())
            .map(|key| Auth::InlineBearer {
                value: key.trim().to_string(),
            })
            .unwrap_or_else(|| Auth::Env {
                var: API_KEY_ENV.to_string(),
            }),
        request: RequestDefaults::default(),
        disabled: false,
    }
}

pub(crate) fn model_definition(model_id: &str, display_name: Option<String>) -> ModelDefinition {
    let mut request = RequestDefaults::default();
    request.generation = GenerationDefaults {
        temperature: Some(0.7),
        max_output_tokens: None,
        reasoning_effort: None,
    };

    ModelDefinition {
        id: ModelId::new(model_id).expect("DeepSeek model id is valid"),
        provider: ProviderId::unchecked(PROVIDER_ID),
        display_name: display_name.unwrap_or_else(|| fallback_display_name(model_id)),
        adapter_model_id: model_id.to_string(),
        capabilities: Capabilities::text_tools(),
        limits: Limits::default(),
        request,
        enabled: true,
    }
}

fn fallback_display_name(id: &str) -> String {
    id.split('/').next_back().unwrap_or(id).replace('-', " ")
}
