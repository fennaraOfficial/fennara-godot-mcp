use rusqlite::{Connection, OptionalExtension, params};
use serde_json::{Value, json};
use std::{fs, time::Duration};

use super::settings;

#[derive(Clone, Debug, PartialEq)]
pub(crate) struct ModelTrace {
    pub(crate) provider_id: String,
    pub(crate) model_id: String,
    pub(crate) model_variant: Option<String>,
    pub(crate) model_ref_json: String,
}

pub(crate) fn connection() -> Result<Connection, String> {
    let path = settings::app_dir().join("chat.sqlite");
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let conn = Connection::open(&path)
        .map_err(|error| format!("failed to open {}: {error}", path.display()))?;
    conn.busy_timeout(Duration::from_secs(5))
        .map_err(to_store_error)?;
    conn.pragma_update(None, "journal_mode", "WAL")
        .map_err(to_store_error)?;
    conn.pragma_update(None, "synchronous", "NORMAL")
        .map_err(to_store_error)?;
    conn.pragma_update(None, "foreign_keys", true)
        .map_err(to_store_error)?;
    migrate(&conn)?;
    Ok(conn)
}

fn migrate(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        BEGIN;
        CREATE TABLE IF NOT EXISTS schema_migrations (
          version INTEGER PRIMARY KEY,
          name TEXT NOT NULL,
          applied_at_ms INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS chats (
          id TEXT PRIMARY KEY,
          title TEXT NOT NULL DEFAULT 'New chat',
          project_path TEXT,
          project_name TEXT,
          model TEXT NOT NULL,
          provider_id TEXT,
          model_id TEXT,
          model_variant TEXT,
          model_ref_json TEXT,
          reasoning_effort TEXT NOT NULL DEFAULT 'medium',
          total_cost REAL NOT NULL DEFAULT 0,
          latest_prompt_tokens INTEGER NOT NULL DEFAULT 0,
          message_count INTEGER NOT NULL DEFAULT 0,
          archived_at_ms INTEGER,
          created_at_ms INTEGER NOT NULL,
          updated_at_ms INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS chat_messages (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          role TEXT NOT NULL,
          status TEXT NOT NULL DEFAULT 'done',
          content TEXT NOT NULL DEFAULT '',
          reasoning_content TEXT,
          generation_id TEXT,
          provider_id TEXT,
          model_id TEXT,
          model_variant TEXT,
          model_ref_json TEXT,
          tool_call_id TEXT,
          tool_name TEXT,
          tool_calls_json TEXT,
          metadata_json TEXT,
          usage_json TEXT,
          cost REAL,
          sequence INTEGER NOT NULL,
          created_at_ms INTEGER NOT NULL,
          updated_at_ms INTEGER NOT NULL,
          FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_chat_messages_chat_sequence
          ON chat_messages(chat_id, sequence);
        CREATE TABLE IF NOT EXISTS chat_settings (
          key TEXT PRIMARY KEY,
          value_json TEXT NOT NULL,
          updated_at_ms INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS chat_tool_calls (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          assistant_message_id TEXT NOT NULL,
          generation_id TEXT,
          tool_name TEXT NOT NULL,
          arguments_json TEXT NOT NULL DEFAULT '{}',
          status TEXT NOT NULL DEFAULT 'pending',
          raw_result_json TEXT,
          mcp_markdown TEXT,
          plugin_markdown TEXT,
          metadata_json TEXT,
          target_keys_json TEXT,
          created_at_ms INTEGER NOT NULL,
          updated_at_ms INTEGER NOT NULL,
          FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE,
          FOREIGN KEY (assistant_message_id) REFERENCES chat_messages(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_chat_tool_calls_chat
          ON chat_tool_calls(chat_id, created_at_ms);
        CREATE TABLE IF NOT EXISTS chat_usage_logs (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          assistant_message_id TEXT,
          run_id TEXT,
          generation_id TEXT,
          model TEXT NOT NULL,
          provider_id TEXT,
          model_id TEXT,
          model_variant TEXT,
          model_ref_json TEXT,
          agent_type TEXT NOT NULL DEFAULT 'chat',
          prompt_tokens INTEGER NOT NULL DEFAULT 0,
          completion_tokens INTEGER NOT NULL DEFAULT 0,
          total_tokens INTEGER NOT NULL DEFAULT 0,
          reasoning_tokens INTEGER NOT NULL DEFAULT 0,
          cached_tokens INTEGER NOT NULL DEFAULT 0,
          cache_write_tokens INTEGER NOT NULL DEFAULT 0,
          cost REAL NOT NULL DEFAULT 0,
          upstream_cost REAL,
          provider_name TEXT,
          created_at_ms INTEGER NOT NULL,
          FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE,
          FOREIGN KEY (assistant_message_id) REFERENCES chat_messages(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_chat_usage_logs_chat_created
          ON chat_usage_logs(chat_id, created_at_ms);
        CREATE UNIQUE INDEX IF NOT EXISTS idx_chat_usage_logs_assistant_message
          ON chat_usage_logs(assistant_message_id)
          WHERE assistant_message_id IS NOT NULL;
        CREATE TABLE IF NOT EXISTS chat_generations (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          assistant_message_id TEXT,
          provider_id TEXT,
          model_id TEXT,
          model_variant TEXT,
          model_ref_json TEXT,
          reasoning_effort TEXT,
          status TEXT NOT NULL DEFAULT 'running',
          error_json TEXT,
          started_at_ms INTEGER NOT NULL,
          finished_at_ms INTEGER,
          FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE,
          FOREIGN KEY (assistant_message_id) REFERENCES chat_messages(id) ON DELETE SET NULL
        );
        CREATE INDEX IF NOT EXISTS idx_chat_generations_chat_started
          ON chat_generations(chat_id, started_at_ms);
        CREATE INDEX IF NOT EXISTS idx_chat_generations_assistant_message
          ON chat_generations(assistant_message_id);
        INSERT OR IGNORE INTO schema_migrations (version, name, applied_at_ms)
          VALUES (1, 'initial_chat_store', strftime('%s','now') * 1000);
        INSERT OR IGNORE INTO schema_migrations (version, name, applied_at_ms)
          VALUES (2, 'chat_tool_calls', strftime('%s','now') * 1000);
        INSERT OR IGNORE INTO schema_migrations (version, name, applied_at_ms)
          VALUES (3, 'project_scoped_chats', strftime('%s','now') * 1000);
        INSERT OR IGNORE INTO schema_migrations (version, name, applied_at_ms)
          VALUES (4, 'chat_usage_logs', strftime('%s','now') * 1000);
        COMMIT;
        ",
    )
    .map_err(to_store_error)?;
    add_column_if_missing(conn, "chats", "project_path", "TEXT")?;
    add_column_if_missing(conn, "chats", "project_name", "TEXT")?;
    add_column_if_missing(
        conn,
        "chats",
        "latest_prompt_tokens",
        "INTEGER NOT NULL DEFAULT 0",
    )?;
    add_model_trace_columns(conn, "chats")?;
    add_column_if_missing(conn, "chat_messages", "generation_id", "TEXT")?;
    add_model_trace_columns(conn, "chat_messages")?;
    add_column_if_missing(conn, "chat_messages", "tool_call_id", "TEXT")?;
    add_column_if_missing(conn, "chat_messages", "tool_name", "TEXT")?;
    add_column_if_missing(conn, "chat_messages", "tool_calls_json", "TEXT")?;
    add_column_if_missing(conn, "chat_messages", "metadata_json", "TEXT")?;
    add_column_if_missing(conn, "chat_tool_calls", "generation_id", "TEXT")?;
    add_column_if_missing(conn, "chat_usage_logs", "generation_id", "TEXT")?;
    add_model_trace_columns(conn, "chat_usage_logs")?;
    create_generation_trace_tables(conn)?;
    backfill_chat_model_trace(conn)?;
    backfill_tool_message_display_content(conn)?;
    backfill_usage_logs(conn)?;
    record_migration(conn, 5, "generation_traceability")?;
    Ok(())
}

pub(crate) fn to_store_error(error: rusqlite::Error) -> String {
    error.to_string()
}

fn backfill_tool_message_display_content(conn: &Connection) -> Result<(), String> {
    conn.execute(
        "UPDATE chat_messages
         SET content = COALESCE(
               (SELECT plugin_markdown
                FROM chat_tool_calls
                WHERE chat_tool_calls.id = chat_messages.tool_call_id
                  AND plugin_markdown IS NOT NULL
                  AND plugin_markdown != ''),
               content
             ),
             status = COALESCE(
               (SELECT status
                FROM chat_tool_calls
                WHERE chat_tool_calls.id = chat_messages.tool_call_id
                  AND status IS NOT NULL
                  AND status != ''),
               status
             )
         WHERE role = 'tool'
           AND tool_call_id IS NOT NULL",
        [],
    )
    .map_err(to_store_error)?;
    Ok(())
}

fn add_column_if_missing(
    conn: &Connection,
    table: &str,
    column: &str,
    definition: &str,
) -> Result<(), String> {
    let mut statement = conn
        .prepare(&format!("PRAGMA table_info({table})"))
        .map_err(to_store_error)?;
    let rows = statement
        .query_map([], |row| row.get::<_, String>(1))
        .map_err(to_store_error)?;
    for row in rows {
        if row.map_err(to_store_error)? == column {
            return Ok(());
        }
    }
    conn.execute(
        &format!("ALTER TABLE {table} ADD COLUMN {column} {definition}"),
        [],
    )
    .map_err(to_store_error)?;
    Ok(())
}

fn add_model_trace_columns(conn: &Connection, table: &str) -> Result<(), String> {
    add_column_if_missing(conn, table, "provider_id", "TEXT")?;
    add_column_if_missing(conn, table, "model_id", "TEXT")?;
    add_column_if_missing(conn, table, "model_variant", "TEXT")?;
    add_column_if_missing(conn, table, "model_ref_json", "TEXT")?;
    Ok(())
}

fn create_generation_trace_tables(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS chat_generations (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          assistant_message_id TEXT,
          provider_id TEXT,
          model_id TEXT,
          model_variant TEXT,
          model_ref_json TEXT,
          reasoning_effort TEXT,
          status TEXT NOT NULL DEFAULT 'running',
          error_json TEXT,
          started_at_ms INTEGER NOT NULL,
          finished_at_ms INTEGER,
          FOREIGN KEY (chat_id) REFERENCES chats(id) ON DELETE CASCADE,
          FOREIGN KEY (assistant_message_id) REFERENCES chat_messages(id) ON DELETE SET NULL
        );
        CREATE INDEX IF NOT EXISTS idx_chat_generations_chat_started
          ON chat_generations(chat_id, started_at_ms);
        CREATE INDEX IF NOT EXISTS idx_chat_generations_assistant_message
          ON chat_generations(assistant_message_id);
        ",
    )
    .map_err(to_store_error)
}

fn record_migration(conn: &Connection, version: i64, name: &str) -> Result<(), String> {
    conn.execute(
        "INSERT OR IGNORE INTO schema_migrations (version, name, applied_at_ms)
         VALUES (?1, ?2, strftime('%s','now') * 1000)",
        params![version, name],
    )
    .map_err(to_store_error)?;
    Ok(())
}

fn backfill_chat_model_trace(conn: &Connection) -> Result<(), String> {
    let mut statement = conn
        .prepare(
            "SELECT id, model FROM chats
             WHERE provider_id IS NULL
                OR model_id IS NULL
                OR model_ref_json IS NULL",
        )
        .map_err(to_store_error)?;
    let rows = statement
        .query_map([], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })
        .map_err(to_store_error)?;
    let mut updates = Vec::new();
    for row in rows {
        let (chat_id, model) = row.map_err(to_store_error)?;
        let Some(trace) = model_trace_from_selection(&model) else {
            continue;
        };
        updates.push((chat_id, trace));
    }
    drop(statement);

    for (chat_id, trace) in updates {
        conn.execute(
            "UPDATE chats
             SET provider_id = ?2,
                 model_id = ?3,
                 model_variant = ?4,
                 model_ref_json = ?5
             WHERE id = ?1",
            params![
                chat_id,
                trace.provider_id,
                trace.model_id,
                trace.model_variant,
                trace.model_ref_json
            ],
        )
        .map_err(to_store_error)?;
    }
    Ok(())
}

fn backfill_usage_logs(conn: &Connection) -> Result<(), String> {
    let mut statement = conn
        .prepare(
            "SELECT id, chat_id, usage_json, cost, created_at_ms
             FROM chat_messages
             WHERE role = 'assistant'
               AND usage_json IS NOT NULL
               AND usage_json != ''",
        )
        .map_err(to_store_error)?;
    let rows = statement
        .query_map([], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, Option<f64>>(3)?,
                row.get::<_, i64>(4)?,
            ))
        })
        .map_err(to_store_error)?;

    for row in rows {
        let (message_id, chat_id, usage_json, message_cost, created_at_ms) =
            row.map_err(to_store_error)?;
        let existing: Option<String> = conn
            .query_row(
                "SELECT id FROM chat_usage_logs WHERE assistant_message_id = ?1",
                [&message_id],
                |row| row.get(0),
            )
            .optional()
            .map_err(to_store_error)?;
        if existing.is_some() {
            continue;
        }
        let Ok(usage) = serde_json::from_str::<Value>(&usage_json) else {
            continue;
        };
        let usage_model = usage_string(&usage, "model").unwrap_or_else(|| "unknown".to_string());
        let trace = model_trace_from_selection(&usage_model);
        conn.execute(
            "INSERT INTO chat_usage_logs
             (id, chat_id, assistant_message_id, generation_id, model,
              provider_id, model_id, model_variant, model_ref_json,
              agent_type, prompt_tokens,
              completion_tokens, total_tokens, reasoning_tokens, cached_tokens,
              cache_write_tokens, cost, upstream_cost, provider_name,
              created_at_ms)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 'chat', ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18)",
            params![
                format!("usage_backfill_{message_id}"),
                chat_id,
                message_id,
                usage_string(&usage, "generation_id"),
                usage_model,
                trace.as_ref().map(|trace| trace.provider_id.as_str()),
                trace.as_ref().map(|trace| trace.model_id.as_str()),
                trace.as_ref().and_then(|trace| trace.model_variant.as_deref()),
                trace.as_ref().map(|trace| trace.model_ref_json.as_str()),
                usage_i64(&usage, "prompt_tokens", "promptTokens"),
                usage_i64(&usage, "completion_tokens", "completionTokens"),
                usage_i64(&usage, "total_tokens", "totalTokens"),
                usage_i64(&usage, "reasoning_tokens", "reasoningTokens"),
                usage_i64(&usage, "cached_tokens", "cachedTokens"),
                usage_i64(&usage, "cache_write_tokens", "cacheWriteTokens"),
                usage_f64(&usage, "cost", "total_cost")
                    .or(message_cost)
                    .unwrap_or(0.0),
                usage_f64(&usage, "upstream_cost", "upstreamCost"),
                usage_string(&usage, "provider_name"),
                created_at_ms
            ],
        )
        .map_err(to_store_error)?;
    }
    Ok(())
}

fn usage_i64(usage: &Value, snake_key: &str, camel_key: &str) -> i64 {
    usage
        .get(snake_key)
        .or_else(|| usage.get(camel_key))
        .and_then(Value::as_i64)
        .unwrap_or(0)
}

fn usage_f64(usage: &Value, snake_key: &str, camel_key: &str) -> Option<f64> {
    usage
        .get(snake_key)
        .or_else(|| usage.get(camel_key))
        .and_then(Value::as_f64)
}

fn usage_string(usage: &Value, key: &str) -> Option<String> {
    usage
        .get(key)
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(ToOwned::to_owned)
}

pub(crate) fn model_trace_from_selection(model: &str) -> Option<ModelTrace> {
    let model = model.trim();
    if model.is_empty() {
        return None;
    }
    let (provider_id, model_id) = if let Some(model_id) = model.strip_prefix("ollama/") {
        ("ollama", model_id.trim())
    } else if let Some(model_id) = model.strip_prefix("ollama-cloud/") {
        ("ollama-cloud", model_id.trim())
    } else if let Some(model_id) = model.strip_prefix("lmstudio/") {
        ("lmstudio", model_id.trim())
    } else if let Some(model_id) = model.strip_prefix("deepseek/") {
        ("deepseek", model_id.trim())
    } else if let Some(model_id) = model.strip_prefix("zai/") {
        ("zai", model_id.trim())
    } else if let Some(model_id) = model.strip_prefix("openrouter/") {
        ("openrouter", model_id.trim())
    } else if model.contains('/') {
        ("openrouter", model)
    } else {
        return None;
    };
    if model_id.is_empty() {
        return None;
    }
    let provider_id = provider_id.to_string();
    let model_id = model_id.to_string();
    let model_variant = None;
    let model_ref_json = json!({
        "provider_id": &provider_id,
        "model_id": &model_id,
        "variant": model_variant.as_deref(),
    })
    .to_string();
    Some(ModelTrace {
        provider_id,
        model_id,
        model_variant,
        model_ref_json,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn has_column(conn: &Connection, table: &str, column: &str) -> bool {
        let mut statement = conn
            .prepare(&format!("PRAGMA table_info({table})"))
            .unwrap();
        let rows = statement
            .query_map([], |row| row.get::<_, String>(1))
            .unwrap();
        rows.map(|row| row.unwrap()).any(|name| name == column)
    }

    #[test]
    fn model_trace_parses_legacy_and_explicit_models() {
        let ollama = model_trace_from_selection("ollama/llama3.2").unwrap();
        assert_eq!(ollama.provider_id, "ollama");
        assert_eq!(ollama.model_id, "llama3.2");

        let openrouter = model_trace_from_selection("openrouter/google/gemini").unwrap();
        assert_eq!(openrouter.provider_id, "openrouter");
        assert_eq!(openrouter.model_id, "google/gemini");

        let lmstudio = model_trace_from_selection("lmstudio/openai/gpt-oss-20b").unwrap();
        assert_eq!(lmstudio.provider_id, "lmstudio");
        assert_eq!(lmstudio.model_id, "openai/gpt-oss-20b");

        let deepseek = model_trace_from_selection("deepseek/deepseek-chat").unwrap();
        assert_eq!(deepseek.provider_id, "deepseek");
        assert_eq!(deepseek.model_id, "deepseek-chat");

        let zai = model_trace_from_selection("zai/glm-5.2").unwrap();
        assert_eq!(zai.provider_id, "zai");
        assert_eq!(zai.model_id, "glm-5.2");

        let legacy = model_trace_from_selection("google/gemini").unwrap();
        assert_eq!(legacy.provider_id, "openrouter");
        assert_eq!(legacy.model_id, "google/gemini");

        assert!(model_trace_from_selection("").is_none());
        assert!(model_trace_from_selection("not-a-routable-model").is_none());
        assert!(model_trace_from_selection("ollama/").is_none());
    }

    #[test]
    fn migration_adds_generation_trace_columns_and_backfills_chats() {
        let conn = Connection::open_in_memory().unwrap();
        conn.execute_batch(
            "
            CREATE TABLE schema_migrations (
              version INTEGER PRIMARY KEY,
              name TEXT NOT NULL,
              applied_at_ms INTEGER NOT NULL
            );
            CREATE TABLE chats (
              id TEXT PRIMARY KEY,
              title TEXT NOT NULL DEFAULT 'New chat',
              project_path TEXT,
              project_name TEXT,
              model TEXT NOT NULL,
              reasoning_effort TEXT NOT NULL DEFAULT 'medium',
              total_cost REAL NOT NULL DEFAULT 0,
              latest_prompt_tokens INTEGER NOT NULL DEFAULT 0,
              message_count INTEGER NOT NULL DEFAULT 0,
              archived_at_ms INTEGER,
              created_at_ms INTEGER NOT NULL,
              updated_at_ms INTEGER NOT NULL
            );
            CREATE TABLE chat_messages (
              id TEXT PRIMARY KEY,
              chat_id TEXT NOT NULL,
              role TEXT NOT NULL,
              status TEXT NOT NULL DEFAULT 'done',
              content TEXT NOT NULL DEFAULT '',
              reasoning_content TEXT,
              tool_call_id TEXT,
              tool_name TEXT,
              tool_calls_json TEXT,
              metadata_json TEXT,
              usage_json TEXT,
              cost REAL,
              sequence INTEGER NOT NULL,
              created_at_ms INTEGER NOT NULL,
              updated_at_ms INTEGER NOT NULL
            );
            CREATE TABLE chat_tool_calls (
              id TEXT PRIMARY KEY,
              chat_id TEXT NOT NULL,
              assistant_message_id TEXT NOT NULL,
              tool_name TEXT NOT NULL,
              arguments_json TEXT NOT NULL DEFAULT '{}',
              status TEXT NOT NULL DEFAULT 'pending',
              raw_result_json TEXT,
              mcp_markdown TEXT,
              plugin_markdown TEXT,
              metadata_json TEXT,
              target_keys_json TEXT,
              created_at_ms INTEGER NOT NULL,
              updated_at_ms INTEGER NOT NULL
            );
            CREATE TABLE chat_usage_logs (
              id TEXT PRIMARY KEY,
              chat_id TEXT NOT NULL,
              assistant_message_id TEXT,
              run_id TEXT,
              model TEXT NOT NULL,
              agent_type TEXT NOT NULL DEFAULT 'chat',
              prompt_tokens INTEGER NOT NULL DEFAULT 0,
              completion_tokens INTEGER NOT NULL DEFAULT 0,
              total_tokens INTEGER NOT NULL DEFAULT 0,
              reasoning_tokens INTEGER NOT NULL DEFAULT 0,
              cached_tokens INTEGER NOT NULL DEFAULT 0,
              cache_write_tokens INTEGER NOT NULL DEFAULT 0,
              cost REAL NOT NULL DEFAULT 0,
              upstream_cost REAL,
              provider_name TEXT,
              created_at_ms INTEGER NOT NULL
            );
            INSERT INTO chats
              (id, title, model, reasoning_effort, created_at_ms, updated_at_ms)
              VALUES
              ('chat_1', 'One', 'google/gemini', 'medium', 1, 1),
              ('chat_2', 'Two', 'ollama/llama3.2', 'medium', 1, 1),
              ('chat_3', 'Three', 'weird', 'medium', 1, 1);
            ",
        )
        .unwrap();

        migrate(&conn).unwrap();

        assert!(has_column(&conn, "chat_messages", "generation_id"));
        assert!(has_column(&conn, "chat_tool_calls", "generation_id"));
        assert!(has_column(&conn, "chat_usage_logs", "generation_id"));
        assert!(has_column(&conn, "chats", "provider_id"));
        assert!(has_column(&conn, "chat_messages", "model_ref_json"));
        assert!(has_column(&conn, "chat_usage_logs", "model_ref_json"));

        let generation_count: i64 = conn
            .query_row("SELECT COUNT(*) FROM chat_generations", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert_eq!(generation_count, 0);

        let chat_1: (Option<String>, Option<String>) = conn
            .query_row(
                "SELECT provider_id, model_id FROM chats WHERE id = 'chat_1'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        assert_eq!(
            chat_1,
            (
                Some("openrouter".to_string()),
                Some("google/gemini".to_string())
            )
        );

        let chat_2: (Option<String>, Option<String>) = conn
            .query_row(
                "SELECT provider_id, model_id FROM chats WHERE id = 'chat_2'",
                [],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )
            .unwrap();
        assert_eq!(
            chat_2,
            (Some("ollama".to_string()), Some("llama3.2".to_string()))
        );

        let weird_provider: Option<String> = conn
            .query_row(
                "SELECT provider_id FROM chats WHERE id = 'chat_3'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(weird_provider, None);
    }
}
