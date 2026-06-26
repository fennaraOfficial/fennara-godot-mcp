use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, fs, path::PathBuf};

use super::settings;

const AUTH_FILE: &str = "auth.json";

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
struct AuthFile {
    #[serde(flatten)]
    providers: BTreeMap<String, ProviderAuth>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
struct ProviderAuth {
    #[serde(rename = "type")]
    kind: String,
    key: String,
}

pub(crate) fn api_key(provider: &str) -> Option<String> {
    load_auth()
        .providers
        .remove(clean_provider(provider)?)
        .and_then(|auth| {
            let key = auth.key.trim();
            (!key.is_empty() && auth.kind == "api").then(|| key.to_string())
        })
}

pub(crate) fn has_api_key(provider: &str) -> bool {
    api_key(provider).is_some()
}

pub(crate) fn save_api_key(provider: &str, key: &str) -> Result<(), String> {
    let Some(provider) = clean_provider(provider) else {
        return Err("Provider id is empty.".to_string());
    };
    let key = key.trim();
    if key.is_empty() {
        return Ok(());
    }

    let mut auth = load_auth();
    auth.providers.insert(
        provider.to_string(),
        ProviderAuth {
            kind: "api".to_string(),
            key: key.to_string(),
        },
    );
    write_auth(&auth)
}

pub(crate) fn migrate_legacy_api_key(provider: &str, key: Option<String>) {
    let Some(key) = key
        .map(|value| value.trim().to_string())
        .filter(|value| !value.is_empty())
    else {
        return;
    };
    if !has_api_key(provider) {
        let _ = save_api_key(provider, &key);
    }
}

fn load_auth() -> AuthFile {
    let Ok(raw) = fs::read_to_string(auth_path()) else {
        return AuthFile::default();
    };
    serde_json::from_str(&raw).unwrap_or_default()
}

fn write_auth(auth: &AuthFile) -> Result<(), String> {
    let path = auth_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let raw = serde_json::to_string_pretty(auth).map_err(|error| error.to_string())?;
    fs::write(&path, format!("{raw}\n"))
        .map_err(|error| format!("failed to write {}: {error}", path.display()))?;
    restrict_file_permissions(&path);
    Ok(())
}

fn clean_provider(provider: &str) -> Option<&str> {
    let provider = provider.trim();
    (!provider.is_empty()).then_some(provider)
}

fn auth_path() -> PathBuf {
    settings::app_dir().join(AUTH_FILE)
}

#[cfg(unix)]
fn restrict_file_permissions(path: &PathBuf) {
    use std::os::unix::fs::PermissionsExt;
    if let Ok(mut permissions) = fs::metadata(path).map(|metadata| metadata.permissions()) {
        permissions.set_mode(0o600);
        let _ = fs::set_permissions(path, permissions);
    }
}

#[cfg(not(unix))]
fn restrict_file_permissions(_path: &PathBuf) {}
