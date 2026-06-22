use crate::VERSION;
use crate::app_layout::{
    AppLayout, arch_name, binary_name, display_path, platform_name, read_current_manifest,
    resolve_manifest_path,
};
use crate::webview_runtime;
use serde_json::Value;
use std::path::Path;

pub fn run(args: Vec<&str>) -> Result<(), String> {
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
    report_dir("webview", &layout.webview_dir);
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
        "release local asset hint: fennara-release-local-{}-{}-v{VERSION}.zip",
        platform_name(),
        arch_name()
    );
    println!(
        "cli asset hint: fennara-cli-{}-{}-v{VERSION}.zip",
        platform_name(),
        arch_name()
    );
    println!("release addon asset hint: fennara-release-addon-v{VERSION}.zip");
    webview_runtime::report_for_doctor(&layout, repair)?;

    if repair {
        println!("repair: base directories ensured");
    } else {
        println!("repair: not run; use `fennara doctor --repair` to create base directories");
    }

    Ok(())
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
        if path.is_file() || path.is_dir() {
            "ok"
        } else {
            "missing"
        }
    );
}
