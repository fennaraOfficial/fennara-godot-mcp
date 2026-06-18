use crate::app_layout::display_path;
use crate::project_guidance;
use crate::project_install;
use crate::release_package;
use std::path::PathBuf;

pub fn run(args: Vec<&str>) -> Result<(), String> {
    let options = UpdateOptions::parse(args)?;
    let project_dir = project_install::resolve_project_dir(options.project_dir)?;
    project_install::ensure_godot_project(&project_dir)?;

    if !project_install::has_fennara_addon(&project_dir) {
        return Err(format!(
            "This Godot project does not have Fennara installed yet. Run `fennara install` from {} first.",
            display_path(&project_dir)
        ));
    }

    let package = release_package::ensure_package(&options.version)?;
    let project_version = project_install::project_addon_version(&project_dir);
    if project_version.as_deref() == Some(package.version.as_str()) {
        project_guidance::write(&project_dir)?;
        println!("Fennara is already up to date.");
        println!("version: {}", package.version);
        println!("project: {}", display_path(&project_dir));
        println!("guidance: refreshed AGENTS.md and .fennara/ai/guidelines.md");
        return Ok(());
    }

    project_install::install_addon(&project_dir, &package.addon_dir)?;
    project_guidance::write(&project_dir)?;
    println!("Updated Fennara");
    println!(
        "from: {}",
        project_version.unwrap_or_else(|| "unknown".to_string())
    );
    println!("to: {}", package.version);
    println!("project: {}", display_path(&project_dir));
    println!("guidance: refreshed AGENTS.md and .fennara/ai/guidelines.md");
    Ok(())
}

struct UpdateOptions {
    version: String,
    project_dir: Option<PathBuf>,
}

impl UpdateOptions {
    fn parse(args: Vec<&str>) -> Result<Self, String> {
        let mut version = "latest".to_string();
        let mut project_dir = None;
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
                "--project" => {
                    index += 1;
                    project_dir = Some(PathBuf::from(value_arg(&args, index, "--project")?));
                }
                arg if arg.starts_with("--project=") => {
                    project_dir = Some(PathBuf::from(arg.trim_start_matches("--project=")));
                }
                "-h" | "--help" => {
                    print_help();
                    return Err("".to_string());
                }
                other => return Err(format!("unknown update option: {other}")),
            }
            index += 1;
        }

        Ok(Self {
            version,
            project_dir,
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
Update an existing Fennara project setup.

Usage:
  fennara update
  fennara update --project <path>
  fennara update --version 0.2.8 --project <path>
"
    );
}
