use rusqlite::{Connection, OptionalExtension, params};
use serde::Serialize;
use serde_json::{Value, json};

use super::{
    ids::{new_id, now_ms},
    images::{self, ImagePlaceholder},
    schema::{ModelTrace, connection, model_trace_from_selection, to_store_error},
    settings::{self, DEFAULT_MODEL},
};

const CHAT_LIST_LIMIT: i64 = 40;
const REPLAY_MESSAGE_LIMIT: i64 = 40;
const MAX_IMAGE_METADATA_MESSAGES_PER_CHAT: i64 = 12;

#[derive(Clone, Debug, Serialize)]
pub(crate) struct ChatSummary {
    pub(crate) id: String,
    pub(crate) title: String,
    pub(crate) project_path: Option<String>,
    pub(crate) project_name: Option<String>,
    pub(crate) model: String,
    pub(crate) reasoning_effort: String,
    pub(crate) total_cost: f64,
    pub(crate) latest_prompt_tokens: i64,
    pub(crate) message_count: i64,
    pub(crate) created_at_ms: i64,
    pub(crate) updated_at_ms: i64,
}

#[derive(Clone, Debug, Default)]
pub(crate) struct ProjectScope {
    pub(crate) project_path: Option<String>,
    pub(crate) project_name: Option<String>,
}

impl ProjectScope {
    pub(crate) fn key(&self) -> String {
        self.project_path
            .as_deref()
            .map(normalize_project_path)
            .filter(|path| !path.is_empty())
            .unwrap_or_else(|| "global".to_string())
    }
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct StoredMessage {
    pub(crate) id: String,
    pub(crate) chat_id: String,
    pub(crate) role: String,
    pub(crate) status: String,
    pub(crate) content: String,
    pub(crate) reasoning_content: Option<String>,
    pub(crate) tool_call_id: Option<String>,
    pub(crate) tool_name: Option<String>,
    pub(crate) tool_calls_json: Option<String>,
    pub(crate) metadata_json: Option<String>,
    pub(crate) usage_json: Option<String>,
    pub(crate) cost: Option<f64>,
    pub(crate) sequence: i64,
    pub(crate) created_at_ms: i64,
    pub(crate) updated_at_ms: i64,
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct OpenedChat {
    pub(crate) chat: ChatSummary,
    pub(crate) messages: Vec<StoredMessage>,
}

#[derive(Clone, Debug)]
pub(crate) struct StartedGeneration {
    pub(crate) id: String,
}

pub(crate) fn list_chats(scope: &ProjectScope) -> Result<Vec<ChatSummary>, String> {
    let conn = connection()?;
    let mut statement = conn
        .prepare(
            "SELECT id, title, project_path, project_name, model, reasoning_effort, total_cost, latest_prompt_tokens, message_count, created_at_ms, updated_at_ms
             FROM chats
             WHERE archived_at_ms IS NULL AND project_path IS ?1
             ORDER BY updated_at_ms DESC
             LIMIT ?2",
        )
        .map_err(to_store_error)?;
    let rows = statement
        .query_map(
            params![scope.project_path.as_deref(), CHAT_LIST_LIMIT],
            chat_from_row,
        )
        .map_err(to_store_error)?;
    rows.collect::<Result<Vec<_>, _>>().map_err(to_store_error)
}

pub(crate) fn open_chat(scope: &ProjectScope, chat_id: &str) -> Result<OpenedChat, String> {
    let conn = connection()?;
    let chat = get_chat_for_scope(&conn, scope, chat_id)?
        .ok_or_else(|| "Chat not found for this project.".to_string())?;
    let messages = messages_for_chat(&conn, chat_id)?;
    set_active_chat_id(scope, chat_id)?;
    Ok(OpenedChat { chat, messages })
}

pub(crate) fn chat_summary(chat_id: &str) -> Result<ChatSummary, String> {
    let conn = connection()?;
    get_chat(&conn, chat_id)?.ok_or_else(|| "Chat not found.".to_string())
}

pub(crate) fn open_active_or_create(
    scope: &ProjectScope,
    model: &str,
    reasoning_effort: &str,
) -> Result<OpenedChat, String> {
    if let Some(chat_id) = active_chat_id(scope)? {
        if let Ok(opened) = open_chat(scope, &chat_id) {
            return Ok(opened);
        }
    }
    create_chat(scope, model, reasoning_effort)
}

pub(crate) fn create_chat(
    scope: &ProjectScope,
    model: &str,
    reasoning_effort: &str,
) -> Result<OpenedChat, String> {
    let conn = connection()?;
    let now = now_ms();
    let chat_id = new_id("chat");
    let clean_model = settings::clean_model(model).unwrap_or_else(|| DEFAULT_MODEL.to_string());
    let clean_effort = settings::clean_reasoning_effort(reasoning_effort);
    let model_trace = model_trace_from_selection(&clean_model);
    conn.execute(
        "INSERT INTO chats
         (id, title, project_path, project_name, model,
          provider_id, model_id, model_variant, model_ref_json,
          reasoning_effort, total_cost, latest_prompt_tokens, message_count, created_at_ms, updated_at_ms)
         VALUES (?1, 'New chat', ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, 0, 0, 0, ?10, ?10)",
        params![
            chat_id,
            scope.project_path.as_deref(),
            scope.project_name.as_deref(),
            clean_model,
            model_trace.as_ref().map(|trace| trace.provider_id.as_str()),
            model_trace.as_ref().map(|trace| trace.model_id.as_str()),
            model_trace
                .as_ref()
                .and_then(|trace| trace.model_variant.as_deref()),
            model_trace
                .as_ref()
                .map(|trace| trace.model_ref_json.as_str()),
            clean_effort,
            now
        ],
    )
    .map_err(to_store_error)?;
    set_active_chat_id(scope, &chat_id)?;
    open_chat(scope, &chat_id)
}

pub(crate) fn archive_chat(scope: &ProjectScope, chat_id: &str) -> Result<(), String> {
    let conn = connection()?;
    if get_chat_for_scope(&conn, scope, chat_id)?.is_none() {
        return Err("Chat not found for this project.".to_string());
    }
    let now = now_ms();
    conn.execute(
        "UPDATE chats SET archived_at_ms = ?2, updated_at_ms = ?2 WHERE id = ?1",
        params![chat_id, now],
    )
    .map_err(to_store_error)?;
    if active_chat_id(scope)?.as_deref() == Some(chat_id) {
        set_active_chat_id(scope, "")?;
    }
    Ok(())
}

pub(crate) fn revert_last_turn(scope: &ProjectScope, chat_id: &str) -> Result<OpenedChat, String> {
    let mut conn = connection()?;
    if get_chat_for_scope(&conn, scope, chat_id)?.is_none() {
        return Err("Chat not found for this project.".to_string());
    }
    let start_sequence: Option<i64> = conn
        .query_row(
            "SELECT sequence FROM chat_messages
             WHERE chat_id = ?1 AND role = 'user'
             ORDER BY sequence DESC
             LIMIT 1",
            [chat_id],
            |row| row.get(0),
        )
        .optional()
        .map_err(to_store_error)?;
    let Some(start_sequence) = start_sequence else {
        return open_chat(scope, chat_id);
    };

    let tx = conn.transaction().map_err(to_store_error)?;
    tx.execute(
        "DELETE FROM chat_usage_logs
         WHERE chat_id = ?1
           AND assistant_message_id IN (
             SELECT id FROM chat_messages WHERE chat_id = ?1 AND sequence >= ?2
           )",
        params![chat_id, start_sequence],
    )
    .map_err(to_store_error)?;
    tx.execute(
        "DELETE FROM chat_tool_calls
         WHERE chat_id = ?1
           AND assistant_message_id IN (
             SELECT id FROM chat_messages WHERE chat_id = ?1 AND sequence >= ?2
           )",
        params![chat_id, start_sequence],
    )
    .map_err(to_store_error)?;
    tx.execute(
        "DELETE FROM chat_messages WHERE chat_id = ?1 AND sequence >= ?2",
        params![chat_id, start_sequence],
    )
    .map_err(to_store_error)?;
    refresh_chat_rollups(&tx, chat_id)?;
    tx.commit().map_err(to_store_error)?;
    open_chat(scope, chat_id)
}

pub(crate) fn last_user_message_content(chat_id: &str) -> Result<Option<String>, String> {
    let conn = connection()?;
    conn.query_row(
        "SELECT content FROM chat_messages
         WHERE chat_id = ?1 AND role = 'user'
         ORDER BY sequence DESC
         LIMIT 1",
        [chat_id],
        |row| row.get(0),
    )
    .optional()
    .map_err(to_store_error)
}

pub(crate) fn ensure_chat_in_scope(scope: &ProjectScope, chat_id: &str) -> Result<(), String> {
    let conn = connection()?;
    get_chat_for_scope(&conn, scope, chat_id)?
        .map(|_| ())
        .ok_or_else(|| "Chat not found for this project.".to_string())
}

pub(crate) fn set_chat_model(
    chat_id: &str,
    model: &str,
    reasoning_effort: &str,
) -> Result<(), String> {
    let conn = connection()?;
    if get_chat(&conn, chat_id)?.is_none() {
        return Err("Chat not found.".to_string());
    }
    let clean_model = settings::clean_model(model).unwrap_or_else(|| DEFAULT_MODEL.to_string());
    let clean_effort = settings::clean_reasoning_effort(reasoning_effort);
    let model_trace = model_trace_from_selection(&clean_model);
    let now = now_ms();
    conn.execute(
        "UPDATE chats
         SET model = ?2,
             provider_id = ?3,
             model_id = ?4,
             model_variant = ?5,
             model_ref_json = ?6,
             reasoning_effort = ?7,
             updated_at_ms = ?8
         WHERE id = ?1",
        params![
            chat_id,
            clean_model,
            model_trace.as_ref().map(|trace| trace.provider_id.as_str()),
            model_trace.as_ref().map(|trace| trace.model_id.as_str()),
            model_trace
                .as_ref()
                .and_then(|trace| trace.model_variant.as_deref()),
            model_trace
                .as_ref()
                .map(|trace| trace.model_ref_json.as_str()),
            clean_effort,
            now
        ],
    )
    .map_err(to_store_error)?;
    Ok(())
}

pub(crate) fn insert_user_message(
    chat_id: &str,
    content: &str,
    metadata: Option<&Value>,
) -> Result<StoredMessage, String> {
    let message = insert_message(chat_id, "user", "done", content, None, None, None)?;
    if let Some(metadata) = metadata {
        let conn = connection()?;
        let metadata_json = serde_json::to_string(metadata).map_err(|error| error.to_string())?;
        let now = now_ms();
        conn.execute(
            "UPDATE chat_messages SET metadata_json = ?2, updated_at_ms = ?3 WHERE id = ?1",
            params![message.id, metadata_json, now],
        )
        .map_err(to_store_error)?;
        prune_old_image_metadata(&conn, chat_id)?;
        return get_message(&conn, &message.id)?.ok_or_else(|| "Message not found.".to_string());
    }
    Ok(message)
}

fn prune_old_image_metadata(conn: &Connection, chat_id: &str) -> Result<(), String> {
    let now = now_ms();
    conn.execute(
        "UPDATE chat_messages
         SET metadata_json = NULL,
             updated_at_ms = ?2
         WHERE id IN (
           SELECT id
           FROM chat_messages
           WHERE chat_id = ?1
             AND role = 'user'
             AND metadata_json LIKE '%\"images\"%'
           ORDER BY sequence DESC
           LIMIT -1 OFFSET ?3
         )",
        params![chat_id, now, MAX_IMAGE_METADATA_MESSAGES_PER_CHAT],
    )
    .map_err(to_store_error)?;
    Ok(())
}

pub(crate) fn insert_assistant_placeholder(chat_id: &str) -> Result<StoredMessage, String> {
    insert_message(chat_id, "assistant", "in_progress", "", None, None, None)
}

pub(crate) fn start_generation(
    chat_id: &str,
    assistant_message_id: &str,
    model: &str,
    reasoning_effort: &str,
) -> Result<StartedGeneration, String> {
    let conn = connection()?;
    if get_message(&conn, assistant_message_id)?.is_none() {
        return Err("Assistant message not found.".to_string());
    }
    let now = now_ms();
    let generation_id = new_id("gen");
    let model_trace = model_trace_from_selection(model);
    conn.execute(
        "INSERT INTO chat_generations
         (id, chat_id, assistant_message_id, provider_id, model_id, model_variant,
          model_ref_json, reasoning_effort, status, started_at_ms)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, 'running', ?9)",
        params![
            generation_id,
            chat_id,
            assistant_message_id,
            model_trace.as_ref().map(|trace| trace.provider_id.as_str()),
            model_trace.as_ref().map(|trace| trace.model_id.as_str()),
            model_trace
                .as_ref()
                .and_then(|trace| trace.model_variant.as_deref()),
            model_trace
                .as_ref()
                .map(|trace| trace.model_ref_json.as_str()),
            reasoning_effort,
            now
        ],
    )
    .map_err(to_store_error)?;
    set_message_generation_trace(
        &conn,
        assistant_message_id,
        Some(&generation_id),
        model_trace.as_ref(),
    )?;
    Ok(StartedGeneration { id: generation_id })
}

pub(crate) fn finish_generation(
    generation_id: &str,
    status: &str,
    error: Option<&Value>,
) -> Result<(), String> {
    let conn = connection()?;
    let now = now_ms();
    let error_json = error
        .map(serde_json::to_string)
        .transpose()
        .map_err(|error| error.to_string())?;
    conn.execute(
        "UPDATE chat_generations
         SET status = ?2,
             error_json = ?3,
             finished_at_ms = ?4
         WHERE id = ?1",
        params![generation_id, status, error_json, now],
    )
    .map_err(to_store_error)?;
    Ok(())
}

fn set_message_generation_trace(
    conn: &Connection,
    message_id: &str,
    generation_id: Option<&str>,
    model_trace: Option<&ModelTrace>,
) -> Result<(), String> {
    let now = now_ms();
    conn.execute(
        "UPDATE chat_messages
         SET generation_id = COALESCE(?2, generation_id),
             provider_id = COALESCE(?3, provider_id),
             model_id = COALESCE(?4, model_id),
             model_variant = COALESCE(?5, model_variant),
             model_ref_json = COALESCE(?6, model_ref_json),
             updated_at_ms = ?7
         WHERE id = ?1",
        params![
            message_id,
            generation_id,
            model_trace.map(|trace| trace.provider_id.as_str()),
            model_trace.map(|trace| trace.model_id.as_str()),
            model_trace.and_then(|trace| trace.model_variant.as_deref()),
            model_trace.map(|trace| trace.model_ref_json.as_str()),
            now
        ],
    )
    .map_err(to_store_error)?;
    Ok(())
}

pub(crate) fn finish_assistant_message(
    message_id: &str,
    content: &str,
    reasoning_content: Option<&str>,
    usage: Option<&Value>,
    fallback_model: &str,
    generation_id: Option<&str>,
) -> Result<StoredMessage, String> {
    let conn = connection()?;
    let now = now_ms();
    let usage_json = usage
        .map(serde_json::to_string)
        .transpose()
        .map_err(|error| error.to_string())?;
    let cost = usage.and_then(usage_cost);
    let model_trace = model_trace_from_selection(fallback_model);
    conn.execute(
        "UPDATE chat_messages
         SET status = 'done',
             content = ?2,
             reasoning_content = ?3,
             usage_json = ?4,
             cost = ?5,
             generation_id = COALESCE(?6, generation_id),
             provider_id = COALESCE(?7, provider_id),
             model_id = COALESCE(?8, model_id),
             model_variant = COALESCE(?9, model_variant),
             model_ref_json = COALESCE(?10, model_ref_json),
             updated_at_ms = ?11
         WHERE id = ?1",
        params![
            message_id,
            content,
            reasoning_content,
            usage_json,
            cost,
            generation_id,
            model_trace.as_ref().map(|trace| trace.provider_id.as_str()),
            model_trace.as_ref().map(|trace| trace.model_id.as_str()),
            model_trace
                .as_ref()
                .and_then(|trace| trace.model_variant.as_deref()),
            model_trace
                .as_ref()
                .map(|trace| trace.model_ref_json.as_str()),
            now
        ],
    )
    .map_err(to_store_error)?;
    let message =
        get_message(&conn, message_id)?.ok_or_else(|| "Message not found.".to_string())?;
    if let Some(usage) = usage {
        record_usage_log(
            &conn,
            &message.chat_id,
            &message.id,
            fallback_model,
            "chat",
            usage,
            generation_id,
            model_trace.as_ref(),
        )?;
    }
    refresh_chat_rollups(&conn, &message.chat_id)?;
    Ok(message)
}

pub(crate) fn fail_assistant_message(
    message_id: &str,
    content: &str,
) -> Result<StoredMessage, String> {
    let conn = connection()?;
    let now = now_ms();
    conn.execute(
        "UPDATE chat_messages
         SET status = 'failed',
             content = ?2,
             updated_at_ms = ?3
         WHERE id = ?1",
        params![message_id, content, now],
    )
    .map_err(to_store_error)?;
    let message =
        get_message(&conn, message_id)?.ok_or_else(|| "Message not found.".to_string())?;
    refresh_chat_rollups(&conn, &message.chat_id)?;
    Ok(message)
}

pub(crate) fn cancel_turn(
    chat_id: &str,
    assistant_message_id: &str,
    assistant_content: &str,
) -> Result<StoredMessage, String> {
    let conn = connection()?;
    let assistant = get_message(&conn, assistant_message_id)?
        .ok_or_else(|| "Assistant message not found.".to_string())?;
    let user_sequence: Option<i64> = conn
        .query_row(
            "SELECT sequence FROM chat_messages
             WHERE chat_id = ?1 AND role = 'user' AND sequence <= ?2
             ORDER BY sequence DESC
             LIMIT 1",
            params![chat_id, assistant.sequence],
            |row| row.get(0),
        )
        .optional()
        .map_err(to_store_error)?;
    let now = now_ms();
    if let Some(user_sequence) = user_sequence {
        conn.execute(
            "UPDATE chat_messages
             SET status = 'cancelled', updated_at_ms = ?3
             WHERE chat_id = ?1 AND sequence >= ?2",
            params![chat_id, user_sequence, now],
        )
        .map_err(to_store_error)?;
    }
    conn.execute(
        "UPDATE chat_messages
         SET status = 'cancelled',
             content = ?2,
             updated_at_ms = ?3
         WHERE id = ?1",
        params![assistant_message_id, assistant_content, now],
    )
    .map_err(to_store_error)?;
    let message = get_message(&conn, assistant_message_id)?
        .ok_or_else(|| "Assistant message not found.".to_string())?;
    refresh_chat_rollups(&conn, chat_id)?;
    Ok(message)
}

pub(crate) fn set_assistant_tool_calls(
    message_id: &str,
    tool_calls: &Value,
) -> Result<StoredMessage, String> {
    let conn = connection()?;
    let tool_calls_json = serde_json::to_string(tool_calls).map_err(|error| error.to_string())?;
    let now = now_ms();
    conn.execute(
        "UPDATE chat_messages SET tool_calls_json = ?2, updated_at_ms = ?3 WHERE id = ?1",
        params![message_id, tool_calls_json, now],
    )
    .map_err(to_store_error)?;
    get_message(&conn, message_id)?.ok_or_else(|| "Message not found.".to_string())
}

pub(crate) fn insert_tool_message(
    chat_id: &str,
    tool_call_id: &str,
    tool_name: &str,
    status: &str,
    content: &str,
    metadata: &Value,
) -> Result<StoredMessage, String> {
    let message = insert_message(chat_id, "tool", status, content, None, None, None)?;
    let conn = connection()?;
    let metadata_json = serde_json::to_string(metadata).map_err(|error| error.to_string())?;
    let now = now_ms();
    conn.execute(
        "UPDATE chat_messages
         SET tool_call_id = ?2, tool_name = ?3, metadata_json = ?4, updated_at_ms = ?5
         WHERE id = ?1",
        params![message.id, tool_call_id, tool_name, metadata_json, now],
    )
    .map_err(to_store_error)?;
    get_message(&conn, &message.id)?.ok_or_else(|| "Message not found.".to_string())
}

pub(crate) fn upsert_tool_call(
    chat_id: &str,
    assistant_message_id: &str,
    generation_id: Option<&str>,
    tool_call_id: &str,
    tool_name: &str,
    arguments: &Value,
    status: &str,
) -> Result<(), String> {
    let conn = connection()?;
    let now = now_ms();
    let args_json = serde_json::to_string(arguments).map_err(|error| error.to_string())?;
    conn.execute(
        "INSERT INTO chat_tool_calls
         (id, chat_id, assistant_message_id, generation_id, tool_name, arguments_json, status, created_at_ms, updated_at_ms)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8)
         ON CONFLICT(id) DO UPDATE SET
           generation_id = COALESCE(excluded.generation_id, chat_tool_calls.generation_id),
           tool_name = excluded.tool_name,
           arguments_json = excluded.arguments_json,
           status = excluded.status,
           updated_at_ms = excluded.updated_at_ms",
        params![
            tool_call_id,
            chat_id,
            assistant_message_id,
            generation_id,
            tool_name,
            args_json,
            status,
            now
        ],
    )
    .map_err(to_store_error)?;
    Ok(())
}

pub(crate) fn finish_tool_call(
    tool_call_id: &str,
    status: &str,
    raw_result: &Value,
    mcp_markdown: &str,
    plugin_markdown: &str,
    metadata: &Value,
    target_keys: &[String],
) -> Result<(), String> {
    let conn = connection()?;
    let now = now_ms();
    let raw_json = serde_json::to_string(raw_result).map_err(|error| error.to_string())?;
    let metadata_json = serde_json::to_string(metadata).map_err(|error| error.to_string())?;
    let target_keys_json = serde_json::to_string(target_keys).map_err(|error| error.to_string())?;
    conn.execute(
        "UPDATE chat_tool_calls
         SET status = ?2,
             raw_result_json = ?3,
             mcp_markdown = ?4,
             plugin_markdown = ?5,
             metadata_json = ?6,
             target_keys_json = ?7,
             updated_at_ms = ?8
         WHERE id = ?1",
        params![
            tool_call_id,
            status,
            raw_json,
            mcp_markdown,
            plugin_markdown,
            metadata_json,
            target_keys_json,
            now
        ],
    )
    .map_err(to_store_error)?;
    Ok(())
}

pub(crate) fn replay_messages(chat_id: &str) -> Result<Vec<Value>, String> {
    let conn = connection()?;
    let mut statement = conn
        .prepare(
            "SELECT
               m.role,
               CASE
                 WHEN m.role = 'tool' THEN COALESCE(t.mcp_markdown, m.content)
                 ELSE m.content
               END AS content,
               m.tool_call_id,
               m.tool_name,
               m.tool_calls_json,
               m.metadata_json
             FROM chat_messages m
             LEFT JOIN chat_tool_calls t ON t.id = m.tool_call_id
             WHERE m.chat_id = ?1
               AND (m.status = 'done' OR (m.role = 'tool' AND m.status = 'failed'))
               AND m.role IN ('user', 'assistant', 'tool')
             ORDER BY m.sequence DESC
             LIMIT ?2",
        )
        .map_err(to_store_error)?;
    let mut rows = statement
        .query(params![chat_id, REPLAY_MESSAGE_LIMIT])
        .map_err(to_store_error)?;
    let mut messages = Vec::new();

    while let Some(row) = rows.next().map_err(to_store_error)? {
        let role: String = row.get(0).map_err(to_store_error)?;
        let content: String = row.get(1).map_err(to_store_error)?;
        let tool_call_id: Option<String> = row.get(2).map_err(to_store_error)?;
        let tool_name: Option<String> = row.get(3).map_err(to_store_error)?;
        let tool_calls_json: Option<String> = row.get(4).map_err(to_store_error)?;
        let metadata_json: Option<String> = row.get(5).map_err(to_store_error)?;
        let image_placeholders = image_placeholders_from_metadata(metadata_json.as_deref());
        let message_content = if role == "user" && !image_placeholders.is_empty() {
            images::user_content_with_image_placeholders(&content, &image_placeholders)
        } else {
            json!(content)
        };
        let mut message = json!({ "role": role, "content": message_content });
        if let Some(tool_call_id) = tool_call_id {
            message["tool_call_id"] = json!(tool_call_id);
        }
        if let Some(tool_name) = tool_name {
            message["tool_name"] = json!(tool_name);
        }
        if let Some(tool_calls_json) = tool_calls_json {
            if let Ok(tool_calls) = serde_json::from_str::<Value>(&tool_calls_json) {
                message["tool_calls"] = tool_calls;
            }
        }
        messages.push(message);
    }

    messages.reverse();
    Ok(messages)
}

fn image_placeholders_from_metadata(metadata_json: Option<&str>) -> Vec<ImagePlaceholder> {
    let Some(metadata_json) = metadata_json else {
        return Vec::new();
    };
    let Ok(metadata) = serde_json::from_str::<Value>(metadata_json) else {
        return Vec::new();
    };
    let Some(images) = metadata.get("images") else {
        return Vec::new();
    };
    images
        .as_array()
        .map(|images| {
            images
                .iter()
                .filter_map(image_placeholder_from_value)
                .collect()
        })
        .unwrap_or_default()
}

fn image_placeholder_from_value(value: &Value) -> Option<ImagePlaceholder> {
    let object = value.as_object()?;
    let mime_type = object
        .get("mime_type")
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .unwrap_or("image")
        .to_string();
    let name = object
        .get("name")
        .or_else(|| object.get("description"))
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| value.chars().take(120).collect::<String>());
    Some(ImagePlaceholder { mime_type, name })
}

pub(crate) fn set_active_chat_id(scope: &ProjectScope, chat_id: &str) -> Result<(), String> {
    let conn = connection()?;
    let key = active_chat_key(scope);
    let value = if chat_id.is_empty() {
        Value::Null
    } else {
        json!(chat_id)
    };
    let now = now_ms();
    conn.execute(
        "INSERT INTO chat_settings (key, value_json, updated_at_ms)
         VALUES (?1, ?2, ?3)
         ON CONFLICT(key) DO UPDATE SET value_json = excluded.value_json, updated_at_ms = excluded.updated_at_ms",
        params![key, value.to_string(), now],
    )
    .map_err(to_store_error)?;
    Ok(())
}

pub(crate) fn active_chat_id(scope: &ProjectScope) -> Result<Option<String>, String> {
    let conn = connection()?;
    let key = active_chat_key(scope);
    let raw: Option<String> = conn
        .query_row(
            "SELECT value_json FROM chat_settings WHERE key = ?1",
            [key],
            |row| row.get(0),
        )
        .optional()
        .map_err(to_store_error)?;
    let Some(raw) = raw else {
        return Ok(None);
    };
    Ok(serde_json::from_str::<Option<String>>(&raw)
        .ok()
        .flatten()
        .filter(|id| !id.trim().is_empty()))
}

fn insert_message(
    chat_id: &str,
    role: &str,
    status: &str,
    content: &str,
    reasoning_content: Option<&str>,
    usage: Option<&Value>,
    cost: Option<f64>,
) -> Result<StoredMessage, String> {
    let conn = connection()?;
    if get_chat(&conn, chat_id)?.is_none() {
        return Err("Chat not found.".to_string());
    }
    let now = now_ms();
    let message_id = new_id("msg");
    let sequence = next_sequence(&conn, chat_id)?;
    let usage_json = usage
        .map(serde_json::to_string)
        .transpose()
        .map_err(|error| error.to_string())?;
    conn.execute(
        "INSERT INTO chat_messages
         (id, chat_id, role, status, content, reasoning_content, usage_json, cost, sequence, created_at_ms, updated_at_ms)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?10)",
        params![
            message_id,
            chat_id,
            role,
            status,
            content,
            reasoning_content,
            usage_json,
            cost,
            sequence,
            now
        ],
    )
    .map_err(to_store_error)?;
    refresh_chat_rollups(&conn, chat_id)?;
    get_message(&conn, &message_id)?.ok_or_else(|| "Message not found.".to_string())
}

fn next_sequence(conn: &Connection, chat_id: &str) -> Result<i64, String> {
    let max_sequence: Option<i64> = conn
        .query_row(
            "SELECT MAX(sequence) FROM chat_messages WHERE chat_id = ?1",
            [chat_id],
            |row| row.get(0),
        )
        .map_err(to_store_error)?;
    Ok(max_sequence.unwrap_or(0) + 1)
}

fn get_chat(conn: &Connection, chat_id: &str) -> Result<Option<ChatSummary>, String> {
    conn.query_row(
        "SELECT id, title, project_path, project_name, model, reasoning_effort, total_cost, latest_prompt_tokens, message_count, created_at_ms, updated_at_ms
         FROM chats
         WHERE id = ?1 AND archived_at_ms IS NULL",
        [chat_id],
        chat_from_row,
    )
    .optional()
    .map_err(to_store_error)
}

fn get_chat_for_scope(
    conn: &Connection,
    scope: &ProjectScope,
    chat_id: &str,
) -> Result<Option<ChatSummary>, String> {
    conn.query_row(
        "SELECT id, title, project_path, project_name, model, reasoning_effort, total_cost, latest_prompt_tokens, message_count, created_at_ms, updated_at_ms
         FROM chats
         WHERE id = ?1 AND archived_at_ms IS NULL AND project_path IS ?2",
        params![chat_id, scope.project_path.as_deref()],
        chat_from_row,
    )
    .optional()
    .map_err(to_store_error)
}

fn get_message(conn: &Connection, message_id: &str) -> Result<Option<StoredMessage>, String> {
    conn.query_row(
        "SELECT id, chat_id, role, status, content, reasoning_content, tool_call_id, tool_name, tool_calls_json, metadata_json, usage_json, cost, sequence, created_at_ms, updated_at_ms
         FROM chat_messages
         WHERE id = ?1",
        [message_id],
        message_from_row,
    )
    .optional()
    .map_err(to_store_error)
}

fn messages_for_chat(conn: &Connection, chat_id: &str) -> Result<Vec<StoredMessage>, String> {
    let mut statement = conn
        .prepare(
            "SELECT id, chat_id, role, status, content, reasoning_content, tool_call_id, tool_name, tool_calls_json, metadata_json, usage_json, cost, sequence, created_at_ms, updated_at_ms
             FROM chat_messages
             WHERE chat_id = ?1
             ORDER BY sequence ASC",
        )
        .map_err(to_store_error)?;
    let rows = statement
        .query_map([chat_id], message_from_row)
        .map_err(to_store_error)?;
    rows.collect::<Result<Vec<_>, _>>().map_err(to_store_error)
}

fn refresh_chat_rollups(conn: &Connection, chat_id: &str) -> Result<(), String> {
    let now = now_ms();
    let message_count: i64 = conn
        .query_row(
            "SELECT COUNT(*) FROM chat_messages WHERE chat_id = ?1 AND status = 'done'",
            [chat_id],
            |row| row.get(0),
        )
        .map_err(to_store_error)?;
    let total_cost = total_cost_for_chat(conn, chat_id)?;
    let latest_prompt_tokens = latest_prompt_tokens_for_chat(conn, chat_id)?;
    let first_user: Option<String> = conn
        .query_row(
            "SELECT content FROM chat_messages WHERE chat_id = ?1 AND role = 'user' ORDER BY sequence ASC LIMIT 1",
            [chat_id],
            |row| row.get(0),
        )
        .optional()
        .map_err(to_store_error)?;
    let title = first_user
        .as_deref()
        .map(chat_title)
        .unwrap_or_else(|| "New chat".to_string());
    conn.execute(
        "UPDATE chats
         SET title = ?2, message_count = ?3, total_cost = ?4, latest_prompt_tokens = ?5, updated_at_ms = ?6
         WHERE id = ?1",
        params![
            chat_id,
            title,
            message_count,
            total_cost,
            latest_prompt_tokens,
            now
        ],
    )
    .map_err(to_store_error)?;
    Ok(())
}

fn chat_from_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<ChatSummary> {
    Ok(ChatSummary {
        id: row.get(0)?,
        title: row.get(1)?,
        project_path: row.get(2)?,
        project_name: row.get(3)?,
        model: row.get(4)?,
        reasoning_effort: row.get(5)?,
        total_cost: row.get(6)?,
        latest_prompt_tokens: row.get(7)?,
        message_count: row.get(8)?,
        created_at_ms: row.get(9)?,
        updated_at_ms: row.get(10)?,
    })
}

fn message_from_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<StoredMessage> {
    Ok(StoredMessage {
        id: row.get(0)?,
        chat_id: row.get(1)?,
        role: row.get(2)?,
        status: row.get(3)?,
        content: row.get(4)?,
        reasoning_content: row.get(5)?,
        tool_call_id: row.get(6)?,
        tool_name: row.get(7)?,
        tool_calls_json: row.get(8)?,
        metadata_json: row.get(9)?,
        usage_json: row.get(10)?,
        cost: row.get(11)?,
        sequence: row.get(12)?,
        created_at_ms: row.get(13)?,
        updated_at_ms: row.get(14)?,
    })
}

fn usage_cost(usage: &Value) -> Option<f64> {
    usage
        .get("cost")
        .or_else(|| usage.get("total_cost"))
        .and_then(Value::as_f64)
}

fn record_usage_log(
    conn: &Connection,
    chat_id: &str,
    assistant_message_id: &str,
    fallback_model: &str,
    agent_type: &str,
    usage: &Value,
    generation_id: Option<&str>,
    model_trace: Option<&ModelTrace>,
) -> Result<(), String> {
    let now = now_ms();
    let model = usage_string(usage, "model").unwrap_or_else(|| fallback_model.to_string());
    let usage_generation = usage_string(usage, "generation_id");
    let usage_generation_id = generation_id.or(usage_generation.as_deref());
    conn.execute(
        "INSERT INTO chat_usage_logs
         (id, chat_id, assistant_message_id, generation_id, model,
          provider_id, model_id, model_variant, model_ref_json,
          agent_type, prompt_tokens,
          completion_tokens, total_tokens, reasoning_tokens, cached_tokens,
          cache_write_tokens, cost, upstream_cost, provider_name, created_at_ms)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20)
         ON CONFLICT(assistant_message_id) WHERE assistant_message_id IS NOT NULL DO UPDATE SET
           generation_id = COALESCE(excluded.generation_id, chat_usage_logs.generation_id),
           model = excluded.model,
           provider_id = COALESCE(excluded.provider_id, chat_usage_logs.provider_id),
           model_id = COALESCE(excluded.model_id, chat_usage_logs.model_id),
           model_variant = COALESCE(excluded.model_variant, chat_usage_logs.model_variant),
           model_ref_json = COALESCE(excluded.model_ref_json, chat_usage_logs.model_ref_json),
           agent_type = excluded.agent_type,
           prompt_tokens = excluded.prompt_tokens,
           completion_tokens = excluded.completion_tokens,
           total_tokens = excluded.total_tokens,
           reasoning_tokens = excluded.reasoning_tokens,
           cached_tokens = excluded.cached_tokens,
           cache_write_tokens = excluded.cache_write_tokens,
           cost = excluded.cost,
           upstream_cost = excluded.upstream_cost,
           provider_name = excluded.provider_name",
        params![
            new_id("usage"),
            chat_id,
            assistant_message_id,
            usage_generation_id,
            model,
            model_trace.map(|trace| trace.provider_id.as_str()),
            model_trace.map(|trace| trace.model_id.as_str()),
            model_trace.and_then(|trace| trace.model_variant.as_deref()),
            model_trace.map(|trace| trace.model_ref_json.as_str()),
            agent_type,
            usage_i64_any(usage, &["prompt_tokens", "promptTokens", "input_tokens", "inputTokens"]),
            usage_i64_any(usage, &["completion_tokens", "completionTokens", "output_tokens", "outputTokens"]),
            usage_i64_any(usage, &["total_tokens", "totalTokens"]),
            usage_i64_any(usage, &["reasoning_tokens", "reasoningTokens"]),
            usage_i64_any(usage, &[
                "cached_tokens",
                "cachedTokens",
                "cache_read_tokens",
                "cacheReadTokens",
                "cache_read_input_tokens",
                "cacheReadInputTokens",
                "cachedInputTokens"
            ]),
            usage_i64_any(usage, &[
                "cache_write_tokens",
                "cacheWriteTokens",
                "cache_creation_input_tokens",
                "cacheWriteInputTokens"
            ]),
            usage_cost(usage).unwrap_or(0.0),
            usage_f64(usage, "upstream_cost", "upstreamCost"),
            usage_string(usage, "provider_name"),
            now
        ],
    )
    .map_err(to_store_error)?;
    Ok(())
}

fn total_cost_for_chat(conn: &Connection, chat_id: &str) -> Result<f64, String> {
    let from_usage_logs: Option<f64> = conn
        .query_row(
            "SELECT SUM(cost) FROM chat_usage_logs WHERE chat_id = ?1",
            [chat_id],
            |row| row.get(0),
        )
        .map_err(to_store_error)?;
    if let Some(cost) = from_usage_logs {
        return Ok(cost);
    }
    let from_messages: Option<f64> = conn
        .query_row(
            "SELECT SUM(cost) FROM chat_messages WHERE chat_id = ?1",
            [chat_id],
            |row| row.get(0),
        )
        .map_err(to_store_error)?;
    Ok(from_messages.unwrap_or(0.0))
}

fn latest_prompt_tokens_for_chat(conn: &Connection, chat_id: &str) -> Result<i64, String> {
    let mut usage_statement = conn
        .prepare(
            "SELECT prompt_tokens, total_tokens
             FROM chat_usage_logs
             WHERE chat_id = ?1
               AND agent_type != 'context_summary'
             ORDER BY created_at_ms DESC",
        )
        .map_err(to_store_error)?;
    let usage_rows = usage_statement
        .query_map([chat_id], |row| {
            Ok((row.get::<_, i64>(0)?, row.get::<_, i64>(1)?))
        })
        .map_err(to_store_error)?;
    for row in usage_rows {
        let (prompt_tokens, total_tokens) = row.map_err(to_store_error)?;
        if prompt_tokens > 0 {
            return Ok(prompt_tokens);
        }
        if total_tokens > 0 {
            return Ok(total_tokens);
        }
    }

    let mut statement = conn
        .prepare(
            "SELECT usage_json
             FROM chat_messages
             WHERE chat_id = ?1
               AND role = 'assistant'
               AND status = 'done'
               AND usage_json IS NOT NULL
               AND usage_json != ''
             ORDER BY sequence DESC",
        )
        .map_err(to_store_error)?;
    let rows = statement
        .query_map([chat_id], |row| row.get::<_, String>(0))
        .map_err(to_store_error)?;

    for row in rows {
        let usage_json = row.map_err(to_store_error)?;
        let Ok(usage) = serde_json::from_str::<Value>(&usage_json) else {
            continue;
        };
        if let Some(tokens) = usage_prompt_tokens(&usage) {
            return Ok(tokens);
        }
    }
    Ok(0)
}

fn usage_prompt_tokens(usage: &Value) -> Option<i64> {
    usage
        .get("prompt_tokens")
        .or_else(|| usage.get("promptTokens"))
        .and_then(Value::as_i64)
        .filter(|tokens| *tokens > 0)
        .or_else(|| {
            usage
                .get("total_tokens")
                .or_else(|| usage.get("totalTokens"))
                .and_then(Value::as_i64)
                .filter(|tokens| *tokens > 0)
        })
}

fn usage_i64_any(usage: &Value, keys: &[&str]) -> i64 {
    keys.iter()
        .find_map(|key| usage.get(*key).and_then(Value::as_i64))
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

fn chat_title(content: &str) -> String {
    let title = content
        .split_whitespace()
        .take(8)
        .collect::<Vec<_>>()
        .join(" ");
    if title.chars().count() > 60 {
        title.chars().take(57).collect::<String>() + "..."
    } else if title.is_empty() {
        "New chat".to_string()
    } else {
        title
    }
}

fn active_chat_key(scope: &ProjectScope) -> String {
    format!("active_chat_id:{}", scope.key())
}

fn normalize_project_path(path: &str) -> String {
    path.trim().replace('\\', "/").to_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn old_image_metadata_replays_as_placeholders() {
        let metadata = json!({
            "images": [
                {
                    "base64": "a".repeat(320_000),
                    "mime_type": "image/png",
                    "name": "old.png",
                    "size_bytes": 240000
                }
            ]
        })
        .to_string();

        let placeholders = image_placeholders_from_metadata(Some(&metadata));

        assert_eq!(
            placeholders,
            vec![ImagePlaceholder {
                mime_type: "image/png".to_string(),
                name: Some("old.png".to_string())
            }]
        );
        let content = images::user_content_with_image_placeholders("see attached", &placeholders);
        assert!(
            content
                .to_string()
                .contains("[Attached image/png: old.png]")
        );
        assert!(!content.to_string().contains(&"a".repeat(256)));
    }
}
