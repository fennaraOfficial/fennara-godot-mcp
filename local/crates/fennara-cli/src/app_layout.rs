use serde_json::Value;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

pub struct AppLayout {
    pub app_dir: PathBuf,
    pub bin_dir: PathBuf,
    pub versions_dir: PathBuf,
    pub cache_dir: PathBuf,
    pub logs_dir: PathBuf,
    pub tools_dir: PathBuf,
    pub webview_dir: PathBuf,
    pub current_manifest_path: PathBuf,
}

impl AppLayout {
    pub fn detect() -> Result<Self, String> {
        let app_dir = app_dir()?;
        Ok(Self {
            bin_dir: app_dir.join("bin"),
            versions_dir: app_dir.join("versions"),
            cache_dir: app_dir.join("cache"),
            logs_dir: app_dir.join("logs"),
            tools_dir: app_dir.join("tools"),
            webview_dir: app_dir.join("webview"),
            current_manifest_path: app_dir.join("current.json"),
            app_dir,
        })
    }

    pub fn ensure_base_dirs(&self) -> Result<(), String> {
        for dir in [
            &self.app_dir,
            &self.bin_dir,
            &self.versions_dir,
            &self.cache_dir,
            &self.logs_dir,
            &self.tools_dir,
            &self.webview_dir,
        ] {
            fs::create_dir_all(dir)
                .map_err(|err| format!("failed to create {}: {err}", display_path(dir)))?;
        }
        Ok(())
    }

    pub fn linux_cef_runtime_dir(&self, platform_arch: &str, version: &str) -> PathBuf {
        self.webview_dir
            .join("cef")
            .join(platform_arch)
            .join(version)
    }

    pub fn webview_profile_root(&self) -> PathBuf {
        self.cache_dir.join("webview").join("profiles")
    }

    pub fn webview_log_root(&self) -> PathBuf {
        self.logs_dir.join("webview")
    }
}

pub fn read_current_manifest(path: &Path) -> Result<Option<Value>, String> {
    if !path.is_file() {
        return Ok(None);
    }

    let raw = fs::read_to_string(path)
        .map_err(|err| format!("failed to read {}: {err}", display_path(path)))?;
    serde_json::from_str(&raw)
        .map(Some)
        .map_err(|err| format!("failed to parse {}: {err}", display_path(path)))
}

pub fn resolve_manifest_path(app_dir: &Path, value: &str) -> PathBuf {
    let path = PathBuf::from(value);
    if path.is_absolute() {
        path
    } else {
        app_dir.join(path)
    }
}

pub fn binary_name(base_name: &str) -> String {
    if cfg!(target_os = "windows") {
        format!("{base_name}.exe")
    } else {
        base_name.to_string()
    }
}

pub fn platform_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "macos"
    } else {
        "linux"
    }
}

pub fn arch_name() -> &'static str {
    if cfg!(target_arch = "aarch64") {
        "arm64"
    } else if cfg!(target_arch = "x86_64") {
        "x86_64"
    } else {
        env::consts::ARCH
    }
}

pub fn display_path(path: &Path) -> String {
    path.display().to_string()
}

#[cfg(target_os = "windows")]
fn app_dir() -> Result<PathBuf, String> {
    env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .map(|path| path.join("Fennara"))
        .ok_or_else(|| "LOCALAPPDATA is not set".to_string())
}

#[cfg(target_os = "macos")]
fn app_dir() -> Result<PathBuf, String> {
    home_dir()
        .map(|path| {
            path.join("Library")
                .join("Application Support")
                .join("Fennara")
        })
        .ok_or_else(|| "home directory is not available".to_string())
}

#[cfg(all(unix, not(target_os = "macos")))]
fn app_dir() -> Result<PathBuf, String> {
    home_dir()
        .map(|path| path.join(".local").join("share").join("fennara"))
        .ok_or_else(|| "home directory is not available".to_string())
}

#[cfg(not(target_os = "windows"))]
fn home_dir() -> Option<PathBuf> {
    env::var_os("HOME").map(PathBuf::from)
}
