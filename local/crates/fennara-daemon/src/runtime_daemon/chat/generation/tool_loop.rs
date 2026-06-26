use axum::extract::ws::Message;
use futures_util::Sink;
use serde_json::{Value, json};

use crate::runtime_daemon::state::AppState;

use super::super::{BoundChatProject, send_chat_list, send_error, send_json, store, tools};
use super::is_chat_cancelled;

pub(super) enum ToolLoopResult {
    Completed { provider_messages: Vec<Value> },
    Stopped,
}

pub(super) async fn run_tool_calls<S>(
    sender: &mut S,
    request_id: Option<String>,
    state: &AppState,
    bound_project: &BoundChatProject,
    scope: &store::ProjectScope,
    chat_id: &str,
    assistant_message_id: &str,
    generation_id: &str,
    assistant_content: &str,
    tool_calls: Vec<Value>,
) -> Result<ToolLoopResult, S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let mut provider_messages = Vec::new();
    for tool_call in tool_calls {
        if is_chat_cancelled(state, chat_id).await {
            let _ = store::finish_generation(generation_id, "cancelled", None);
            finish_cancelled_turn(
                sender,
                request_id,
                state,
                scope,
                chat_id,
                assistant_message_id,
                assistant_content,
            )
            .await?;
            return Ok(ToolLoopResult::Stopped);
        }

        let (tool_call_id, tool_name, arguments) = normalize_tool_call(&tool_call);
        let tool_status = if tools::is_allowed_tool(&tool_name) {
            "in_progress"
        } else {
            "failed"
        };
        if let Err(error) = store::upsert_tool_call(
            chat_id,
            assistant_message_id,
            Some(generation_id),
            &tool_call_id,
            &tool_name,
            &arguments,
            tool_status,
        ) {
            let error_json = json!({ "message": error });
            let _ = store::finish_generation(generation_id, "failed", Some(&error_json));
            send_error(sender, request_id, "chat_store_failed", &error).await?;
            return Ok(ToolLoopResult::Stopped);
        }
        send_json(
            sender,
            json!({
                "type": "chat_item_update",
                "request_id": request_id.clone(),
                "item": {
                    "id": tool_call_id,
                    "type": "function_call",
                    "name": tool_name,
                    "arguments": arguments.to_string(),
                    "status": "in_progress"
                }
            }),
        )
        .await?;

        let result = tools::execute(state, &bound_project.session_id, &tool_name, &arguments).await;
        if is_chat_cancelled(state, chat_id).await {
            let _ = store::finish_generation(generation_id, "cancelled", None);
            finish_cancelled_turn(
                sender,
                request_id,
                state,
                scope,
                chat_id,
                assistant_message_id,
                assistant_content,
            )
            .await?;
            return Ok(ToolLoopResult::Stopped);
        }

        let status = if result.ok { "done" } else { "failed" };
        if let Err(error) = store::finish_tool_call(
            &tool_call_id,
            status,
            &result.raw_result,
            &result.mcp_markdown,
            &result.plugin_markdown,
            &result.metadata,
            &result.target_keys,
        )
        .and_then(|_| {
            store::insert_tool_message(
                chat_id,
                &tool_call_id,
                &tool_name,
                status,
                &result.plugin_markdown,
                &result.metadata,
            )
            .map(|_| ())
        }) {
            let error_json = json!({ "message": error });
            let _ = store::finish_generation(generation_id, "failed", Some(&error_json));
            send_error(sender, request_id, "chat_store_failed", &error).await?;
            return Ok(ToolLoopResult::Stopped);
        }
        send_json(
            sender,
            json!({
                "type": "chat_item_update",
                "request_id": request_id.clone(),
                "item": {
                    "id": tool_call_id,
                    "type": "tool_result",
                    "name": tool_name,
                    "content": result.plugin_markdown,
                    "status": status
                }
            }),
        )
        .await?;
        provider_messages.push(json!({
            "role": "tool",
            "tool_call_id": tool_call_id,
            "name": tool_name,
            "content": result.mcp_markdown
        }));
        provider_messages.extend(result.model_followup_messages.clone());
    }

    Ok(ToolLoopResult::Completed { provider_messages })
}

pub(super) async fn finish_cancelled_turn<S>(
    sender: &mut S,
    request_id: Option<String>,
    state: &AppState,
    scope: &store::ProjectScope,
    chat_id: &str,
    assistant_message_id: &str,
    partial: &str,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let stored = match store::cancel_turn(chat_id, assistant_message_id, partial) {
        Ok(message) => message,
        Err(error) => {
            return send_error(sender, request_id, "chat_store_failed", &error).await;
        }
    };
    state.cancelled_chats.write().await.remove(chat_id);
    send_json(
        sender,
        json!({
            "type": "chat_cancelled",
            "request_id": request_id.clone(),
            "chat_id": chat_id,
            "message": stored,
            "response": partial
        }),
    )
    .await?;
    send_chat_list(sender, None, scope).await
}

fn normalize_tool_call(tool_call: &Value) -> (String, String, Value) {
    let tool_call_id = tool_call
        .get("id")
        .and_then(Value::as_str)
        .unwrap_or("tool_call")
        .to_string();
    let function = tool_call.get("function").unwrap_or(&Value::Null);
    let tool_name = function
        .get("name")
        .and_then(Value::as_str)
        .unwrap_or("unknown")
        .to_string();
    let arguments = function
        .get("arguments")
        .and_then(Value::as_str)
        .and_then(|raw| serde_json::from_str::<Value>(raw).ok())
        .filter(Value::is_object)
        .unwrap_or_else(|| json!({}));
    (tool_call_id, tool_name, arguments)
}
