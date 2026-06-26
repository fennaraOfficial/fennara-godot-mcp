use crate::app_layout::display_path;
use crate::csharp_support;
use crate::project_guidance;
use crate::release_package;
use crate::webview_prereq;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

pub fn run(args: Vec<&str>) -> Result<(), String> {
    let options = InstallOptions::parse(args)?;
    let project_dir = resolve_project_dir(options.project_dir)?;
    ensure_godot_project(&project_dir)?;

    if project_addon_dir(&project_dir).exists() {
        return Err(format!(
            "Fennara is already installed in this project. Run `fennara update` from {} instead.",
            display_path(&project_dir)
        ));
    }

    let (version, source) = match options.source_dir {
        Some(path) => ("local".to_string(), path),
        None => {
            let package = release_package::ensure_package(&options.version)?;
            (package.version, package.addon_dir)
        }
    };
    install_addon(&project_dir, &source)?;
    project_guidance::write(&project_dir)?;
    if options.csharp {
        csharp_support::install()?;
    }

    println!("Installed Fennara");
    println!("version: {version}");
    println!("project: {}", display_path(&project_dir));
    println!("guidance: wrote AGENTS.md and .fennara/ai/guidelines.md");
    if options.csharp {
        println!("csharp: installed");
    }
    webview_prereq::warn_for_current_platform()?;
    println!("next: run `fennara update` inside this project when a new release is available");
    Ok(())
}

pub fn resolve_project_dir(project_dir: Option<PathBuf>) -> Result<PathBuf, String> {
    match project_dir {
        Some(path) => Ok(path),
        None => env::current_dir().map_err(|err| {
            format!("failed to read the current directory; pass --project instead: {err}")
        }),
    }
}

pub fn is_godot_project(project_dir: &Path) -> bool {
    project_dir.join("project.godot").is_file()
}

pub fn ensure_godot_project(project_dir: &Path) -> Result<(), String> {
    if is_godot_project(project_dir) {
        Ok(())
    } else {
        Err(format!(
            "{} is not a Godot project. Run this inside a folder with project.godot or pass --project <path>.",
            display_path(project_dir)
        ))
    }
}

pub fn has_fennara_addon(project_dir: &Path) -> bool {
    project_addon_dir(project_dir)
        .join("fennara.gdextension")
        .is_file()
}

pub fn project_addon_version(project_dir: &Path) -> Option<String> {
    fs::read_to_string(project_addon_dir(project_dir).join("VERSION"))
        .ok()
        .map(|version| version.trim().to_string())
        .filter(|version| !version.is_empty())
}

pub fn install_addon(project_dir: &Path, source: &Path) -> Result<(), String> {
    ensure_godot_project(project_dir)?;
    ensure_addon_source(source)?;

    let target = project_addon_dir(project_dir);
    if target.exists() {
        fs::remove_dir_all(&target).map_err(|err| {
            format!(
                "failed to remove existing addon at {}: {err}",
                display_path(&target)
            )
        })?;
    }
    copy_dir(source, &target)
}

pub fn project_addon_dir(project_dir: &Path) -> PathBuf {
    project_dir.join("addons").join("fennara")
}

struct InstallOptions {
    project_dir: Option<PathBuf>,
    source_dir: Option<PathBuf>,
    version: String,
    csharp: bool,
}

impl InstallOptions {
    fn parse(args: Vec<&str>) -> Result<Self, String> {
        let mut project_dir = None;
        let mut source_dir = None;
        let mut version = "latest".to_string();
        let mut csharp = false;
        let mut index = 0;

        while index < args.len() {
            match args[index] {
                "--project" => {
                    index += 1;
                    project_dir = Some(PathBuf::from(value_arg(&args, index, "--project")?));
                }
                arg if arg.starts_with("--project=") => {
                    project_dir = Some(PathBuf::from(arg.trim_start_matches("--project=")));
                }
                "--source" => {
                    index += 1;
                    source_dir = Some(PathBuf::from(value_arg(&args, index, "--source")?));
                }
                arg if arg.starts_with("--source=") => {
                    source_dir = Some(PathBuf::from(arg.trim_start_matches("--source=")));
                }
                "--version" => {
                    index += 1;
                    version = value_arg(&args, index, "--version")?.to_string();
                }
                arg if arg.starts_with("--version=") => {
                    version = arg.trim_start_matches("--version=").to_string();
                }
                "--csharp" => {
                    csharp = true;
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
            version,
            csharp,
        })
    }
}

fn value_arg<'a>(args: &'a [&str], index: usize, option: &str) -> Result<&'a str, String> {
    args.get(index)
        .copied()
        .ok_or_else(|| format!("{option} requires a value"))
}

fn print_help() {
    println!(
        "\
Install Fennara into a Godot project.

Usage:
  fennara install
  fennara install --project <path>
  fennara install --csharp
  fennara install --version 0.2.8 --project <path>
"
    );
}

fn ensure_addon_source(source: &Path) -> Result<(), String> {
    if source.join("fennara.gdextension").is_file() {
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
