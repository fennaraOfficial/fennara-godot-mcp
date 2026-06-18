use crate::app_layout::{AppLayout, arch_name, binary_name, display_path, platform_name};
use serde_json::Value;
use std::env;
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use zip::ZipArchive;

const REPO: &str = "fennaraOfficial/fennara-godot-mcp";

pub fn run(args: Vec<&str>) -> Result<(), String> {
    let options = UpdateOptions::parse(args)?;
    let layout = AppLayout::detect()?;
    layout.ensure_base_dirs()?;

    let release = fetch_release(&options.version)?;
    let local_asset_prefix = format!("fennara-local-{}-{}-v", platform_name(), arch_name());
    let addon_asset_prefix = format!("fennara-addon-{}-{}-v", platform_name(), arch_name());
    let local_asset = release.asset_url(&local_asset_prefix).ok_or_else(|| {
        format!(
            "release {} is missing {local_asset_prefix}*.zip",
            release.tag
        )
    })?;
    let addon_asset = release.asset_url(&addon_asset_prefix).ok_or_else(|| {
        format!(
            "release {} is missing {addon_asset_prefix}*.zip",
            release.tag
        )
    })?;

    let temp_dir = create_temp_dir()?;
    let result = update_from_assets(&layout, &temp_dir, local_asset, addon_asset);
    let _ = fs::remove_dir_all(&temp_dir);
    result
}

struct UpdateOptions {
    version: String,
}

impl UpdateOptions {
    fn parse(args: Vec<&str>) -> Result<Self, String> {
        let mut version = "latest".to_string();
        let mut index = 0;

        while index < args.len() {
            match args[index] {
                "--version" => {
                    index += 1;
                    version = args
                        .get(index)
                        .ok_or_else(|| "--version requires a value".to_string())?
                        .to_string();
                }
                arg if arg.starts_with("--version=") => {
                    version = arg.trim_start_matches("--version=").to_string();
                }
                "-h" | "--help" => {
                    print_help();
                    return Err("".to_string());
                }
                other => return Err(format!("unknown update option: {other}")),
            }
            index += 1;
        }

        Ok(Self { version })
    }
}

fn print_help() {
    println!(
        "\
Update the installed Fennara local runtime and addon package.

Usage:
  fennara update
  fennara update --version 0.2.8
"
    );
}

fn update_from_assets(
    layout: &AppLayout,
    temp_dir: &Path,
    local_asset: &str,
    addon_asset: &str,
) -> Result<(), String> {
    let local_dir = temp_dir.join("local");
    let addon_dir = temp_dir.join("addon");
    download_zip_to_dir(local_asset, &local_dir)?;
    download_zip_to_dir(addon_asset, &addon_dir)?;

    let package_version = fs::read_to_string(local_dir.join("VERSION"))
        .map_err(|err| format!("downloaded local package is missing VERSION: {err}"))?
        .trim()
        .to_string();
    if package_version.is_empty() {
        return Err("downloaded local package has an empty VERSION".to_string());
    }

    let version_dir = layout.versions_dir.join(&package_version);
    let addon_target = version_dir.join("addon");
    fs::create_dir_all(&version_dir)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&version_dir)))?;

    copy_runtime_binaries(&local_dir, &layout.bin_dir, &version_dir)?;
    if addon_target.exists() {
        fs::remove_dir_all(&addon_target).map_err(|err| {
            format!(
                "failed to remove old addon package at {}: {err}",
                display_path(&addon_target)
            )
        })?;
    }
    copy_dir(&addon_dir, &addon_target)?;

    write_manifest(layout, &package_version)?;

    println!("Updated Fennara packages");
    println!("version: {package_version}");
    println!(
        "addon: {}",
        display_path(&addon_target.join("addons").join("fennara"))
    );
    if cfg!(target_os = "windows") {
        println!(
            "cli: Windows keeps the running fennara.exe in place; rerun the bootstrap installer when the CLI binary itself must update"
        );
    }

    Ok(())
}

fn copy_runtime_binaries(
    local_dir: &Path,
    bin_dir: &Path,
    version_dir: &Path,
) -> Result<(), String> {
    for name in [binary_name("fennara-mcp"), binary_name("fennara-daemon")] {
        copy_file(&local_dir.join("bin").join(&name), &bin_dir.join(&name))?;
    }

    let cli_source = local_dir.join("bin").join(binary_name("fennara"));
    let cli_target = bin_dir.join(binary_name("fennara"));
    if !cfg!(target_os = "windows") || !cli_target.exists() {
        copy_file(&cli_source, &cli_target)?;
    }

    for name in [
        binary_name("fennara-mcp-runtime"),
        binary_name("fennara-daemon-runtime"),
    ] {
        copy_file(&local_dir.join("bin").join(&name), &version_dir.join(&name))?;
    }

    Ok(())
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

struct Release {
    tag: String,
    assets: Value,
}

impl Release {
    fn asset_url(&self, prefix: &str) -> Option<&str> {
        self.assets.as_array()?.iter().find_map(|asset| {
            let name = asset.get("name")?.as_str()?;
            if name.starts_with(prefix) && name.ends_with(".zip") {
                asset.get("browser_download_url")?.as_str()
            } else {
                None
            }
        })
    }
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

fn download_zip_to_dir(url: &str, target: &Path) -> Result<(), String> {
    fs::create_dir_all(target)
        .map_err(|err| format!("failed to create {}: {err}", display_path(target)))?;
    let response = ureq::get(url)
        .set("User-Agent", "fennara-cli")
        .call()
        .map_err(|err| format!("failed to download {url}: {err}"))?;
    let mut bytes = Vec::new();
    response
        .into_reader()
        .read_to_end(&mut bytes)
        .map_err(|err| format!("failed to read download from {url}: {err}"))?;

    let cursor = Cursor::new(bytes);
    let mut archive =
        ZipArchive::new(cursor).map_err(|err| format!("failed to open downloaded zip: {err}"))?;
    archive
        .extract(target)
        .map_err(|err| format!("failed to extract zip into {}: {err}", display_path(target)))
}

fn create_temp_dir() -> Result<PathBuf, String> {
    let path = env::temp_dir().join(format!(
        "fennara-update-{}-{}",
        std::process::id(),
        chrono_like_stamp()
    ));
    fs::create_dir_all(&path)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&path)))?;
    Ok(path)
}

fn chrono_like_stamp() -> u128 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or(0)
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
