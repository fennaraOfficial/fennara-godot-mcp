use crate::app_layout::display_path;
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::env;
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use zip::ZipArchive;

const REPO: &str = "fennaraOfficial/fennara-godot-ai";

#[derive(Clone)]
pub struct ReleaseAsset {
    pub name: String,
    pub url: String,
    pub version: Option<String>,
}

pub struct Release {
    pub tag: String,
    pub assets: Value,
}

impl Release {
    pub fn asset(&self, prefix: &str) -> Option<ReleaseAsset> {
        self.asset_by_prefix_suffix(prefix, ".zip")
    }

    pub fn manifest_asset(&self) -> Option<ReleaseAsset> {
        self.asset_by_prefix_suffix("fennara-release-manifest-v", ".json")
    }

    pub fn asset_by_name(&self, expected_name: &str) -> Option<ReleaseAsset> {
        self.assets.as_array()?.iter().find_map(|asset| {
            let name = asset.get("name")?.as_str()?;
            if name != expected_name {
                return None;
            }
            let url = asset.get("browser_download_url")?.as_str()?;
            Some(ReleaseAsset {
                name: name.to_string(),
                url: url.to_string(),
                version: version_from_asset_name(name),
            })
        })
    }

    fn asset_by_prefix_suffix(&self, prefix: &str, suffix: &str) -> Option<ReleaseAsset> {
        self.assets.as_array()?.iter().find_map(|asset| {
            let name = asset.get("name")?.as_str()?;
            if !name.starts_with(prefix) || !name.ends_with(suffix) {
                return None;
            }
            let url = asset.get("browser_download_url")?.as_str()?;
            Some(ReleaseAsset {
                name: name.to_string(),
                url: url.to_string(),
                version: version_from_asset_name(name),
            })
        })
    }
}

pub struct DownloadAsset<'a> {
    pub url: &'a str,
    pub expected_sha256: Option<&'a str>,
    pub label: &'a str,
}

pub fn fetch_release(version: &str) -> Result<Release, String> {
    let tag = if version == "latest" {
        "latest".to_string()
    } else {
        format!("v{version}")
    };
    let url = format!("https://api.github.com/repos/{REPO}/releases/tags/{tag}");
    let response = ureq::get(&url)
        .set("User-Agent", "fennara-cli")
        .call()
        .map_err(|err| format!("failed to fetch release metadata: {err}"))?;
    let value: Value = response
        .into_json()
        .map_err(|err| format!("failed to parse release metadata: {err}"))?;

    Ok(Release {
        tag: value
            .get("tag_name")
            .and_then(Value::as_str)
            .unwrap_or(&tag)
            .to_string(),
        assets: value.get("assets").cloned().unwrap_or(Value::Null),
    })
}

pub fn download_zip_to_dir(asset: &DownloadAsset<'_>, target: &Path) -> Result<(), String> {
    fs::create_dir_all(target)
        .map_err(|err| format!("failed to create {}: {err}", display_path(target)))?;
    let bytes = download_bytes(asset.url, asset.label)?;
    if let Some(expected_sha256) = asset.expected_sha256 {
        let actual_sha256 = format!("{:x}", Sha256::digest(&bytes));
        if !actual_sha256.eq_ignore_ascii_case(expected_sha256) {
            return Err(format!(
                "{} sha256 mismatch: expected {expected_sha256}, got {actual_sha256}",
                asset.label
            ));
        }
    }

    let cursor = Cursor::new(bytes);
    let mut archive =
        ZipArchive::new(cursor).map_err(|err| format!("failed to open downloaded zip: {err}"))?;
    archive
        .extract(target)
        .map_err(|err| format!("failed to extract zip into {}: {err}", display_path(target)))
}

pub fn download_bytes(url: &str, label: &str) -> Result<Vec<u8>, String> {
    let response = ureq::get(url)
        .set("User-Agent", "fennara-cli")
        .call()
        .map_err(|err| format!("failed to download {label} from {url}: {err}"))?;
    let mut bytes = Vec::new();
    response
        .into_reader()
        .read_to_end(&mut bytes)
        .map_err(|err| format!("failed to read download for {label}: {err}"))?;
    Ok(bytes)
}

pub fn create_temp_dir(prefix: &str) -> Result<PathBuf, String> {
    let path = env::temp_dir().join(format!(
        "{prefix}-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_millis())
            .unwrap_or(0)
    ));
    fs::create_dir_all(&path)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&path)))?;
    Ok(path)
}

fn version_from_asset_name(name: &str) -> Option<String> {
    let marker = "-v";
    let start = name.rfind(marker)? + marker.len();
    let version = name.get(start..)?.strip_suffix(".zip")?;
    if version.split('.').count() == 3 && version.chars().all(|c| c.is_ascii_digit() || c == '.') {
        Some(version.to_string())
    } else {
        None
    }
}
