use serde::{Deserialize, Serialize};
use std::{env, fs, path::PathBuf};

pub(crate) const DEFAULT_MODEL: &str = "google/gemini-3.5-flash";
pub(crate) const DEFAULT_REASONING_EFFORT: &str = "medium";

#[derive(Clone, Debug, Deserialize, Serialize)]
pub(crate) struct ChatSettings {
    pub(crate) openrouter_api_key: Option<String>,
    pub(crate) model: String,
    #[serde(default = "default_reasoning_effort")]
    pub(crate) reasoning_effort: String,
    #[serde(default)]
    pub(crate) custom_models: Vec<String>,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct PublicChatSettings {
    pub(crate) has_openrouter_key: bool,
    pub(crate) model: String,
    pub(crate) default_model: &'static str,
    pub(crate) reasoning_effort: String,
    pub(crate) reasoning_effort_options: Vec<&'static str>,
    pub(crate) text_model_suggestions: Vec<String>,
    pub(crate) custom_models: Vec<String>,
}

impl Default for ChatSettings {
    fn default() -> Self {
        Self {
            openrouter_api_key: None,
            model: DEFAULT_MODEL.to_string(),
            reasoning_effort: DEFAULT_REASONING_EFFORT.to_string(),
            custom_models: Vec::new(),
        }
    }
}

impl ChatSettings {
    pub(crate) fn public(&self) -> PublicChatSettings {
        PublicChatSettings {
            has_openrouter_key: self
                .openrouter_api_key
                .as_ref()
                .is_some_and(|key| !key.trim().is_empty()),
            model: clean_model(&self.model).unwrap_or_else(|| DEFAULT_MODEL.to_string()),
            default_model: DEFAULT_MODEL,
            reasoning_effort: clean_reasoning_effort(&self.reasoning_effort).to_string(),
            reasoning_effort_options: vec!["low", DEFAULT_REASONING_EFFORT, "high"],
            text_model_suggestions: suggestion_models(&self.custom_models),
            custom_models: self.custom_models.clone(),
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

fn suggestion_models(custom_models: &[String]) -> Vec<String> {
    let mut models = recommended_model_ids()
        .into_iter()
        .map(ToString::to_string)
        .collect::<Vec<_>>();
    for model in custom_models {
        if !models.iter().any(|existing| existing == model) {
            models.push(model.clone());
        }
    }
    models
}

#[derive(Clone, Debug, Deserialize)]
pub(crate) struct SaveSettingsRequest {
    pub(crate) openrouter_api_key: Option<String>,
    pub(crate) model: Option<String>,
    pub(crate) reasoning_effort: Option<String>,
}

pub(crate) fn load_settings() -> ChatSettings {
    let path = settings_path();
    let Ok(raw) = fs::read_to_string(path) else {
        return ChatSettings::default();
    };
    let Ok(mut settings) = serde_json::from_str::<ChatSettings>(&raw) else {
        return ChatSettings::default();
    };
    if let Some(model) = clean_model(&settings.model) {
        settings.model = model;
    } else {
        settings.model = DEFAULT_MODEL.to_string();
    }
    settings.reasoning_effort = clean_reasoning_effort(&settings.reasoning_effort).to_string();
    settings.custom_models = clean_model_list(&settings.custom_models);
    settings
}

pub(crate) fn save_settings(update: SaveSettingsRequest) -> Result<ChatSettings, String> {
    let mut settings = load_settings();
    if let Some(key) = update.openrouter_api_key {
        let trimmed = key.trim();
        if !trimmed.is_empty() {
            settings.openrouter_api_key = Some(trimmed.to_string());
        }
    }
    if let Some(model) = update.model {
        settings.model = clean_model(&model).unwrap_or_else(|| DEFAULT_MODEL.to_string());
        remember_custom_model(&mut settings.custom_models, &settings.model);
    }
    if let Some(reasoning_effort) = update.reasoning_effort {
        settings.reasoning_effort = clean_reasoning_effort(&reasoning_effort).to_string();
    }

    let path = settings_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let raw = serde_json::to_string_pretty(&settings).map_err(|error| error.to_string())?;
    fs::write(&path, format!("{raw}\n"))
        .map_err(|error| format!("failed to write {}: {error}", path.display()))?;
    Ok(settings)
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

fn default_reasoning_effort() -> String {
    DEFAULT_REASONING_EFFORT.to_string()
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
