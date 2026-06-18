use serde_json::Value;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

const VERSION: &str = env!("CARGO_PKG_VERSION");

fn main() {
    let status = match run(env::args().skip(1).collect()) {
        Ok(()) => 0,
        Err(error) => {
            eprintln!("error: {error}");
            1
        }
    };
    std::process::exit(status);
}

fn run(args: Vec<String>) -> Result<(), String> {
    match args.first().map(String::as_str) {
        None | Some("-h") | Some("--help") | Some("help") => {
            print_help();
            Ok(())
        }
        Some("-V") | Some("--version") | Some("version") => {
            println!("fennara {VERSION}");
            Ok(())
        }
        Some("doctor") => doctor(args.iter().skip(1).map(String::as_str).collect()),
        Some(command) => Err(format!("unknown command: {command}")),
    }
}

fn print_help() {
    println!(
        "\
Fennara CLI {VERSION}

Usage:
  fennara doctor [--repair]
  fennara --version
  fennara --help

Commands:
  doctor    Inspect the local Fennara install layout

Options:
  --repair  Create missing base app-data directories during doctor
"
    );
}

fn doctor(args: Vec<&str>) -> Result<(), String> {
    let repair = args.contains(&"--repair");
    for arg in args {
        if arg != "--repair" {
            return Err(format!("unknown doctor option: {arg}"));
        }
    }

    let layout = AppLayout::detect()?;
    println!("Fennara doctor");
    println!("version: {VERSION}");
    println!("platform: {} {}", platform_name(), arch_name());
    println!("app_dir: {}", display_path(&layout.app_dir));

    if repair {
        layout.ensure_base_dirs()?;
    }

    report_dir("bin", &layout.bin_dir);
    report_dir("versions", &layout.versions_dir);
    report_dir("cache", &layout.cache_dir);
    report_dir("logs", &layout.logs_dir);
    report_dir("tools", &layout.tools_dir);
    report_file("current manifest", &layout.current_manifest_path);
    report_file(
        "fennara-mcp shim",
        &layout.bin_dir.join(binary_name("fennara-mcp")),
    );
    report_file(
        "fennara-daemon shim",
        &layout.bin_dir.join(binary_name("fennara-daemon")),
    );

    match read_current_manifest(&layout.current_manifest_path) {
        Ok(Some(manifest)) => report_manifest(&layout.app_dir, &manifest),
        Ok(None) => println!("current version: not installed yet"),
        Err(error) => println!("current manifest: invalid ({error})"),
    }

    println!(
        "release asset hint: fennara-local-{}-{}-v{VERSION}.zip",
        platform_name(),
        arch_name()
    );
    println!(
        "addon asset hint: fennara-addon-{}-{}-v{VERSION}.zip",
        platform_name(),
        arch_name()
    );

    if repair {
        println!("repair: base directories ensured");
    } else {
        println!("repair: not run; use `fennara doctor --repair` to create base directories");
    }

    Ok(())
}

struct AppLayout {
    app_dir: PathBuf,
    bin_dir: PathBuf,
    versions_dir: PathBuf,
    cache_dir: PathBuf,
    logs_dir: PathBuf,
    tools_dir: PathBuf,
    current_manifest_path: PathBuf,
}

impl AppLayout {
    fn detect() -> Result<Self, String> {
        let app_dir = app_dir()?;
        Ok(Self {
            bin_dir: app_dir.join("bin"),
            versions_dir: app_dir.join("versions"),
            cache_dir: app_dir.join("cache"),
            logs_dir: app_dir.join("logs"),
            tools_dir: app_dir.join("tools"),
            current_manifest_path: app_dir.join("current.json"),
            app_dir,
        })
    }

    fn ensure_base_dirs(&self) -> Result<(), String> {
        for dir in [
            &self.app_dir,
            &self.bin_dir,
            &self.versions_dir,
            &self.cache_dir,
            &self.logs_dir,
            &self.tools_dir,
        ] {
            fs::create_dir_all(dir)
                .map_err(|err| format!("failed to create {}: {err}", display_path(dir)))?;
        }
        Ok(())
    }
}

fn read_current_manifest(path: &Path) -> Result<Option<Value>, String> {
    if !path.is_file() {
        return Ok(None);
    }

    let raw = fs::read_to_string(path)
        .map_err(|err| format!("failed to read {}: {err}", display_path(path)))?;
    serde_json::from_str(&raw)
        .map(Some)
        .map_err(|err| format!("failed to parse {}: {err}", display_path(path)))
}

fn report_manifest(app_dir: &Path, manifest: &Value) {
    let version = manifest
        .get("version")
        .and_then(Value::as_str)
        .unwrap_or("unknown");
    println!("current version: {version}");

    for field in ["mcp_runtime", "daemon_runtime", "cli_runtime", "addon"] {
        if let Some(path) = manifest.get(field).and_then(Value::as_str) {
            let resolved = resolve_manifest_path(app_dir, path);
            report_file(field, &resolved);
        }
    }
}

fn resolve_manifest_path(app_dir: &Path, value: &str) -> PathBuf {
    let path = PathBuf::from(value);
    if path.is_absolute() {
        path
    } else {
        app_dir.join(path)
    }
}

fn report_dir(label: &str, path: &Path) {
    println!(
        "{label}: {} ({})",
        display_path(path),
        if path.is_dir() { "ok" } else { "missing" }
    );
}

fn report_file(label: &str, path: &Path) {
    println!(
        "{label}: {} ({})",
        display_path(path),
        if path.is_file() { "ok" } else { "missing" }
    );
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

fn binary_name(base_name: &str) -> String {
    if cfg!(target_os = "windows") {
        format!("{base_name}.exe")
    } else {
        base_name.to_string()
    }
}

fn platform_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "macos"
    } else {
        "linux"
    }
}

fn arch_name() -> &'static str {
    if cfg!(target_arch = "aarch64") {
        "arm64"
    } else if cfg!(target_arch = "x86_64") {
        "x86_64"
    } else {
        env::consts::ARCH
    }
}

fn display_path(path: &Path) -> String {
    path.display().to_string()
}
