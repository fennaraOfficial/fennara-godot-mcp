use crate::app_layout::{AppLayout, display_path, platform_name};
use crate::webview_runtime;
use std::path::{Path, PathBuf};
#[cfg(target_os = "windows")]
use std::process::Command;

const WEBVIEW2_URL: &str = "https://developer.microsoft.com/microsoft-edge/webview2/";

pub fn warn_for_current_platform() -> Result<(), String> {
    match platform_name() {
        "windows" => warn_windows_webview2(),
        "macos" => warn_macos_webkit(),
        "linux" => Ok(()),
        _ => Ok(()),
    }
}

pub fn report_for_doctor(layout: &AppLayout, repair: bool) -> Result<(), String> {
    match platform_name() {
        "windows" => {
            println!("webview runtimes: {}", display_path(&layout.webview_dir));
            report_windows_webview2()
        }
        "macos" => {
            println!("webview runtimes: {}", display_path(&layout.webview_dir));
            let status = macos_webkit_status();
            println!("macOS WebKit framework: {}", status.label());
            if let Some(path) = status.path {
                println!("macOS WebKit path: {}", display_path(&path));
            }
            Ok(())
        }
        "linux" => webview_runtime::report_for_doctor(layout, repair),
        _ => {
            println!("webview prerequisite: unsupported platform");
            Ok(())
        }
    }
}

#[cfg(target_os = "windows")]
fn warn_windows_webview2() -> Result<(), String> {
    let status = windows_webview2_status();
    if status.found {
        return Ok(());
    }

    println!("warning: Microsoft Edge WebView2 Runtime was not detected.");
    println!(
        "warning: Fennara MCP tools will work, but the built-in Godot chat dock needs WebView2 on Windows."
    );
    println!("warning: Install WebView2 Evergreen Runtime, then restart Godot:");
    println!("warning: {WEBVIEW2_URL}");
    Ok(())
}

#[cfg(not(target_os = "windows"))]
fn warn_windows_webview2() -> Result<(), String> {
    Ok(())
}

#[cfg(target_os = "windows")]
fn report_windows_webview2() -> Result<(), String> {
    let status = windows_webview2_status();
    println!(
        "Windows WebView2 Runtime: {}",
        if status.found { "found" } else { "missing" }
    );
    if let Some(version) = status.version {
        println!("Windows WebView2 version: {version}");
    }
    if let Some(path) = status.path {
        println!("Windows WebView2 path: {}", display_path(&path));
    }
    if !status.found {
        println!("Windows WebView2 install: {WEBVIEW2_URL}");
        println!(
            "Windows WebView2 note: MCP tools still work; only the built-in Godot chat dock needs this runtime."
        );
    }
    Ok(())
}

#[cfg(not(target_os = "windows"))]
fn report_windows_webview2() -> Result<(), String> {
    Ok(())
}

#[cfg(target_os = "macos")]
fn warn_macos_webkit() -> Result<(), String> {
    let status = macos_webkit_status();
    if status.found {
        return Ok(());
    }

    println!("warning: macOS WebKit.framework was not detected.");
    println!(
        "warning: Fennara MCP tools will work, but the built-in Godot chat dock needs WKWebView on macOS."
    );
    println!(
        "warning: WebKit is normally included with macOS; restart Godot or check the macOS installation."
    );
    Ok(())
}

#[cfg(not(target_os = "macos"))]
fn warn_macos_webkit() -> Result<(), String> {
    Ok(())
}

#[cfg(target_os = "windows")]
struct WindowsWebView2Status {
    found: bool,
    version: Option<String>,
    path: Option<PathBuf>,
}

#[cfg(target_os = "windows")]
fn windows_webview2_status() -> WindowsWebView2Status {
    if let Some((path, version)) = find_webview2_on_disk() {
        return WindowsWebView2Status {
            found: true,
            version,
            path: Some(path),
        };
    }

    if let Some(version) = find_webview2_in_registry() {
        return WindowsWebView2Status {
            found: true,
            version: Some(version),
            path: None,
        };
    }

    WindowsWebView2Status {
        found: false,
        version: None,
        path: None,
    }
}

#[cfg(target_os = "windows")]
fn find_webview2_on_disk() -> Option<(PathBuf, Option<String>)> {
    for root in webview2_roots() {
        let application_dir = root
            .join("Microsoft")
            .join("EdgeWebView")
            .join("Application");
        if let Some(found) = find_versioned_webview2_exe(&application_dir) {
            return Some(found);
        }
    }
    None
}

#[cfg(target_os = "windows")]
fn webview2_roots() -> Vec<PathBuf> {
    let mut roots = Vec::new();
    for var in ["ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"] {
        if let Some(value) = std::env::var_os(var) {
            roots.push(PathBuf::from(value));
        }
    }
    roots
}

#[cfg(target_os = "windows")]
fn find_versioned_webview2_exe(application_dir: &Path) -> Option<(PathBuf, Option<String>)> {
    let entries = std::fs::read_dir(application_dir).ok()?;
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        let exe = path.join("msedgewebview2.exe");
        if exe.is_file() {
            let version = path
                .file_name()
                .and_then(|name| name.to_str())
                .map(str::to_string);
            return Some((exe, version));
        }
    }
    None
}

#[cfg(target_os = "windows")]
fn find_webview2_in_registry() -> Option<String> {
    for key in [
        r"HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients",
        r"HKCU\Software\Microsoft\EdgeUpdate\Clients",
    ] {
        let output = Command::new("reg")
            .args(["query", key, "/s"])
            .output()
            .ok()?;
        if !output.status.success() {
            continue;
        }
        let text = String::from_utf8_lossy(&output.stdout);
        if !text.to_ascii_lowercase().contains("webview2") {
            continue;
        }
        if let Some(version) = parse_registry_version(&text) {
            return Some(version);
        }
        return Some("installed".to_string());
    }
    None
}

#[cfg(target_os = "windows")]
fn parse_registry_version(text: &str) -> Option<String> {
    for line in text.lines() {
        let mut parts = line.split_whitespace();
        if parts.next()? != "pv" {
            continue;
        }
        let _kind = parts.next()?;
        let version = parts.next()?;
        if !version.is_empty() {
            return Some(version.to_string());
        }
    }
    None
}

struct FrameworkStatus {
    found: bool,
    path: Option<PathBuf>,
}

impl FrameworkStatus {
    fn label(&self) -> &'static str {
        if self.found { "found" } else { "missing" }
    }
}

fn macos_webkit_status() -> FrameworkStatus {
    let candidates = [
        Path::new("/System/Library/Frameworks/WebKit.framework"),
        Path::new("/System/Library/Frameworks/WebKit.framework/WebKit"),
    ];
    for path in candidates {
        if path.exists() {
            return FrameworkStatus {
                found: true,
                path: Some(path.to_path_buf()),
            };
        }
    }
    FrameworkStatus {
        found: false,
        path: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn framework_status_labels_are_stable() {
        assert_eq!(
            FrameworkStatus {
                found: true,
                path: None,
            }
            .label(),
            "found"
        );
        assert_eq!(
            FrameworkStatus {
                found: false,
                path: None,
            }
            .label(),
            "missing"
        );
    }
}
