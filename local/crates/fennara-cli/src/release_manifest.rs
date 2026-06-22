use crate::VERSION;
use crate::app_layout::{arch_name, platform_name};
use serde_json::Value;
use std::cmp::Ordering;

const SUPPORTED_SCHEMA_VERSION: u64 = 1;
const SUPPORTED_INSTALL_PRIMITIVES: &[&str] =
    &["local-zip-v1", "addon-zip-v1", "shared-webview-cef-v1"];

pub struct ReleaseManifest {
    value: Value,
}

#[derive(Clone)]
pub struct ManifestAsset {
    pub name: String,
    pub sha256: String,
}

pub struct ReleaseSelection {
    pub version: String,
    pub local: ManifestAsset,
    pub addon: ManifestAsset,
    pub shared_runtimes: Vec<Value>,
}

impl ReleaseManifest {
    pub fn parse(bytes: &[u8]) -> Result<Self, String> {
        let value: Value = serde_json::from_slice(bytes)
            .map_err(|err| format!("failed to parse release manifest: {err}"))?;
        let manifest = Self { value };
        manifest.validate_schema()?;
        manifest.validate_minimum_cli_version(VERSION)?;
        manifest.validate_install_primitives()?;
        Ok(manifest)
    }

    pub fn select_for_current_platform(&self) -> Result<ReleaseSelection, String> {
        let version = required_string(&self.value, "version")?.to_string();
        let local = self.local_asset_for_current_platform()?;
        let addon = parse_asset(
            self.value
                .pointer("/assets/addon")
                .ok_or_else(|| "release manifest is missing assets.addon".to_string())?,
            "assets.addon",
        )?;
        let shared_runtimes = self.shared_runtimes_for_current_platform()?;

        Ok(ReleaseSelection {
            version,
            local,
            addon,
            shared_runtimes,
        })
    }

    fn validate_schema(&self) -> Result<(), String> {
        let schema_version = self
            .value
            .get("schema_version")
            .and_then(Value::as_u64)
            .ok_or_else(|| "release manifest is missing schema_version".to_string())?;
        if schema_version != SUPPORTED_SCHEMA_VERSION {
            return Err(format!(
                "This release uses Fennara release manifest schema {schema_version}, but this CLI supports schema {SUPPORTED_SCHEMA_VERSION}. {}",
                update_cli_instruction()
            ));
        }
        Ok(())
    }

    fn validate_minimum_cli_version(&self, running_cli_version: &str) -> Result<(), String> {
        let minimum = required_string(&self.value, "minimum_cli_version")?;
        match compare_versions(running_cli_version, minimum) {
            Some(Ordering::Less) => Err(format!(
                "This release requires Fennara CLI {minimum} or newer. You are running {running_cli_version}. {}",
                update_cli_instruction()
            )),
            Some(_) => Ok(()),
            None => Err(format!(
                "release manifest has invalid minimum_cli_version {minimum:?}"
            )),
        }
    }

    fn validate_install_primitives(&self) -> Result<(), String> {
        let Some(primitives) = self.value.get("install_primitives") else {
            return Ok(());
        };
        let primitives = primitives
            .as_array()
            .ok_or_else(|| "release manifest install_primitives must be an array".to_string())?;

        for primitive in primitives {
            let primitive = primitive.as_str().ok_or_else(|| {
                "release manifest install_primitives entries must be strings".to_string()
            })?;
            if !SUPPORTED_INSTALL_PRIMITIVES.contains(&primitive) {
                return Err(format!(
                    "This release uses install primitive {primitive}, but this CLI does not support it. {}",
                    update_cli_instruction()
                ));
            }
        }
        Ok(())
    }

    fn local_asset_for_current_platform(&self) -> Result<ManifestAsset, String> {
        let local_assets = self
            .value
            .pointer("/assets/local")
            .ok_or_else(|| "release manifest is missing assets.local".to_string())?;
        let key = current_platform_key();

        if let Some(asset) = local_assets.get(key.as_str()) {
            return parse_asset(asset, &format!("assets.local.{key}"));
        }

        if let Some(array) = local_assets.as_array() {
            if let Some(asset) = array.iter().find(|asset| matches_current_platform(asset)) {
                return parse_asset(asset, "assets.local[]");
            }
        }

        Err(format!(
            "release manifest has no local package asset for {} {} ({key})",
            platform_name(),
            arch_name()
        ))
    }

    fn shared_runtimes_for_current_platform(&self) -> Result<Vec<Value>, String> {
        let Some(runtimes) = self.value.get("shared_runtimes") else {
            return Ok(Vec::new());
        };
        let runtimes = runtimes
            .as_array()
            .ok_or_else(|| "release manifest shared_runtimes must be an array".to_string())?;

        let mut selected = Vec::new();
        for runtime in runtimes {
            if runtime
                .get("enabled")
                .and_then(Value::as_bool)
                .is_some_and(|enabled| !enabled)
            {
                continue;
            }
            if shared_runtime_matches_current_platform(runtime) {
                selected.push(runtime.clone());
            }
        }
        Ok(selected)
    }
}

fn parse_asset(value: &Value, label: &str) -> Result<ManifestAsset, String> {
    let asset = value.get("asset").unwrap_or(value);
    let name = required_string(asset, "name")?.to_string();
    let sha256 = required_string(asset, "sha256")?.to_string();
    if sha256.len() != 64 || !sha256.chars().all(|ch| ch.is_ascii_hexdigit()) {
        return Err(format!(
            "release manifest {label} has invalid sha256 for {name}"
        ));
    }
    Ok(ManifestAsset { name, sha256 })
}

fn required_string<'a>(value: &'a Value, field: &str) -> Result<&'a str, String> {
    value
        .get(field)
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| format!("release manifest is missing {field}"))
}

fn matches_current_platform(value: &Value) -> bool {
    optional_matches(value, "platform", platform_name())
        && optional_matches(value, "arch", arch_name())
        && optional_platform_arch_matches_current(value)
}

fn shared_runtime_matches_current_platform(value: &Value) -> bool {
    matches_current_platform(value)
}

fn optional_matches(value: &Value, field: &str, expected: &str) -> bool {
    value
        .get(field)
        .and_then(Value::as_str)
        .map(|actual| actual == expected)
        .unwrap_or(true)
}

fn optional_platform_arch_matches_current(value: &Value) -> bool {
    let Some(platform_arch) = value.get("platform_arch").and_then(Value::as_str) else {
        return true;
    };
    let webview_platform_arch = current_webview_platform_arch();
    platform_arch == current_platform_key()
        || (!webview_platform_arch.is_empty() && platform_arch == webview_platform_arch)
}

fn current_platform_key() -> String {
    format!("{}-{}", platform_name(), arch_name())
}

fn current_webview_platform_arch() -> &'static str {
    match (platform_name(), arch_name()) {
        ("linux", "x86_64") => "linux-x64",
        ("linux", "arm64") => "linux-arm64",
        _ => "",
    }
}

fn compare_versions(left: &str, right: &str) -> Option<Ordering> {
    Some(parse_semver_core(left)?.cmp(&parse_semver_core(right)?))
}

fn parse_semver_core(value: &str) -> Option<[u64; 3]> {
    let core = value.split_once('-').map(|(core, _)| core).unwrap_or(value);
    let mut parts = core.split('.');
    let major = parts.next()?.parse().ok()?;
    let minor = parts.next()?.parse().ok()?;
    let patch = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some([major, minor, patch])
}

fn update_cli_instruction() -> &'static str {
    if cfg!(target_os = "windows") {
        "Update the CLI first: irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.ps1 | iex"
    } else {
        "Update the CLI first: curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.sh | sh"
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compares_semver_core_versions() {
        assert_eq!(compare_versions("0.3.0", "0.3.0"), Some(Ordering::Equal));
        assert_eq!(compare_versions("0.3.1", "0.3.0"), Some(Ordering::Greater));
        assert_eq!(compare_versions("0.2.9", "0.3.0"), Some(Ordering::Less));
        assert_eq!(
            compare_versions("0.3.0-beta.1", "0.3.0"),
            Some(Ordering::Equal)
        );
        assert_eq!(compare_versions("0.3", "0.3.0"), None);
    }
}
