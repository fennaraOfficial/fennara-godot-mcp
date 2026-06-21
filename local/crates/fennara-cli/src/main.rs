mod app_layout;
mod csharp_support;
mod doctor;
mod mcp_setup;
mod project_guidance;
mod project_install;
mod release_package;
mod release_update;
mod webview_runtime;

use std::env;

const VERSION: &str = env!("CARGO_PKG_VERSION");

fn main() {
    let status = match run(env::args().skip(1).collect()) {
        Ok(()) => 0,
        Err(error) if error.is_empty() => 0,
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
        Some("doctor") => doctor::run(args.iter().skip(1).map(String::as_str).collect()),
        Some("install") => project_install::run(args.iter().skip(1).map(String::as_str).collect()),
        Some("mcp-setup") => mcp_setup::run(args.iter().skip(1).map(String::as_str).collect()),
        Some("update") => release_update::run(args.iter().skip(1).map(String::as_str).collect()),
        Some(command) => Err(format!("unknown command: {command}")),
    }
}

fn print_help() {
    println!(
        "\
Fennara CLI {VERSION}

Usage:
  fennara doctor [--repair]
  fennara install [--project <path>] [--version <version>] [--csharp]
  fennara mcp-setup <target flags>
  fennara update [--version <version>] [--project <path>]
  fennara --version
  fennara --help

Commands:
  doctor     Inspect the local Fennara install layout
  install    Set up Fennara in a Godot project
  mcp-setup  Configure an MCP app to launch Fennara
  update     Update an existing Fennara project setup

Options:
  --repair   Create missing base app-data directories during doctor
"
    );
}
