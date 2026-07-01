use crate::app_layout::display_path;
use std::fs;
use std::path::Path;

const AGENTS_BLOCK: &str = include_str!("../../../templates/AGENTS.block.md");
const GUIDELINES_MD: &str = include_str!("../../../templates/fennara-guidelines.md");
const AGENTS_START: &str = "<!-- fennara-agents-start -->";
const AGENTS_END: &str = "<!-- fennara-agents-end -->";
const GUIDELINES_PATH: &[&str] = &["addons", "fennara", "ai", "guidelines.md"];

pub fn write(project_dir: &Path) -> Result<(), String> {
    write_guidelines(project_dir)?;
    update_agents(project_dir)?;
    update_gitignore_if_present(project_dir)
}

fn write_guidelines(project_dir: &Path) -> Result<(), String> {
    let guidelines_path = GUIDELINES_PATH
        .iter()
        .fold(project_dir.to_path_buf(), |path, part| path.join(part));

    if let Some(parent) = guidelines_path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("failed to create {}: {err}", display_path(parent)))?;
    }

    write_if_changed(
        &guidelines_path,
        normalize_template(GUIDELINES_MD).as_bytes(),
    )
}

fn update_agents(project_dir: &Path) -> Result<(), String> {
    let agents_path = project_dir.join("AGENTS.md");
    let block = normalize_template(AGENTS_BLOCK);
    let existing = fs::read_to_string(&agents_path).unwrap_or_default();
    let next = replace_or_append_block(&existing, &block)?;

    write_if_changed(&agents_path, next.as_bytes())
}

fn update_gitignore_if_present(project_dir: &Path) -> Result<(), String> {
    let gitignore_path = project_dir.join(".gitignore");
    if !gitignore_path.is_file() {
        return Ok(());
    }

    let existing = fs::read_to_string(&gitignore_path)
        .map_err(|err| format!("failed to read {}: {err}", display_path(&gitignore_path)))?;
    let already_ignored = existing
        .lines()
        .any(|line| matches!(line.trim(), ".fennara" | ".fennara/"));
    if already_ignored {
        return Ok(());
    }

    let mut next = ensure_single_trailing_newline(&existing);
    next.push_str(".fennara/\n");
    write_if_changed(&gitignore_path, next.as_bytes())
}

fn replace_or_append_block(existing: &str, block: &str) -> Result<String, String> {
    if existing.trim().is_empty() {
        return Ok(format!("{block}\n"));
    }

    if let Some(start) = existing.find(AGENTS_START) {
        let Some(end_relative) = existing[start..].find(AGENTS_END) else {
            return Err(format!(
                "found {AGENTS_START} in AGENTS.md but could not find {AGENTS_END}"
            ));
        };
        let end = start + end_relative + AGENTS_END.len();
        let mut next = String::new();
        next.push_str(&existing[..start]);
        next.push_str(block);
        next.push_str(&existing[end..]);
        return Ok(ensure_single_trailing_newline(&next));
    }

    Ok(format!("{}\n\n{block}\n", existing.trim_end()))
}

fn normalize_template(template: &str) -> String {
    ensure_single_trailing_newline(template.trim())
}

fn ensure_single_trailing_newline(value: &str) -> String {
    format!("{}\n", value.trim_end())
}

fn write_if_changed(path: &Path, content: &[u8]) -> Result<(), String> {
    if fs::read(path).ok().as_deref() == Some(content) {
        return Ok(());
    }

    fs::write(path, content).map_err(|err| format!("failed to write {}: {err}", display_path(path)))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn appends_block_to_existing_agents_file() {
        let next = replace_or_append_block("# Existing\n", "BLOCK").unwrap();
        assert_eq!(next, "# Existing\n\nBLOCK\n");
    }

    #[test]
    fn replaces_existing_generated_block() {
        let existing =
            "before\n<!-- fennara-agents-start -->\nold\n<!-- fennara-agents-end -->\nafter\n";
        let next = replace_or_append_block(existing, "new").unwrap();
        assert_eq!(next, "before\nnew\nafter\n");
    }

    #[test]
    fn appends_fennara_to_existing_gitignore() {
        let temp =
            std::env::temp_dir().join(format!("fennara-guidance-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&temp);
        fs::create_dir_all(&temp).unwrap();
        let gitignore = temp.join(".gitignore");
        fs::write(&gitignore, "target/\n").unwrap();

        update_gitignore_if_present(&temp).unwrap();

        assert_eq!(
            fs::read_to_string(&gitignore).unwrap(),
            "target/\n.fennara/\n"
        );
        let _ = fs::remove_dir_all(&temp);
    }
}
