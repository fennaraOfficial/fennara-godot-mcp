use crate::app_layout::display_path;
use crate::project_guidance;
use crate::project_install;
use crate::release_package;
use crate::self_update::{self, StartResult};
use crate::webview_prereq;
use std::path::PathBuf;

pub fn run(args: Vec<&str>) -> Result<(), String> {
    let options = UpdateOptions::parse(args)?;
    let project_dir = project_install::resolve_project_dir(options.project_dir.clone())?;
    project_install::ensure_godot_project(&project_dir)?;
    println!("Updating Fennara");
    println!("project: {}", display_path(&project_dir));
    println!("requested version: {}", options.version);

    if !project_install::has_fennara_addon(&project_dir) {
        return Err(format!(
            "This Godot project does not have Fennara installed yet. Run `fennara install` from {} first.",
            display_path(&project_dir)
        ));
    }

    if !options.no_self_update {
        println!("self-update: checking installed CLI");
        match self_update::start(&options.version, options.continuation_args())? {
            StartResult::Started => return Ok(()),
            StartResult::AlreadyCurrent => println!("self-update: CLI is current"),
            StartResult::Skipped(reason) => println!("warning: {reason}"),
        }
    } else {
        println!("self-update: skipped by --no-self-update");
    }

    println!("package: resolving update package");
    let package = release_package::ensure_package(&options.version)?;
    let project_version = project_install::project_addon_version(&project_dir);
    if project_version.as_deref() == Some(package.version.as_str()) {
        println!("guidance: refreshing AGENTS.md and addons/fennara/ai/guidelines.md");
        project_guidance::write(&project_dir)?;
        println!("Fennara is already up to date.");
        println!("version: {}", package.version);
        println!("project: {}", display_path(&project_dir));
        println!("guidance: refreshed AGENTS.md and addons/fennara/ai/guidelines.md");
        webview_prereq::warn_for_current_platform()?;
        return Ok(());
    }

    println!("addon: copying from {}", display_path(&package.addon_dir));
    project_install::install_addon(&project_dir, &package.addon_dir)?;
    println!("guidance: refreshing AGENTS.md and addons/fennara/ai/guidelines.md");
    project_guidance::write(&project_dir)?;
    println!("Updated Fennara");
    println!(
        "from: {}",
        project_version.unwrap_or_else(|| "unknown".to_string())
    );
    println!("to: {}", package.version);
    println!("project: {}", display_path(&project_dir));
    println!("guidance: refreshed AGENTS.md and addons/fennara/ai/guidelines.md");
    webview_prereq::warn_for_current_platform()?;
    Ok(())
}

struct UpdateOptions {
    version: String,
    project_dir: Option<PathBuf>,
    no_self_update: bool,
}

impl UpdateOptions {
    fn parse(args: Vec<&str>) -> Result<Self, String> {
        let mut version = "latest".to_string();
        let mut project_dir = None;
        let mut no_self_update = false;
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
                "--no-self-update" => {
                    no_self_update = true;
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
            no_self_update,
        })
    }

    fn continuation_args(&self) -> Vec<String> {
        let mut args = vec![
            "update".to_string(),
            "--no-self-update".to_string(),
            "--version".to_string(),
            self.version.clone(),
        ];
        if let Some(project_dir) = &self.project_dir {
            args.push("--project".to_string());
            args.push(project_dir.display().to_string());
        }
        args
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
  fennara update --no-self-update
"
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn continuation_args_resume_without_self_update() {
        let options = UpdateOptions {
            version: "1.2.3".to_string(),
            project_dir: Some(PathBuf::from("demo-project")),
            no_self_update: false,
        };

        assert_eq!(
            options.continuation_args(),
            vec![
                "update".to_string(),
                "--no-self-update".to_string(),
                "--version".to_string(),
                "1.2.3".to_string(),
                "--project".to_string(),
                PathBuf::from("demo-project").display().to_string(),
            ]
        );
    }

    #[test]
    fn parse_accepts_no_self_update() {
        let options = UpdateOptions::parse(vec!["--no-self-update", "--version", "1.2.3"])
            .expect("parse update options");

        assert!(options.no_self_update);
        assert_eq!(options.version, "1.2.3");
    }
}
