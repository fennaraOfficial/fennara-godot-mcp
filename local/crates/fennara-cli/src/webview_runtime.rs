use crate::app_layout::{AppLayout, arch_name, display_path, platform_name};
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::env;
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use zip::ZipArchive;

const LINUX_CEF_MANIFEST: &str = include_str!("../../../webview-runtimes/linux-cef.json");
const STALE_PROFILE_AGE: Duration = Duration::from_secs(7 * 24 * 60 * 60);

pub fn ensure_for_current_platform(
    layout: &AppLayout,
    release_assets: Option<&Value>,
) -> Result<Option<String>, String> {
    if platform_name() != "linux" {
        return Ok(None);
    }

    let manifest = parse_manifest()?;
    ensure_linux_cef_manifest(layout, &manifest, release_assets)
}

pub fn ensure_from_release_manifest(
    layout: &AppLayout,
    runtimes: &[Value],
    release_assets: &Value,
) -> Result<Vec<String>, String> {
    let mut messages = Vec::new();

    for runtime in runtimes {
        if !runtime_matches_current_platform(runtime) {
            continue;
        }

        let kind = runtime
            .get("kind")
            .or_else(|| runtime.get("runtime"))
            .and_then(Value::as_str)
            .ok_or_else(|| "release manifest shared runtime is missing kind".to_string())?;
        match kind {
            "cef" | "linux-cef" if platform_name() == "linux" => {
                if let Some(message) =
                    ensure_linux_cef_manifest(layout, runtime, Some(release_assets))?
                {
                    messages.push(message);
                }
            }
            "cef" | "linux-cef" => {}
            _ => {
                return Err(format!(
                    "release manifest declares unsupported shared runtime kind {kind}"
                ));
            }
        }
    }

    Ok(messages)
}

fn ensure_linux_cef_manifest(
    layout: &AppLayout,
    manifest: &Value,
    release_assets: Option<&Value>,
) -> Result<Option<String>, String> {
    fs::create_dir_all(&layout.webview_dir).map_err(|err| {
        format!(
            "failed to create webview runtime dir {}: {err}",
            display_path(&layout.webview_dir)
        )
    })?;

    let platform_arch = manifest_string(manifest, "platform_arch")?;
    if platform_arch != current_linux_platform_arch() {
        return Ok(Some(format!(
            "webview runtime: CEF manifest targets {platform_arch}; current target is {}",
            current_linux_platform_arch()
        )));
    }

    if !manifest
        .get("enabled")
        .and_then(Value::as_bool)
        .unwrap_or(false)
    {
        return Ok(Some(format!(
            "webview runtime: Linux CEF asset is not selected yet; reserved shared layout is {}",
            display_path(
                &layout
                    .webview_dir
                    .join("cef")
                    .join(platform_arch)
                    .join("<cef-version>")
            )
        )));
    }

    let version = manifest_string(manifest, "version")?;
    if version.starts_with("TODO") || version.is_empty() {
        return Err(
            "Linux CEF runtime manifest is enabled but version is not finalized".to_string(),
        );
    }

    let target_dir = layout.linux_cef_runtime_dir(platform_arch, version);
    if runtime_complete(&target_dir, manifest) {
        write_current_runtime(&target_dir, manifest)?;
        return Ok(Some(format!(
            "webview runtime: Linux CEF runtime is installed at {}",
            display_path(&target_dir)
        )));
    }

    let archive = manifest
        .get("archive")
        .and_then(Value::as_object)
        .ok_or_else(|| "Linux CEF runtime manifest is missing archive config".to_string())?;
    let format = archive
        .get("format")
        .and_then(Value::as_str)
        .unwrap_or("zip");
    if format != "zip" {
        return Err(format!(
            "Linux CEF runtime archive format {format} is not supported yet"
        ));
    }

    let sha256 = archive
        .get("sha256")
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "Linux CEF runtime manifest is enabled but sha256 is not set".to_string())?;
    let url = archive_url(
        archive.get("url").and_then(Value::as_str),
        archive.get("name").and_then(Value::as_str),
        release_assets,
    )?;

    let temp_dir = create_temp_dir()?;
    let result = download_and_install_zip(&url, sha256, &temp_dir, &target_dir, manifest);
    let _ = fs::remove_dir_all(&temp_dir);
    result?;

    Ok(Some(format!(
        "webview runtime: installed Linux CEF runtime at {}",
        display_path(&target_dir)
    )))
}

pub fn report_for_doctor(layout: &AppLayout, repair: bool) -> Result<(), String> {
    println!("webview runtimes: {}", display_path(&layout.webview_dir));
    if platform_name() != "linux" {
        return Ok(());
    }

    let manifest = parse_manifest()?;
    let platform_arch = manifest_string(&manifest, "platform_arch")?;
    let version = manifest_string(&manifest, "version")?;
    let runtime_dir = if version.starts_with("TODO") {
        layout
            .webview_dir
            .join("cef")
            .join(platform_arch)
            .join("<cef-version>")
    } else {
        layout.linux_cef_runtime_dir(platform_arch, version)
    };
    println!("linux cef runtime: {}", display_path(&runtime_dir));
    println!(
        "linux cef runtime status: {}",
        if runtime_complete(&runtime_dir, &manifest) {
            "installed"
        } else if manifest
            .get("enabled")
            .and_then(Value::as_bool)
            .unwrap_or(false)
        {
            "missing"
        } else {
            "asset not selected yet"
        }
    );
    println!(
        "linux cef writable profiles: {}",
        display_path(&layout.webview_profile_root().join("cef"))
    );
    println!(
        "linux cef logs: {}",
        display_path(&layout.webview_log_root().join("cef"))
    );
    if repair {
        let removed = cleanup_stale_process_dirs(&layout.webview_profile_root().join("cef"))?;
        println!("linux cef stale profile cleanup: removed {removed}");
        if runtime_complete(&runtime_dir, &manifest) {
            write_current_runtime(&runtime_dir, &manifest)?;
            println!("linux cef current marker: repaired");
        }
    }
    Ok(())
}

fn parse_manifest() -> Result<Value, String> {
    serde_json::from_str(LINUX_CEF_MANIFEST)
        .map_err(|err| format!("failed to parse Linux CEF runtime manifest: {err}"))
}

fn manifest_string<'a>(manifest: &'a Value, field: &str) -> Result<&'a str, String> {
    manifest
        .get(field)
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| format!("Linux CEF runtime manifest is missing {field}"))
}

fn current_linux_platform_arch() -> &'static str {
    match arch_name() {
        "x86_64" => "linux-x64",
        "arm64" => "linux-arm64",
        _ => "linux-unknown",
    }
}

fn runtime_matches_current_platform(manifest: &Value) -> bool {
    manifest
        .get("platform")
        .and_then(Value::as_str)
        .map(|platform| platform == platform_name())
        .unwrap_or(true)
        && manifest
            .get("arch")
            .and_then(Value::as_str)
            .map(|arch| arch == arch_name())
            .unwrap_or(true)
}

fn runtime_complete(runtime_dir: &Path, manifest: &Value) -> bool {
    runtime_dir.join("fennara-cef-runtime.json").is_file()
        && runtime_marker_matches(runtime_dir, manifest)
        && manifest
            .get("version")
            .and_then(Value::as_str)
            .map(|version| runtime_dir.ends_with(version))
            .unwrap_or(false)
        && required_files_present(runtime_dir, manifest).is_ok()
}

fn runtime_marker_matches(runtime_dir: &Path, manifest: &Value) -> bool {
    let marker_path = runtime_dir.join("fennara-cef-runtime.json");
    let Ok(raw) = fs::read_to_string(marker_path) else {
        return false;
    };
    let Ok(marker) = serde_json::from_str::<Value>(&raw) else {
        return false;
    };

    marker_string(&marker, "version") == manifest.get("version").and_then(Value::as_str)
        && marker_string(&marker, "platform_arch")
            == manifest.get("platform_arch").and_then(Value::as_str)
        && archive_sha256(&marker) == archive_sha256(manifest)
}

fn marker_string<'a>(marker: &'a Value, field: &str) -> Option<&'a str> {
    marker.get(field).and_then(Value::as_str)
}

fn archive_sha256(manifest: &Value) -> Option<&str> {
    manifest
        .get("archive")
        .and_then(|archive| archive.get("sha256"))
        .and_then(Value::as_str)
        .filter(|sha256| !sha256.is_empty())
}

fn archive_url(
    direct_url: Option<&str>,
    asset_name: Option<&str>,
    release_assets: Option<&Value>,
) -> Result<String, String> {
    if let Some(url) = direct_url.filter(|value| !value.is_empty()) {
        return Ok(url.to_string());
    }

    let name = asset_name
        .filter(|value| !value.is_empty())
        .ok_or_else(|| {
            "Linux CEF runtime manifest is enabled but archive URL/name is not set".to_string()
        })?;
    let assets = release_assets.and_then(Value::as_array).ok_or_else(|| {
        "release metadata did not include assets for Linux CEF runtime lookup".to_string()
    })?;

    assets
        .iter()
        .find_map(|asset| {
            let candidate = asset.get("name")?.as_str()?;
            if candidate != name {
                return None;
            }
            asset
                .get("browser_download_url")?
                .as_str()
                .map(str::to_string)
        })
        .ok_or_else(|| format!("release is missing Linux CEF runtime asset {name}"))
}

fn download_and_install_zip(
    url: &str,
    expected_sha256: &str,
    temp_dir: &Path,
    target_dir: &Path,
    manifest: &Value,
) -> Result<(), String> {
    let response = ureq::get(url)
        .set("User-Agent", "fennara-cli")
        .call()
        .map_err(|err| format!("failed to download Linux CEF runtime {url}: {err}"))?;
    let mut bytes = Vec::new();
    response
        .into_reader()
        .read_to_end(&mut bytes)
        .map_err(|err| format!("failed to read Linux CEF runtime download: {err}"))?;

    let actual_sha256 = format!("{:x}", Sha256::digest(&bytes));
    if !actual_sha256.eq_ignore_ascii_case(expected_sha256) {
        return Err(format!(
            "Linux CEF runtime sha256 mismatch: expected {expected_sha256}, got {actual_sha256}"
        ));
    }

    let extract_dir = temp_dir.join("extract");
    fs::create_dir_all(&extract_dir)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&extract_dir)))?;
    let mut archive = ZipArchive::new(Cursor::new(bytes))
        .map_err(|err| format!("failed to open Linux CEF runtime zip: {err}"))?;
    archive.extract(&extract_dir).map_err(|err| {
        format!(
            "failed to extract Linux CEF runtime zip into {}: {err}",
            display_path(&extract_dir)
        )
    })?;

    let platform_dir = target_dir.parent().ok_or_else(|| {
        format!(
            "Linux CEF runtime target has no platform directory: {}",
            display_path(target_dir)
        )
    })?;
    fs::create_dir_all(platform_dir)
        .map_err(|err| format!("failed to create {}: {err}", display_path(platform_dir)))?;
    let staging_dir = create_unique_sibling_dir(platform_dir, ".install-cef")?;

    let install_result = (|| {
        copy_dir(&extract_dir, &staging_dir)?;
        required_files_present(&staging_dir, manifest)?;
        let raw = serde_json::to_string_pretty(manifest)
            .map_err(|err| format!("failed to serialize Linux CEF runtime manifest: {err}"))?;
        fs::write(
            staging_dir.join("fennara-cef-runtime.json"),
            format!("{raw}\n"),
        )
        .map_err(|err| {
            format!(
                "failed to write Linux CEF runtime marker in {}: {err}",
                display_path(&staging_dir)
            )
        })?;
        publish_runtime_dir(&staging_dir, target_dir, manifest)
    })();

    if install_result.is_err() {
        let _ = fs::remove_dir_all(&staging_dir);
    }
    install_result?;
    write_current_runtime(target_dir, manifest)
}

fn publish_runtime_dir(
    staging_dir: &Path,
    target_dir: &Path,
    manifest: &Value,
) -> Result<(), String> {
    let mut moved_aside: Option<PathBuf> = None;
    if target_dir.exists() {
        if runtime_complete(target_dir, manifest) {
            fs::remove_dir_all(staging_dir).map_err(|err| {
                format!(
                    "failed to remove redundant Linux CEF staging dir {}: {err}",
                    display_path(staging_dir)
                )
            })?;
            return Ok(());
        }

        let parent = target_dir.parent().ok_or_else(|| {
            format!(
                "Linux CEF runtime target has no platform directory: {}",
                display_path(target_dir)
            )
        })?;
        let backup = create_unique_sibling_path(parent, ".corrupt-cef");
        fs::rename(target_dir, &backup).map_err(|err| {
            format!(
                "failed to move incomplete Linux CEF runtime {} aside to {}: {err}",
                display_path(target_dir),
                display_path(&backup)
            )
        })?;
        moved_aside = Some(backup);
    }

    match fs::rename(staging_dir, target_dir) {
        Ok(()) => {
            if let Some(backup) = moved_aside {
                let _ = fs::remove_dir_all(backup);
            }
        }
        Err(_) if target_dir.exists() && runtime_complete(target_dir, manifest) => {
            let _ = fs::remove_dir_all(staging_dir);
            if let Some(backup) = moved_aside {
                let _ = fs::remove_dir_all(backup);
            }
            return Ok(());
        }
        Err(err) => {
            if let Some(backup) = moved_aside {
                let _ = fs::rename(&backup, target_dir);
            }
            return Err(format!(
                "failed to publish Linux CEF runtime {} to {}: {err}",
                display_path(staging_dir),
                display_path(target_dir)
            ));
        }
    }
    let raw = serde_json::to_string_pretty(manifest)
        .map_err(|err| format!("failed to serialize Linux CEF runtime manifest: {err}"))?;
    let marker_path = target_dir.join("fennara-cef-runtime.json");
    fs::write(&marker_path, format!("{raw}\n")).map_err(|err| {
        format!(
            "failed to verify Linux CEF runtime marker in {}: {err}",
            display_path(&marker_path)
        )
    })?;
    required_files_present(target_dir, manifest)
}

fn create_temp_dir() -> Result<PathBuf, String> {
    let path = env::temp_dir().join(format!("fennara-cef-runtime-{}", unique_suffix()));
    fs::create_dir_all(&path)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&path)))?;
    Ok(path)
}

fn create_unique_sibling_dir(parent: &Path, prefix: &str) -> Result<PathBuf, String> {
    for attempt in 0..16 {
        let path = parent.join(format!("{prefix}-{}-{attempt}", unique_suffix()));
        match fs::create_dir(&path) {
            Ok(()) => return Ok(path),
            Err(err) if err.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(err) => {
                return Err(format!("failed to create {}: {err}", display_path(&path)));
            }
        }
    }
    Err(format!(
        "failed to allocate a unique directory under {}",
        display_path(parent)
    ))
}

fn create_unique_sibling_path(parent: &Path, prefix: &str) -> PathBuf {
    parent.join(format!("{prefix}-{}", unique_suffix()))
}

fn unique_suffix() -> String {
    let millis = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or(0);
    format!("{}-{millis}", std::process::id())
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
            if let Some(parent) = target_path.parent() {
                fs::create_dir_all(parent)
                    .map_err(|err| format!("failed to create {}: {err}", display_path(parent)))?;
            }
            fs::copy(&source_path, &target_path).map_err(|err| {
                format!(
                    "failed to copy {} to {}: {err}",
                    display_path(&source_path),
                    display_path(&target_path)
                )
            })?;
        }
    }

    Ok(())
}

fn required_files_present(runtime_dir: &Path, manifest: &Value) -> Result<(), String> {
    let required_files = manifest
        .get("required_files")
        .and_then(Value::as_array)
        .ok_or_else(|| "Linux CEF runtime manifest is missing required_files".to_string())?;

    for file in required_files {
        let relative = file
            .as_str()
            .filter(|value| !value.is_empty())
            .ok_or_else(|| {
                "Linux CEF runtime manifest has an invalid required_files entry".to_string()
            })?;
        let path = Path::new(relative);
        if path.is_absolute() || relative.contains("..") {
            return Err(format!(
                "Linux CEF runtime manifest has unsafe required file path {relative}"
            ));
        }

        let expected = runtime_dir.join(path);
        if !expected.is_file() {
            return Err(format!(
                "Linux CEF runtime is missing required file {}",
                display_path(&expected)
            ));
        }
    }

    Ok(())
}

fn write_current_runtime(target_dir: &Path, manifest: &Value) -> Result<(), String> {
    let version = manifest_string(manifest, "version")?;
    let platform_arch = manifest_string(manifest, "platform_arch")?;
    let current_path = target_dir
        .parent()
        .ok_or_else(|| {
            format!(
                "Linux CEF runtime target has no platform directory: {}",
                display_path(target_dir)
            )
        })?
        .join("current.json");
    let current = serde_json::json!({
        "runtime": "cef",
        "platform": "linux",
        "platform_arch": platform_arch,
        "version": version,
        "dir": version,
    });
    let raw = serde_json::to_string_pretty(&current)
        .map_err(|err| format!("failed to serialize Linux CEF current runtime: {err}"))?;
    let temp_path = current_path.with_file_name(format!("current.json.tmp-{}", unique_suffix()));
    fs::write(&temp_path, format!("{raw}\n")).map_err(|err| {
        format!(
            "failed to write Linux CEF current runtime marker {}: {err}",
            display_path(&temp_path)
        )
    })?;
    fs::rename(&temp_path, &current_path).map_err(|err| {
        let _ = fs::remove_file(&temp_path);
        format!(
            "failed to publish Linux CEF current runtime marker {}: {err}",
            display_path(&current_path)
        )
    })
}

fn cleanup_stale_process_dirs(root: &Path) -> Result<usize, String> {
    if !root.is_dir() {
        return Ok(0);
    }

    let now = SystemTime::now();
    let mut removed = 0;
    for entry in
        fs::read_dir(root).map_err(|err| format!("failed to read {}: {err}", display_path(root)))?
    {
        let entry = entry
            .map_err(|err| format!("failed to read an entry in {}: {err}", display_path(root)))?;
        let path = entry.path();
        let Some(name) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        if !path.is_dir() || !name.starts_with("godot-") {
            continue;
        }

        let age = entry
            .metadata()
            .ok()
            .and_then(|metadata| metadata.modified().ok())
            .and_then(|modified| now.duration_since(modified).ok())
            .unwrap_or(Duration::ZERO);
        if age < STALE_PROFILE_AGE {
            continue;
        }

        match fs::remove_dir_all(&path) {
            Ok(()) => removed += 1,
            Err(err) => println!(
                "warning: failed to remove stale Linux CEF profile {}: {err}",
                display_path(&path)
            ),
        }
    }
    Ok(removed)
}
