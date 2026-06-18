use crate::app_layout::{AppLayout, display_path, read_current_manifest, resolve_manifest_path};
use serde_json::Value;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

pub fn run(args: Vec<&str>) -> Result<(), String> {
    let options = InstallOptions::parse(args)?;
    let project_dir = options
        .project_dir
        .unwrap_or(env::current_dir().map_err(|err| {
            format!("failed to read the current directory; pass --project instead: {err}")
        })?);

    ensure_godot_project(&project_dir)?;

    let source = match options.source_dir {
        Some(path) => path,
        None => installed_addon_source()?
            .or_else(find_repo_addon_source)
            .ok_or_else(|| {
                "could not find an installed Fennara addon; run `fennara update` first".to_string()
            })?,
    };
    ensure_addon_source(&source)?;

    let target = project_dir.join("addons").join("fennara");
    if target.exists() {
        fs::remove_dir_all(&target).map_err(|err| {
            format!(
                "failed to remove existing addon at {}: {err}",
                display_path(&target)
            )
        })?;
    }
    copy_dir(&source, &target)?;

    println!("Installed Fennara addon");
    println!("project: {}", display_path(&project_dir));
    println!("source: {}", display_path(&source));
    println!("target: {}", display_path(&target));
    Ok(())
}

struct InstallOptions {
    project_dir: Option<PathBuf>,
    source_dir: Option<PathBuf>,
}

impl InstallOptions {
    fn parse(args: Vec<&str>) -> Result<Self, String> {
        let mut project_dir = None;
        let mut source_dir = None;
        let mut index = 0;

        while index < args.len() {
            match args[index] {
                "--project" => {
                    index += 1;
                    let value = args
                        .get(index)
                        .ok_or_else(|| "--project requires a path".to_string())?;
                    project_dir = Some(PathBuf::from(value));
                }
                arg if arg.starts_with("--project=") => {
                    project_dir = Some(PathBuf::from(arg.trim_start_matches("--project=")));
                }
                "--source" => {
                    index += 1;
                    let value = args
                        .get(index)
                        .ok_or_else(|| "--source requires a path".to_string())?;
                    source_dir = Some(PathBuf::from(value));
                }
                arg if arg.starts_with("--source=") => {
                    source_dir = Some(PathBuf::from(arg.trim_start_matches("--source=")));
                }
                "-h" | "--help" => {
                    print_help();
                    return Err("".to_string());
                }
                other => return Err(format!("unknown install option: {other}")),
            }
            index += 1;
        }

        Ok(Self {
            project_dir,
            source_dir,
        })
    }
}

fn print_help() {
    println!(
        "\
Install the Fennara Godot addon into a project.

Usage:
  fennara install
  fennara install --project <path>
  fennara install --project <path> --source <addon-path>
"
    );
}

fn ensure_godot_project(project_dir: &Path) -> Result<(), String> {
    let project_file = project_dir.join("project.godot");
    if project_file.is_file() {
        Ok(())
    } else {
        Err(format!(
            "{} is not a Godot project; run this inside a folder with project.godot or pass --project <path>",
            display_path(project_dir)
        ))
    }
}

fn installed_addon_source() -> Result<Option<PathBuf>, String> {
    let layout = AppLayout::detect()?;
    let Some(manifest) = read_current_manifest(&layout.current_manifest_path)? else {
        return Ok(None);
    };
    let Some(addon) = manifest.get("addon").and_then(Value::as_str) else {
        return Ok(None);
    };
    let source = resolve_manifest_path(&layout.app_dir, addon);
    Ok(source.is_dir().then_some(source))
}

fn find_repo_addon_source() -> Option<PathBuf> {
    let current_dir = env::current_dir().ok();
    let current_exe = env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(Path::to_path_buf));

    for root in [current_dir, current_exe].into_iter().flatten() {
        for ancestor in root.ancestors() {
            let candidate = ancestor.join("godot").join("addons").join("fennara");
            if candidate.is_dir() {
                return Some(candidate);
            }
        }
    }

    None
}

fn ensure_addon_source(source: &Path) -> Result<(), String> {
    let extension = source.join("fennara.gdextension");
    if extension.is_file() {
        Ok(())
    } else {
        Err(format!(
            "{} is not a Fennara addon folder; expected fennara.gdextension inside it",
            display_path(source)
        ))
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
