use rusqlite::{Connection, OptionalExtension, params};
use serde_json::Value;
use std::{fs, time::Duration};

use super::settings;

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
          generation_id TEXT,
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
    add_column_if_missing(conn, "chat_messages", "tool_call_id", "TEXT")?;
    add_column_if_missing(conn, "chat_messages", "tool_name", "TEXT")?;
    add_column_if_missing(conn, "chat_messages", "tool_calls_json", "TEXT")?;
    add_column_if_missing(conn, "chat_messages", "metadata_json", "TEXT")?;
    backfill_tool_message_display_content(conn)?;
    backfill_usage_logs(conn)?;
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
        conn.execute(
            "INSERT INTO chat_usage_logs
             (id, chat_id, assistant_message_id, model, agent_type, prompt_tokens,
              completion_tokens, total_tokens, reasoning_tokens, cached_tokens,
              cache_write_tokens, cost, upstream_cost, generation_id, provider_name,
              created_at_ms)
             VALUES (?1, ?2, ?3, ?4, 'chat', ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)",
            params![
                format!("usage_backfill_{message_id}"),
                chat_id,
                message_id,
                usage_string(&usage, "model").unwrap_or_else(|| "unknown".to_string()),
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
                usage_string(&usage, "generation_id"),
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
