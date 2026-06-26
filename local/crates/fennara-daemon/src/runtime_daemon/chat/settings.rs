use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, env, fs, path::PathBuf};

use super::auth;
use super::providers::{self, ProviderId, PublicProvider};

pub(crate) const DEFAULT_MODEL: &str = "google/gemini-3.5-flash";
pub(crate) const DEFAULT_REASONING_EFFORT: &str = "medium";
pub(crate) const DEFAULT_OLLAMA_BASE_URL: &str = "http://127.0.0.1:11434";
pub(crate) const DEFAULT_CHAT_SURFACE: &str = "embedded";
pub(crate) const BROWSER_CHAT_SURFACE: &str = "browser";

#[derive(Clone, Debug, Deserialize, Serialize)]
pub(crate) struct ChatSettings {
    #[serde(default, skip_serializing)]
    pub(crate) openrouter_api_key: Option<String>,
    #[serde(default = "default_ollama_base_url")]
    pub(crate) ollama_base_url: String,
    #[serde(default)]
    pub(crate) provider_base_urls: BTreeMap<String, String>,
    pub(crate) model: String,
    #[serde(default = "default_reasoning_effort")]
    pub(crate) reasoning_effort: String,
    #[serde(default)]
    pub(crate) custom_models: Vec<String>,
    #[serde(default = "default_chat_surface")]
    pub(crate) chat_surface: String,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct PublicChatSettings {
    pub(crate) has_openrouter_key: bool,
    pub(crate) has_ollama_cloud_key: bool,
    pub(crate) providers: Vec<PublicProvider>,
    pub(crate) ollama_base_url: String,
    pub(crate) provider_base_urls: BTreeMap<String, String>,
    pub(crate) model: String,
    pub(crate) default_model: &'static str,
    pub(crate) reasoning_effort: String,
    pub(crate) reasoning_effort_options: Vec<&'static str>,
    pub(crate) text_model_suggestions: Vec<String>,
    pub(crate) custom_models: Vec<String>,
    pub(crate) chat_surface: String,
}

impl Default for ChatSettings {
    fn default() -> Self {
        Self {
            openrouter_api_key: None,
            ollama_base_url: DEFAULT_OLLAMA_BASE_URL.to_string(),
            provider_base_urls: default_provider_base_urls(),
            model: DEFAULT_MODEL.to_string(),
            reasoning_effort: DEFAULT_REASONING_EFFORT.to_string(),
            custom_models: Vec::new(),
            chat_surface: DEFAULT_CHAT_SURFACE.to_string(),
        }
    }
}

impl ChatSettings {
    pub(crate) fn public(&self) -> PublicChatSettings {
        let providers = providers::public_provider_registry(self);
        let has_openrouter_key = provider_connected(&providers, ProviderId::OPENROUTER);
        let has_ollama_cloud_key = provider_connected(&providers, ProviderId::OLLAMA_CLOUD);
        PublicChatSettings {
            has_openrouter_key,
            has_ollama_cloud_key,
            providers,
            ollama_base_url: clean_ollama_base_url(&self.ollama_base_url),
            provider_base_urls: clean_provider_base_urls(&self.provider_base_urls),
            model: clean_model(&self.model).unwrap_or_else(|| DEFAULT_MODEL.to_string()),
            default_model: DEFAULT_MODEL,
            reasoning_effort: clean_reasoning_effort(&self.reasoning_effort).to_string(),
            reasoning_effort_options: vec!["low", DEFAULT_REASONING_EFFORT, "high"],
            text_model_suggestions: suggestion_models(&self.custom_models, has_openrouter_key),
            custom_models: self.custom_models.clone(),
            chat_surface: clean_chat_surface(&self.chat_surface).to_string(),
        }
    }
}

pub(crate) fn recommended_model_ids() -> Vec<&'static str> {
    vec![
        DEFAULT_MODEL,
        "qwen/qwen3.7-plus",
        "moonshotai/kimi-k2.7-code",
        "minimax/minimax-m3",
        "openai/gpt-5.5",
        "anthropic/claude-opus-4.8",
        "deepseek/deepseek-v4-flash",
        "deepseek/deepseek-v4-pro",
        "z-ai/glm-5.2",
    ]
}

fn suggestion_models(custom_models: &[String], has_openrouter_key: bool) -> Vec<String> {
    let mut models = if has_openrouter_key {
        recommended_model_ids()
            .into_iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
    } else {
        Vec::new()
    };
    for model in custom_models {
        if !has_openrouter_key && !model.starts_with("ollama/") && !model.starts_with("lmstudio/") {
            continue;
        }
        if !models.iter().any(|existing| existing == model) {
            models.push(model.clone());
        }
    }
    models
}

#[derive(Clone, Debug, Deserialize)]
pub(crate) struct SaveSettingsRequest {
    pub(crate) openrouter_api_key: Option<String>,
    pub(crate) ollama_cloud_api_key: Option<String>,
    pub(crate) provider_api_keys: Option<BTreeMap<String, String>>,
    pub(crate) ollama_base_url: Option<String>,
    pub(crate) provider_base_urls: Option<BTreeMap<String, String>>,
    pub(crate) model: Option<String>,
    pub(crate) reasoning_effort: Option<String>,
    pub(crate) chat_surface: Option<String>,
}

pub(crate) fn load_settings() -> ChatSettings {
    let path = settings_path();
    let Ok(raw) = fs::read_to_string(path) else {
        return ChatSettings::default();
    };
    let Ok(mut settings) = serde_json::from_str::<ChatSettings>(&raw) else {
        return ChatSettings::default();
    };
    let legacy_openrouter_key = settings.openrouter_api_key.take();
    if let Some(model) = clean_model(&settings.model) {
        settings.model = model;
    } else {
        settings.model = DEFAULT_MODEL.to_string();
    }
    settings.reasoning_effort = clean_reasoning_effort(&settings.reasoning_effort).to_string();
    settings.ollama_base_url = clean_ollama_base_url(&settings.ollama_base_url);
    settings.provider_base_urls = clean_provider_base_urls(&settings.provider_base_urls);
    settings.provider_base_urls.insert(
        ProviderId::OLLAMA.to_string(),
        settings.ollama_base_url.clone(),
    );
    settings.custom_models = clean_model_list(&settings.custom_models);
    settings.chat_surface = clean_chat_surface(&settings.chat_surface).to_string();
    if legacy_openrouter_key.is_some() {
        auth::migrate_legacy_api_key(ProviderId::OPENROUTER, legacy_openrouter_key);
        let _ = write_settings_file(&settings);
    }
    settings
}

pub(crate) fn save_settings(update: SaveSettingsRequest) -> Result<ChatSettings, String> {
    let mut settings = load_settings();
    if let Some(key) = update.openrouter_api_key {
        let trimmed = key.trim();
        if !trimmed.is_empty() {
            auth::save_api_key(ProviderId::OPENROUTER, trimmed)?;
        }
    }
    if let Some(key) = update.ollama_cloud_api_key {
        let trimmed = key.trim();
        if !trimmed.is_empty() {
            auth::save_api_key(ProviderId::OLLAMA_CLOUD, trimmed)?;
        }
    }
    if let Some(provider_api_keys) = update.provider_api_keys {
        save_provider_api_keys(provider_api_keys)?;
    }
    if let Some(base_url) = update.ollama_base_url {
        settings.ollama_base_url = clean_ollama_base_url(&base_url);
        settings.provider_base_urls.insert(
            ProviderId::OLLAMA.to_string(),
            settings.ollama_base_url.clone(),
        );
    }
    if let Some(provider_base_urls) = update.provider_base_urls {
        for (provider, base_url) in provider_base_urls {
            let Some(provider) = super::providers::ProviderId::new(provider) else {
                continue;
            };
            let clean = clean_base_url(&base_url);
            if clean.is_empty() {
                continue;
            }
            if provider.as_str() == ProviderId::OLLAMA {
                settings.ollama_base_url = clean_ollama_base_url(&clean);
            }
            settings
                .provider_base_urls
                .insert(provider.to_string(), clean_base_url(&clean));
        }
    }
    if let Some(model) = update.model {
        settings.model = clean_model(&model).unwrap_or_else(|| DEFAULT_MODEL.to_string());
        remember_custom_model(&mut settings.custom_models, &settings.model);
    }
    if let Some(reasoning_effort) = update.reasoning_effort {
        settings.reasoning_effort = clean_reasoning_effort(&reasoning_effort).to_string();
    }
    if let Some(chat_surface) = update.chat_surface {
        settings.chat_surface = clean_chat_surface(&chat_surface).to_string();
    }

    write_settings_file(&settings)?;
    Ok(settings)
}

fn save_provider_api_keys(provider_api_keys: BTreeMap<String, String>) -> Result<(), String> {
    for (provider, key) in provider_api_keys {
        let provider = provider.trim();
        let key = key.trim();
        if provider.is_empty() || key.is_empty() {
            continue;
        }
        auth::save_api_key(provider, key)?;
    }
    Ok(())
}

fn write_settings_file(settings: &ChatSettings) -> Result<(), String> {
    let path = settings_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let raw = serde_json::to_string_pretty(&settings).map_err(|error| error.to_string())?;
    fs::write(&path, format!("{raw}\n"))
        .map_err(|error| format!("failed to write {}: {error}", path.display()))?;
    Ok(())
}

fn remember_custom_model(custom_models: &mut Vec<String>, model: &str) {
    if recommended_model_ids()
        .into_iter()
        .any(|recommended| recommended == model)
    {
        return;
    }
    if model == DEFAULT_MODEL || model == "openrouter/auto" {
        return;
    }
    if !model.contains('/') {
        return;
    }
    if !custom_models.iter().any(|existing| existing == model) {
        custom_models.push(model.to_string());
    }
    *custom_models = clean_model_list(custom_models);
}

fn clean_model_list(models: &[String]) -> Vec<String> {
    let mut clean = Vec::new();
    for model in models {
        let Some(model) = clean_model(model) else {
            continue;
        };
        if !model.contains('/') {
            continue;
        }
        if !clean.iter().any(|existing| existing == &model) {
            clean.push(model);
        }
    }
    clean
}

pub(crate) fn clean_model(model: &str) -> Option<String> {
    let trimmed = model.trim();
    if trimmed.is_empty() {
        return None;
    }
    let clean = strip_nitro_variant(trimmed);
    if clean.starts_with("ollama/")
        || clean.starts_with("ollama-cloud/")
        || clean.starts_with("lmstudio/")
        || clean.starts_with("deepseek/")
        || clean.starts_with("zai/")
    {
        return Some(clean.to_string());
    }
    if clean == "openrouter/auto" || clean.starts_with('~') || clean.ends_with("-latest") {
        return Some(DEFAULT_MODEL.to_string());
    }
    Some(clean.to_string())
}

fn strip_nitro_variant(model: &str) -> &str {
    let Some(prefix) = model.get(..model.len().saturating_sub(":nitro".len())) else {
        return model;
    };
    if model[prefix.len()..].eq_ignore_ascii_case(":nitro") {
        prefix
    } else {
        model
    }
}

pub(crate) fn clean_reasoning_effort(effort: &str) -> &'static str {
    match effort.trim().to_ascii_lowercase().as_str() {
        "low" => "low",
        "medium" => DEFAULT_REASONING_EFFORT,
        "high" => "high",
        _ => DEFAULT_REASONING_EFFORT,
    }
}

pub(crate) fn clean_chat_surface(surface: &str) -> &'static str {
    match surface.trim().to_ascii_lowercase().as_str() {
        BROWSER_CHAT_SURFACE => BROWSER_CHAT_SURFACE,
        _ => DEFAULT_CHAT_SURFACE,
    }
}

pub(crate) fn clean_ollama_base_url(base_url: &str) -> String {
    let clean = clean_base_url(base_url);
    if clean.is_empty() {
        DEFAULT_OLLAMA_BASE_URL.to_string()
    } else {
        clean
    }
}

impl ChatSettings {
    pub(crate) fn provider_base_url(&self, provider_id: &str, default_base_url: &str) -> String {
        self.provider_base_urls
            .get(provider_id)
            .map(String::as_str)
            .map(clean_base_url)
            .filter(|url| !url.is_empty())
            .unwrap_or_else(|| default_base_url.to_string())
    }
}

fn clean_provider_base_urls(base_urls: &BTreeMap<String, String>) -> BTreeMap<String, String> {
    let mut clean = default_provider_base_urls();
    for (provider, base_url) in base_urls {
        let Some(provider) = super::providers::ProviderId::new(provider) else {
            continue;
        };
        let base_url = clean_base_url(base_url);
        if base_url.is_empty() {
            continue;
        }
        clean.insert(provider.to_string(), base_url);
    }
    clean
}

fn clean_base_url(base_url: &str) -> String {
    base_url.trim().trim_end_matches('/').to_string()
}

fn default_provider_base_urls() -> BTreeMap<String, String> {
    BTreeMap::from([
        (
            ProviderId::OLLAMA.to_string(),
            DEFAULT_OLLAMA_BASE_URL.to_string(),
        ),
        (
            ProviderId::LMSTUDIO.to_string(),
            super::providers::lmstudio_v1_base_url(""),
        ),
    ])
}

fn default_reasoning_effort() -> String {
    DEFAULT_REASONING_EFFORT.to_string()
}

fn default_chat_surface() -> String {
    DEFAULT_CHAT_SURFACE.to_string()
}

fn default_ollama_base_url() -> String {
    DEFAULT_OLLAMA_BASE_URL.to_string()
}

fn provider_connected(providers: &[PublicProvider], provider_id: &str) -> bool {
    providers
        .iter()
        .any(|provider| provider.id == provider_id && provider.connected)
}

fn settings_path() -> PathBuf {
    app_dir().join("chat_settings.json")
}

pub(crate) fn app_dir() -> PathBuf {
    #[cfg(target_os = "windows")]
    {
        if let Some(path) = env::var_os("LOCALAPPDATA") {
            return PathBuf::from(path).join("Fennara");
        }
    }

    #[cfg(target_os = "macos")]
    {
        if let Some(path) = home_dir() {
            return path
                .join("Library")
                .join("Application Support")
                .join("Fennara");
        }
    }

    #[cfg(all(unix, not(target_os = "macos")))]
    {
        if let Some(path) = env::var_os("XDG_DATA_HOME") {
            return PathBuf::from(path).join("fennara");
        }
        if let Some(path) = home_dir() {
            return path.join(".local").join("share").join("fennara");
        }
    }

    env::current_dir().unwrap_or_else(|_| PathBuf::from("."))
}

#[cfg(any(target_os = "macos", all(unix, not(target_os = "macos"))))]
fn home_dir() -> Option<PathBuf> {
    env::var_os("HOME").map(PathBuf::from)
}
