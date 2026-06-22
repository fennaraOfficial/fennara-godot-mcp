use crate::app_layout::{AppLayout, arch_name, binary_name, display_path, platform_name};
use crate::release_manifest::ReleaseManifest;
use crate::webview_runtime;
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::env;
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use zip::ZipArchive;

const REPO: &str = "fennaraOfficial/fennara-godot-mcp";

pub struct InstalledPackage {
    pub version: String,
    pub addon_dir: PathBuf,
}

pub fn ensure_package(version_request: &str) -> Result<InstalledPackage, String> {
    let layout = AppLayout::detect()?;
    layout.ensure_base_dirs()?;

    let release = fetch_release(version_request)?;
    if let Some(manifest_asset) = release.manifest_asset() {
        let bytes = download_bytes(manifest_asset.url, manifest_asset.name)?;
        let manifest = ReleaseManifest::parse(&bytes)?;
        return ensure_manifest_package(&layout, &release, &manifest);
    }

    ensure_legacy_package(&layout, &release)
}

fn ensure_manifest_package(
    layout: &AppLayout,
    release: &Release,
    manifest: &ReleaseManifest,
) -> Result<InstalledPackage, String> {
    let selection = manifest.select_for_current_platform()?;
    let local_asset = release
        .asset_by_name(&selection.local.name)
        .ok_or_else(|| {
            format!(
                "release {} is missing {}",
                release.tag, selection.local.name
            )
        })?;
    let addon_asset = release
        .asset_by_name(&selection.addon.name)
        .ok_or_else(|| {
            format!(
                "release {} is missing {}",
                release.tag, selection.addon.name
            )
        })?;

    let installed = ensure_selected_package(
        layout,
        &selection.version,
        DownloadAsset {
            url: local_asset.url,
            expected_sha256: Some(selection.local.sha256.as_str()),
            label: selection.local.name.as_str(),
        },
        DownloadAsset {
            url: addon_asset.url,
            expected_sha256: Some(selection.addon.sha256.as_str()),
            label: selection.addon.name.as_str(),
        },
    )?;

    for message in webview_runtime::ensure_from_release_manifest(
        layout,
        &selection.shared_runtimes,
        &release.assets,
    )? {
        println!("{message}");
    }

    Ok(installed)
}

fn ensure_legacy_package(
    layout: &AppLayout,
    release: &Release,
) -> Result<InstalledPackage, String> {
    let local_prefix = format!("fennara-local-{}-{}-v", platform_name(), arch_name());
    let addon_prefix = "fennara-addon-v".to_string();
    let local_asset = release
        .asset(&local_prefix)
        .ok_or_else(|| format!("release {} is missing {local_prefix}*.zip", release.tag))?;
    let addon_asset = release
        .asset(&addon_prefix)
        .ok_or_else(|| format!("release {} is missing {addon_prefix}*.zip", release.tag))?;
    let version = local_asset
        .version
        .clone()
        .ok_or_else(|| format!("could not parse version from {}", local_asset.name))?;

    let installed = ensure_selected_package(
        layout,
        &version,
        DownloadAsset {
            url: local_asset.url,
            expected_sha256: None,
            label: local_asset.name,
        },
        DownloadAsset {
            url: addon_asset.url,
            expected_sha256: None,
            label: addon_asset.name,
        },
    )?;

    if let Some(message) =
        webview_runtime::ensure_for_current_platform(layout, Some(&release.assets))?
    {
        println!("{message}");
    }

    Ok(installed)
}

fn ensure_selected_package(
    layout: &AppLayout,
    version: &str,
    local_asset: DownloadAsset<'_>,
    addon_asset: DownloadAsset<'_>,
) -> Result<InstalledPackage, String> {
    if package_complete(layout, version) {
        write_manifest(layout, version)?;
        return Ok(InstalledPackage {
            version: version.to_string(),
            addon_dir: addon_dir(layout, version),
        });
    }

    let temp_dir = create_temp_dir()?;
    let result = install_from_assets(layout, &temp_dir, version, local_asset, addon_asset);
    let _ = fs::remove_dir_all(&temp_dir);
    result
}

fn package_complete(layout: &AppLayout, version: &str) -> bool {
    let version_dir = layout.versions_dir.join(version);
    version_dir
        .join(binary_name("fennara-mcp-runtime"))
        .is_file()
        && version_dir
            .join(binary_name("fennara-daemon-runtime"))
            .is_file()
        && addon_dir(layout, version)
            .join("fennara.gdextension")
            .is_file()
        && addon_dir(layout, version).join("VERSION").is_file()
}

fn addon_dir(layout: &AppLayout, version: &str) -> PathBuf {
    layout
        .versions_dir
        .join(version)
        .join("addon")
        .join("addons")
        .join("fennara")
}

fn install_from_assets(
    layout: &AppLayout,
    temp_dir: &Path,
    version: &str,
    local_asset: DownloadAsset<'_>,
    addon_asset: DownloadAsset<'_>,
) -> Result<InstalledPackage, String> {
    let local_dir = temp_dir.join("local");
    let addon_stage_dir = temp_dir.join("addon");
    download_zip_to_dir(&local_asset, &local_dir)?;
    download_zip_to_dir(&addon_asset, &addon_stage_dir)?;

    let package_version = fs::read_to_string(local_dir.join("VERSION"))
        .map_err(|err| format!("downloaded local package is missing VERSION: {err}"))?
        .trim()
        .to_string();
    if package_version != version {
        return Err(format!(
            "downloaded package version {package_version} did not match expected {version}"
        ));
    }

    let version_dir = layout.versions_dir.join(version);
    let addon_target = version_dir.join("addon");
    fs::create_dir_all(&version_dir)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&version_dir)))?;

    copy_existing_launcher(
        &local_dir.join("bin").join(binary_name("fennara-mcp")),
        &layout.bin_dir.join(binary_name("fennara-mcp")),
    )?;
    copy_existing_launcher(
        &local_dir.join("bin").join(binary_name("fennara-daemon")),
        &layout.bin_dir.join(binary_name("fennara-daemon")),
    )?;
    copy_file(
        &local_dir
            .join("bin")
            .join(binary_name("fennara-mcp-runtime")),
        &version_dir.join(binary_name("fennara-mcp-runtime")),
    )?;
    copy_file(
        &local_dir
            .join("bin")
            .join(binary_name("fennara-daemon-runtime")),
        &version_dir.join(binary_name("fennara-daemon-runtime")),
    )?;

    if addon_target.exists() {
        fs::remove_dir_all(&addon_target).map_err(|err| {
            format!(
                "failed to remove old addon package at {}: {err}",
                display_path(&addon_target)
            )
        })?;
    }
    copy_dir(&addon_stage_dir, &addon_target)?;
    write_manifest(layout, version)?;

    Ok(InstalledPackage {
        version: version.to_string(),
        addon_dir: addon_dir(layout, version),
    })
}

fn write_manifest(layout: &AppLayout, version: &str) -> Result<(), String> {
    let manifest = serde_json::json!({
        "version": version,
        "mcp_runtime": format!("versions/{version}/{}", binary_name("fennara-mcp-runtime")),
        "daemon_runtime": format!("versions/{version}/{}", binary_name("fennara-daemon-runtime")),
        "addon": format!("versions/{version}/addon/addons/fennara"),
    });
    let raw = serde_json::to_string_pretty(&manifest)
        .map_err(|err| format!("failed to write manifest json: {err}"))?;
    fs::write(&layout.current_manifest_path, format!("{raw}\n")).map_err(|err| {
        format!(
            "failed to write {}: {err}",
            display_path(&layout.current_manifest_path)
        )
    })
}

struct Asset<'a> {
    name: &'a str,
    url: &'a str,
    version: Option<String>,
}

struct Release {
    tag: String,
    assets: Value,
}

impl Release {
    fn asset(&self, prefix: &str) -> Option<Asset<'_>> {
        self.asset_by_prefix_suffix(prefix, ".zip")
    }

    fn manifest_asset(&self) -> Option<Asset<'_>> {
        self.asset_by_prefix_suffix("fennara-release-manifest-v", ".json")
    }

    fn asset_by_name(&self, expected_name: &str) -> Option<Asset<'_>> {
        self.assets.as_array()?.iter().find_map(|asset| {
            let name = asset.get("name")?.as_str()?;
            if name != expected_name {
                return None;
            }
            let url = asset.get("browser_download_url")?.as_str()?;
            Some(Asset {
                name,
                url,
                version: version_from_asset_name(name),
            })
        })
    }

    fn asset_by_prefix_suffix(&self, prefix: &str, suffix: &str) -> Option<Asset<'_>> {
        self.assets.as_array()?.iter().find_map(|asset| {
            let name = asset.get("name")?.as_str()?;
            if !name.starts_with(prefix) || !name.ends_with(suffix) {
                return None;
            }
            let url = asset.get("browser_download_url")?.as_str()?;
            Some(Asset {
                name,
                url,
                version: version_from_asset_name(name),
            })
        })
    }
}

struct DownloadAsset<'a> {
    url: &'a str,
    expected_sha256: Option<&'a str>,
    label: &'a str,
}

fn fetch_release(version: &str) -> Result<Release, String> {
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

fn download_zip_to_dir(asset: &DownloadAsset<'_>, target: &Path) -> Result<(), String> {
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

fn download_bytes(url: &str, label: &str) -> Result<Vec<u8>, String> {
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

fn create_temp_dir() -> Result<PathBuf, String> {
    let path = env::temp_dir().join(format!(
        "fennara-package-{}-{}",
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

fn copy_file(source: &Path, target: &Path) -> Result<(), String> {
    if !source.is_file() {
        return Err(format!("missing package file: {}", display_path(source)));
    }
    if let Some(parent) = target.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("failed to create {}: {err}", display_path(parent)))?;
    }
    fs::copy(source, target).map_err(|err| {
        format!(
            "failed to copy {} to {}: {err}",
            display_path(source),
            display_path(target)
        )
    })?;
    Ok(())
}

fn copy_existing_launcher(source: &Path, target: &Path) -> Result<(), String> {
    if !source.is_file() {
        return Err(format!("missing package file: {}", display_path(source)));
    }

    if !target.exists() {
        return copy_file(source, target);
    }

    match copy_file(source, target) {
        Ok(()) => Ok(()),
        Err(error) => {
            println!(
                "warning: kept existing launcher because it could not be replaced: {}",
                display_path(target)
            );
            println!("warning: {error}");
            Ok(())
        }
    }
}

fn copy_dir(source: &Path, target: &Path) -> Result<(), String> {
    fs::create_dir_all(target)
        .map_err(|err| format!("failed to create {}: {err}", display_path(target)))?;

    for entry in fs::read_dir(source)
        .map_err(|err| format!("failed to read {}: {err}", display_path(source)))?
    {
        let entry = entry
            .map_err(|err| format!("failed to read an entry in {}: {err}", display_path(source)))?;
        let source_path = entry.path();
        let target_path = target.join(entry.file_name());

        if source_path.is_dir() {
            copy_dir(&source_path, &target_path)?;
        } else {
            copy_file(&source_path, &target_path)?;
        }
    }

    Ok(())
}
