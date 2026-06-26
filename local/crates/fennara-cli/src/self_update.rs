use crate::VERSION;
use crate::app_layout::{AppLayout, binary_name, display_path};
use crate::release_client::{self, DownloadAsset};
use crate::release_manifest::{ReleaseManifest, compare_versions};
use std::cmp::Ordering;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

pub const COMPLETE_COMMAND: &str = "__complete-self-update";

const REPLACE_ATTEMPTS: usize = 150;
const REPLACE_RETRY_DELAY: Duration = Duration::from_millis(200);

pub enum StartResult {
    AlreadyCurrent,
    Skipped(String),
    Started,
}

pub fn run(args: Vec<&str>) -> Result<(), String> {
    let options = SelfUpdateOptions::parse(args)?;
    match start(&options.version, Vec::new())? {
        StartResult::Started => Ok(()),
        StartResult::AlreadyCurrent => {
            println!("Fennara CLI is already up to date.");
            println!("version: {VERSION}");
            Ok(())
        }
        StartResult::Skipped(reason) => Err(reason),
    }
}

pub fn start(version_request: &str, continuation_args: Vec<String>) -> Result<StartResult, String> {
    let layout = AppLayout::detect()?;
    layout.ensure_base_dirs()?;

    let release = release_client::fetch_release(version_request)?;
    let Some(manifest_asset) = release.manifest_asset() else {
        return Ok(StartResult::Skipped(format!(
            "release {} does not include a release manifest; CLI self-update is not available for this release",
            release.tag
        )));
    };
    let manifest_bytes = release_client::download_bytes(&manifest_asset.url, &manifest_asset.name)?;
    let manifest = ReleaseManifest::parse(&manifest_bytes)?;
    let selection = manifest.select_cli_for_current_platform()?;

    match compare_versions(VERSION, &selection.version) {
        Some(Ordering::Less) => {}
        Some(Ordering::Equal | Ordering::Greater) => return Ok(StartResult::AlreadyCurrent),
        None => {
            return Err(format!(
                "could not compare running CLI version {VERSION} with release version {}",
                selection.version
            ));
        }
    }

    let cli_asset = release
        .asset_by_name(&selection.cli.name)
        .ok_or_else(|| format!("release {} is missing {}", release.tag, selection.cli.name))?;
    let target = installed_cli_path(&layout)?;
    let stage_dir = create_stage_dir(&layout)?;
    let extract_dir = stage_dir.join("extract");
    release_client::download_zip_to_dir(
        &DownloadAsset {
            url: &cli_asset.url,
            expected_sha256: Some(selection.cli.sha256.as_str()),
            label: selection.cli.name.as_str(),
        },
        &extract_dir,
    )?;

    let staged_cli = extract_dir.join("bin").join(binary_name("fennara"));
    if !staged_cli.is_file() {
        return Err(format!(
            "downloaded CLI package is missing {}",
            display_path(&staged_cli)
        ));
    }
    make_executable(&staged_cli)?;
    validate_cli_version(&staged_cli, &selection.version)?;

    println!("Updating Fennara CLI");
    println!("from: {VERSION}");
    println!("to: {}", selection.version);
    println!("target: {}", display_path(&target));

    let mut command = Command::new(&staged_cli);
    command
        .arg(COMPLETE_COMMAND)
        .arg("--source")
        .arg(&staged_cli)
        .arg("--target")
        .arg(&target);
    if !continuation_args.is_empty() {
        command.arg("--").args(continuation_args);
    }
    command
        .spawn()
        .map_err(|err| format!("failed to start staged Fennara CLI updater: {err}"))?;

    println!("CLI updater started. The current process will exit so the binary can be replaced.");
    Ok(StartResult::Started)
}

pub fn complete(args: Vec<String>) -> Result<(), String> {
    let options = CompleteOptions::parse(args)?;
    replace_with_retry(&options.source, &options.target)?;
    let installed_version = cli_version_output(&options.target)?;

    println!("Updated Fennara CLI");
    println!("version: {installed_version}");
    println!("target: {}", display_path(&options.target));

    if options.continuation_args.is_empty() {
        return Ok(());
    }

    let status = Command::new(&options.target)
        .args(&options.continuation_args)
        .status()
        .map_err(|err| format!("failed to continue update with new Fennara CLI: {err}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("continued update command exited with {status}"))
    }
}

fn installed_cli_path(layout: &AppLayout) -> Result<PathBuf, String> {
    let expected = layout.bin_dir.join(binary_name("fennara"));
    if !expected.is_file() {
        return Err(format!(
            "installed CLI was not found at {}; rerun the install script",
            display_path(&expected)
        ));
    }

    let current = env::current_exe()
        .map_err(|err| format!("failed to locate the running Fennara CLI: {err}"))?;
    let current = canonicalize_for_compare(&current)?;
    let expected_canonical = canonicalize_for_compare(&expected)?;
    if current != expected_canonical {
        return Err(format!(
            "self-update only supports the installed CLI at {}; current executable is {}. Rerun the install script for this install location.",
            display_path(&expected),
            display_path(&current)
        ));
    }

    Ok(expected)
}

fn canonicalize_for_compare(path: &Path) -> Result<PathBuf, String> {
    path.canonicalize()
        .map_err(|err| format!("failed to resolve {}: {err}", display_path(path)))
}

fn create_stage_dir(layout: &AppLayout) -> Result<PathBuf, String> {
    let millis = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or(0);
    let path = layout
        .cache_dir
        .join("self-update")
        .join(format!("{}-{millis}", std::process::id()));
    fs::create_dir_all(&path)
        .map_err(|err| format!("failed to create {}: {err}", display_path(&path)))?;
    Ok(path)
}

fn replace_with_retry(source: &Path, target: &Path) -> Result<(), String> {
    let mut last_error = String::new();
    for _ in 0..REPLACE_ATTEMPTS {
        match replace_once(source, target) {
            Ok(()) => return Ok(()),
            Err(error) => {
                last_error = error;
                thread::sleep(REPLACE_RETRY_DELAY);
            }
        }
    }

    Err(format!(
        "failed to replace {} after waiting for the old CLI to exit: {last_error}",
        display_path(target)
    ))
}

fn replace_once(source: &Path, target: &Path) -> Result<(), String> {
    if !source.is_file() {
        return Err(format!("staged CLI is missing: {}", display_path(source)));
    }

    let parent = target
        .parent()
        .ok_or_else(|| format!("target has no parent directory: {}", display_path(target)))?;
    fs::create_dir_all(parent)
        .map_err(|err| format!("failed to create {}: {err}", display_path(parent)))?;

    let pending = pending_path(target)?;
    let backup = backup_path(target)?;
    remove_file_if_exists(&pending)?;
    fs::copy(source, &pending).map_err(|err| {
        format!(
            "failed to stage {} as {}: {err}",
            display_path(source),
            display_path(&pending)
        )
    })?;
    make_executable(&pending)?;
    validate_cli_runs(&pending)?;

    remove_file_if_exists(&backup)?;
    if target.exists() {
        fs::rename(target, &backup).map_err(|err| {
            format!(
                "failed to move old CLI {} to {}: {err}",
                display_path(target),
                display_path(&backup)
            )
        })?;
    }

    match fs::rename(&pending, target) {
        Ok(()) => {
            let _ = fs::remove_file(&backup);
            Ok(())
        }
        Err(err) => {
            if backup.exists() && !target.exists() {
                let _ = fs::rename(&backup, target);
            }
            Err(format!(
                "failed to move new CLI {} to {}: {err}",
                display_path(&pending),
                display_path(target)
            ))
        }
    }
}

fn pending_path(target: &Path) -> Result<PathBuf, String> {
    let file_name = target
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or_else(|| format!("target has invalid file name: {}", display_path(target)))?;
    let pending_name = if cfg!(target_os = "windows") {
        let stem = file_name.strip_suffix(".exe").unwrap_or(file_name);
        format!("{stem}.pending-{}.exe", std::process::id())
    } else {
        format!("{file_name}.pending-{}", std::process::id())
    };
    Ok(target.with_file_name(pending_name))
}

fn backup_path(target: &Path) -> Result<PathBuf, String> {
    let file_name = target
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or_else(|| format!("target has invalid file name: {}", display_path(target)))?;
    Ok(target.with_file_name(format!("{file_name}.old")))
}

fn remove_file_if_exists(path: &Path) -> Result<(), String> {
    if !path.exists() {
        return Ok(());
    }
    fs::remove_file(path).map_err(|err| format!("failed to remove {}: {err}", display_path(path)))
}

fn validate_cli_version(path: &Path, expected_version: &str) -> Result<(), String> {
    let actual = cli_version_output(path)?;
    let expected = format!("fennara {expected_version}");
    if actual != expected {
        return Err(format!(
            "downloaded CLI version mismatch: expected {expected}, got {actual}"
        ));
    }
    Ok(())
}

fn validate_cli_runs(path: &Path) -> Result<(), String> {
    cli_version_output(path).map(|_| ())
}

fn cli_version_output(path: &Path) -> Result<String, String> {
    let output = Command::new(path)
        .arg("--version")
        .output()
        .map_err(|err| format!("failed to run {} --version: {err}", display_path(path)))?;
    if !output.status.success() {
        return Err(format!(
            "{} --version exited with {}",
            display_path(path),
            output.status
        ));
    }
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

#[cfg(unix)]
fn make_executable(path: &Path) -> Result<(), String> {
    use std::os::unix::fs::PermissionsExt;

    let mut permissions = fs::metadata(path)
        .map_err(|err| format!("failed to inspect {}: {err}", display_path(path)))?
        .permissions();
    permissions.set_mode(0o755);
    fs::set_permissions(path, permissions)
        .map_err(|err| format!("failed to make {} executable: {err}", display_path(path)))
}

#[cfg(not(unix))]
fn make_executable(_path: &Path) -> Result<(), String> {
    Ok(())
}

struct SelfUpdateOptions {
    version: String,
}

impl SelfUpdateOptions {
    fn parse(args: Vec<&str>) -> Result<Self, String> {
        let mut version = "latest".to_string();
        let mut index = 0;

        while index < args.len() {
            match args[index] {
                "--version" => {
                    index += 1;
                    version = value_arg(&args, index, "--version")?.to_string();
                }
                arg if arg.starts_with("--version=") => {
                    version = arg.trim_start_matches("--version=").to_string();
                }
                "-h" | "--help" => {
                    print_help();
                    return Err("".to_string());
                }
                other => return Err(format!("unknown self-update option: {other}")),
            }
            index += 1;
        }

        Ok(Self { version })
    }
}

struct CompleteOptions {
    source: PathBuf,
    target: PathBuf,
    continuation_args: Vec<String>,
}

impl CompleteOptions {
    fn parse(args: Vec<String>) -> Result<Self, String> {
        let split_at = args
            .iter()
            .position(|arg| arg == "--")
            .unwrap_or(args.len());
        let control_args = &args[..split_at];
        let continuation_args = if split_at < args.len() {
            args[split_at + 1..].to_vec()
        } else {
            Vec::new()
        };

        let mut source = None;
        let mut target = None;
        let mut index = 0;
        while index < control_args.len() {
            match control_args[index].as_str() {
                "--source" => {
                    index += 1;
                    source = Some(PathBuf::from(value_arg_string(
                        control_args,
                        index,
                        "--source",
                    )?));
                }
                "--target" => {
                    index += 1;
                    target = Some(PathBuf::from(value_arg_string(
                        control_args,
                        index,
                        "--target",
                    )?));
                }
                other => return Err(format!("unknown self-update completion option: {other}")),
            }
            index += 1;
        }

        Ok(Self {
            source: source.ok_or_else(|| "--source is required".to_string())?,
            target: target.ok_or_else(|| "--target is required".to_string())?,
            continuation_args,
        })
    }
}

fn value_arg<'a>(args: &'a [&str], index: usize, option: &str) -> Result<&'a str, String> {
    args.get(index)
        .copied()
        .ok_or_else(|| format!("{option} requires a value"))
}

fn value_arg_string<'a>(args: &'a [String], index: usize, option: &str) -> Result<&'a str, String> {
    args.get(index)
        .map(String::as_str)
        .ok_or_else(|| format!("{option} requires a value"))
}

fn print_help() {
    println!(
        "\
Update only the installed Fennara CLI.

Usage:
  fennara self-update
  fennara self-update --version 0.3.1
"
    );
}
