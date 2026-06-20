use axum::{
    extract::{
        Query, State,
        ws::{Message, WebSocket, WebSocketUpgrade},
    },
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
};
use futures_util::{Sink, SinkExt, StreamExt};
use serde::Deserialize;
use serde_json::{Value, json};
use tokio::sync::{mpsc, oneshot};

use super::state::{AppState, GodotProjectStatus};
use crate::runtime_daemon::godot_bridge;

mod ids;
mod models;
mod openrouter;
mod schema;
mod settings;
mod store;
mod tools;

use openrouter::{StreamItem, build_messages, stream_chat};
use settings::{SaveSettingsRequest, load_settings, save_settings};

#[derive(Debug, Deserialize)]
pub(crate) struct ChatWsQuery {
    chat_token: Option<String>,
}

#[derive(Debug, Deserialize)]
struct ClientRequest {
    #[serde(rename = "type")]
    request_type: String,
    request_id: Option<String>,
    chat_id: Option<String>,
    message: Option<String>,
    model: Option<String>,
    reasoning_effort: Option<String>,
    openrouter_api_key: Option<String>,
}

pub(crate) async fn chat_ws(
    ws: WebSocketUpgrade,
    Query(query): Query<ChatWsQuery>,
    headers: HeaderMap,
    State(state): State<AppState>,
) -> Response {
    if !is_allowed_browser_origin(&headers)
        || !chat_token_matches(&state, query.chat_token.as_deref()).await
    {
        return StatusCode::FORBIDDEN.into_response();
    }
    ws.on_upgrade(move |socket| handle_chat_socket(socket, state))
        .into_response()
}

fn is_allowed_browser_origin(headers: &HeaderMap) -> bool {
    let Some(origin) = headers.get("origin").and_then(|value| value.to_str().ok()) else {
        return true;
    };
    origin == "null" || origin.starts_with("file://")
}

async fn chat_token_matches(state: &AppState, token: Option<&str>) -> bool {
    let Some(token) = token.filter(|value| !value.is_empty()) else {
        return false;
    };
    state
        .projects
        .read()
        .await
        .values()
        .any(|project| project.chat_token.as_deref() == Some(token))
}

async fn handle_chat_socket(socket: WebSocket, state: AppState) {
    let (mut sender, mut receiver) = socket.split();
    let mut active_chat_id: Option<String> = None;

    if send_initial_state(&mut sender, &mut active_chat_id, &state)
        .await
        .is_err()
    {
        return;
    }
    while let Some(message) = receiver.next().await {
        match message {
            Ok(Message::Text(text)) => {
                let Ok(request) = serde_json::from_str::<ClientRequest>(&text) else {
                    let _ =
                        send_error(&mut sender, None, "bad_request", "Invalid chat request.").await;
                    continue;
                };
                if handle_request(&mut sender, &mut active_chat_id, &state, request)
                    .await
                    .is_err()
                {
                    break;
                }
            }
            Ok(Message::Close(_)) => break,
            Ok(_) => {}
            Err(_) => break,
        }
    }
}

async fn send_initial_state<S>(
    sender: &mut S,
    active_chat_id: &mut Option<String>,
    state: &AppState,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let settings = load_settings();
    send_json(
        sender,
        json!({
            "type": "settings",
            "request_id": null,
            "settings": settings.public()
        }),
    )
    .await?;
    let scope = project_scope(state).await;
    match store::open_active_or_create(&scope, &settings.model, &settings.reasoning_effort) {
        Ok(opened) => {
            *active_chat_id = Some(opened.chat.id.clone());
            send_json(
                sender,
                json!({
                    "type": "chat_opened",
                    "request_id": null,
                    "chat": opened.chat,
                    "messages": opened.messages,
                    "can_revert": chat_can_revert(state, &opened.chat.id).await
                }),
            )
            .await?;
        }
        Err(error) => {
            send_error(sender, None, "chat_store_failed", &error).await?;
        }
    }
    send_chat_list(sender, None, &scope).await
}

async fn handle_request<S>(
    sender: &mut S,
    active_chat_id: &mut Option<String>,
    state: &AppState,
    request: ClientRequest,
) -> Result<(), ()>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let request_id = request.request_id.clone();
    match request.request_type.as_str() {
        "get_settings" => {
            send_json(
                sender,
                json!({
                    "type": "settings",
                    "request_id": request_id,
                    "settings": load_settings().public()
                }),
            )
            .await
        }
        "list_chats" => {
            let scope = project_scope(state).await;
            send_chat_list(sender, request_id, &scope).await
        }
        "list_models" => {
            let settings = load_settings();
            let catalog = models::list_models(&settings).await;
            send_json(
                sender,
                json!({
                    "type": "model_list",
                    "request_id": request_id,
                    "catalog": catalog
                }),
            )
            .await
        }
        "open_chat" => {
            let Some(chat_id) = request.chat_id.as_deref() else {
                return send_error(sender, request_id, "bad_request", "chat_id is required.")
                    .await
                    .map_err(|_| ());
            };
            let scope = project_scope(state).await;
            match store::open_chat(&scope, chat_id) {
                Ok(opened) => {
                    *active_chat_id = Some(opened.chat.id.clone());
                    send_json(
                        sender,
                        json!({
                            "type": "chat_opened",
                            "request_id": request_id,
                            "chat": opened.chat,
                            "messages": opened.messages,
                            "can_revert": chat_can_revert(state, &opened.chat.id).await
                        }),
                    )
                    .await
                }
                Err(error) => send_error(sender, request_id, "chat_open_failed", &error).await,
            }
        }
        "save_settings" => {
            let update = SaveSettingsRequest {
                openrouter_api_key: request.openrouter_api_key,
                model: request.model,
                reasoning_effort: request.reasoning_effort,
            };
            match save_settings(update) {
                Ok(settings) => {
                    send_json(
                        sender,
                        json!({
                            "type": "settings_saved",
                            "request_id": request_id,
                            "settings": settings.public()
                        }),
                    )
                    .await
                }
                Err(error) => send_error(sender, request_id, "settings_save_failed", &error).await,
            }
        }
        "new_chat" => {
            let settings = load_settings();
            let scope = project_scope(state).await;
            match store::create_chat(&scope, &settings.model, &settings.reasoning_effort) {
                Ok(opened) => {
                    *active_chat_id = Some(opened.chat.id.clone());
                    if send_json(
                        sender,
                        json!({
                            "type": "chat_created",
                            "request_id": request_id.clone(),
                            "chat": opened.chat
                        }),
                    )
                    .await
                    .is_err()
                    {
                        return Err(());
                    }
                    if send_json(
                        sender,
                        json!({
                            "type": "chat_opened",
                            "request_id": request_id,
                            "chat": opened.chat,
                            "messages": opened.messages,
                            "can_revert": false
                        }),
                    )
                    .await
                    .is_err()
                    {
                        return Err(());
                    }
                    send_chat_list(sender, None, &scope).await
                }
                Err(error) => send_error(sender, request_id, "chat_create_failed", &error).await,
            }
        }
        "delete_chat" => {
            let Some(chat_id) = request.chat_id.as_deref() else {
                return send_error(sender, request_id, "bad_request", "chat_id is required.")
                    .await
                    .map_err(|_| ());
            };
            let scope = project_scope(state).await;
            match store::archive_chat(&scope, chat_id) {
                Ok(()) => {
                    if active_chat_id.as_deref() == Some(chat_id) {
                        *active_chat_id = None;
                    }
                    state.revertable_chats.write().await.remove(chat_id);
                    send_chat_list(sender, request_id, &scope).await
                }
                Err(error) => send_error(sender, request_id, "chat_delete_failed", &error).await,
            }
        }
        "cancel_chat" => {
            let Some(chat_id) = request.chat_id.or_else(|| active_chat_id.clone()) else {
                return send_error(sender, request_id, "bad_request", "chat_id is required.")
                    .await
                    .map_err(|_| ());
            };
            state.cancelled_chats.write().await.insert(chat_id);
            send_json(
                sender,
                json!({
                    "type": "chat_cancel_requested",
                    "request_id": request_id
                }),
            )
            .await
        }
        "revert_chat" => {
            let Some(chat_id) = request.chat_id.or_else(|| active_chat_id.clone()) else {
                return send_error(sender, request_id, "bad_request", "chat_id is required.")
                    .await
                    .map_err(|_| ());
            };
            let scope = project_scope(state).await;
            if let Err(error) = store::ensure_chat_in_scope(&scope, &chat_id) {
                return send_error(sender, request_id, "chat_scope_mismatch", &error)
                    .await
                    .map_err(|_| ());
            }
            if !chat_can_revert(state, &chat_id).await {
                return send_error(
                    sender,
                    request_id,
                    "revert_unavailable",
                    "No active live revert snapshot is available for this chat.",
                )
                .await
                .map_err(|_| ());
            }
            let fallback_restored_message = store::last_user_message_content(&chat_id)
                .ok()
                .flatten()
                .unwrap_or_default();
            let snapshot_result = godot_bridge::revert_snapshot_turn(state, &chat_id).await;
            if snapshot_result.get("ok").and_then(Value::as_bool) == Some(false) {
                let error = snapshot_result
                    .get("error")
                    .and_then(Value::as_str)
                    .unwrap_or("Failed to revert the last file snapshot.");
                return send_error(sender, request_id, "revert_failed", error)
                    .await
                    .map_err(|_| ());
            }
            match store::revert_last_turn(&scope, &chat_id) {
                Ok(opened) => {
                    state.revertable_chats.write().await.remove(&chat_id);
                    let restored_message = snapshot_result
                        .get("restored_message")
                        .and_then(Value::as_str)
                        .filter(|message| !message.is_empty())
                        .unwrap_or(&fallback_restored_message);
                    *active_chat_id = Some(opened.chat.id.clone());
                    if send_json(
                        sender,
                        json!({
                            "type": "chat_opened",
                            "request_id": request_id.clone(),
                            "chat": opened.chat,
                            "messages": opened.messages,
                            "can_revert": false,
                            "reverted": true,
                            "restored_message": restored_message
                        }),
                    )
                    .await
                    .is_err()
                    {
                        return Err(());
                    }
                    send_chat_list(sender, None, &scope).await
                }
                Err(error) => send_error(sender, request_id, "revert_failed", &error).await,
            }
        }
        "send_chat" => run_chat(sender, active_chat_id, state, request).await,
        _ => {
            send_error(
                sender,
                request_id,
                "bad_request",
                "Unsupported chat request type.",
            )
            .await
        }
    }
    .map_err(|_| ())
}

async fn run_chat<S>(
    sender: &mut S,
    active_chat_id: &mut Option<String>,
    state: &AppState,
    request: ClientRequest,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let request_id = request.request_id.clone();
    let message = request.message.unwrap_or_default();
    let message = message.trim();
    if message.is_empty() {
        return send_error(sender, request_id, "bad_request", "Message is required.").await;
    }

    let settings = load_settings();
    let Some(api_key) = settings
        .openrouter_api_key
        .as_ref()
        .filter(|key| !key.trim().is_empty())
    else {
        return send_error(
            sender,
            request_id,
            "missing_openrouter_key",
            "Add your OpenRouter API key in settings first.",
        )
        .await;
    };

    let model = request.model.unwrap_or(settings.model);
    let scope = project_scope(state).await;
    let reasoning_effort = settings::clean_reasoning_effort(
        request
            .reasoning_effort
            .as_deref()
            .unwrap_or(&settings.reasoning_effort),
    )
    .to_string();
    let chat_id = match request.chat_id.or_else(|| active_chat_id.clone()) {
        Some(chat_id) => chat_id,
        None => match store::create_chat(&scope, &model, &reasoning_effort) {
            Ok(opened) => opened.chat.id,
            Err(error) => {
                return send_error(sender, request_id, "chat_create_failed", &error).await;
            }
        },
    };
    if let Err(error) = store::ensure_chat_in_scope(&scope, &chat_id) {
        return send_error(sender, request_id, "chat_scope_mismatch", &error).await;
    }
    state.cancelled_chats.write().await.remove(&chat_id);
    *active_chat_id = Some(chat_id.clone());
    let replay_messages = match store::replay_messages(&chat_id) {
        Ok(messages) => messages,
        Err(error) => return send_error(sender, request_id, "chat_replay_failed", &error).await,
    };
    let snapshot_result = godot_bridge::begin_snapshot_turn(state, &chat_id, message).await;
    if snapshot_result.get("ok").and_then(Value::as_bool) == Some(false) {
        let error = snapshot_result
            .get("error")
            .and_then(Value::as_str)
            .unwrap_or("Failed to begin a local revert snapshot.");
        return send_error(sender, request_id, "snapshot_failed", error).await;
    }
    state.revertable_chats.write().await.insert(chat_id.clone());
    let mut openrouter_messages = build_messages(&replay_messages, message);
    let user_message = match store::insert_user_message(&chat_id, message) {
        Ok(message) => message,
        Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
    };
    let chat_summary = match store::chat_summary(&chat_id) {
        Ok(chat) => chat,
        Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
    };
    send_json(
        sender,
        json!({
            "type": "chat_updated",
            "request_id": request_id.clone(),
            "chat": chat_summary
        }),
    )
    .await?;
    send_chat_list(sender, None, &scope).await?;

    let assistant_message = match store::insert_assistant_placeholder(&chat_id) {
        Ok(message) => message,
        Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
    };
    send_json(
        sender,
        json!({
            "type": "chat_stream_start",
            "request_id": request_id.clone(),
            "chat_id": chat_id,
            "user_message": user_message,
            "assistant_message": assistant_message,
            "model": model,
            "reasoning_effort": reasoning_effort,
            "can_revert": true
        }),
    )
    .await?;

    let mut current_assistant = assistant_message;

    let (final_usage, final_text, stored_assistant) = loop {
        let streamed = stream_one_assistant(
            sender,
            request_id.clone(),
            &api_key,
            &model,
            &reasoning_effort,
            &openrouter_messages,
            &current_assistant.id,
            state,
            &chat_id,
        )
        .await;

        let streamed = match streamed {
            Ok(Ok(streamed)) => streamed,
            Ok(Err(error)) => {
                let _ = store::fail_assistant_message(
                    &current_assistant.id,
                    &format!("Request failed: {error}"),
                );
                return send_error(sender, request_id, "openrouter_error", &error).await;
            }
            Err(error) => return Err(error),
        };

        if is_chat_cancelled(state, &chat_id).await {
            let partial = streamed.completion.content.clone();
            let stored = match store::cancel_turn(&chat_id, &current_assistant.id, &partial) {
                Ok(message) => message,
                Err(error) => {
                    return send_error(sender, request_id, "chat_store_failed", &error).await;
                }
            };
            state.cancelled_chats.write().await.remove(&chat_id);
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
            return send_chat_list(sender, None, &scope).await;
        }

        let usage = streamed.usage.clone();
        let tool_calls = streamed.completion.tool_calls;
        if tool_calls.is_empty() {
            let final_text = streamed.completion.content.clone();
            let stored_assistant = match store::finish_assistant_message(
                &current_assistant.id,
                &final_text,
                streamed.reasoning_content.as_deref(),
                usage.as_ref(),
                &model,
            ) {
                Ok(message) => message,
                Err(error) => {
                    return send_error(sender, request_id, "chat_store_failed", &error).await;
                }
            };
            break (usage, final_text, stored_assistant);
        }

        let tool_calls_value = Value::Array(tool_calls.clone());
        if let Err(error) =
            store::set_assistant_tool_calls(&current_assistant.id, &tool_calls_value).and_then(
                |_| {
                    store::finish_assistant_message(
                        &current_assistant.id,
                        &streamed.completion.content,
                        streamed.reasoning_content.as_deref(),
                        usage.as_ref(),
                        &model,
                    )
                    .map(|_| ())
                },
            )
        {
            return send_error(sender, request_id, "chat_store_failed", &error).await;
        }
        send_chat_updated(sender, request_id.clone(), &chat_id).await?;
        openrouter_messages.push(json!({
            "role": "assistant",
            "content": streamed.completion.content,
            "tool_calls": tool_calls
        }));

        for tool_call in tool_calls {
            if is_chat_cancelled(state, &chat_id).await {
                return finish_cancelled_turn(
                    sender,
                    request_id,
                    state,
                    &scope,
                    &chat_id,
                    &current_assistant.id,
                    &streamed.completion.content,
                )
                .await;
            }
            let (tool_call_id, tool_name, arguments) = normalize_tool_call(&tool_call);
            let tool_status = if tools::is_allowed_tool(&tool_name) {
                "in_progress"
            } else {
                "failed"
            };
            if let Err(error) = store::upsert_tool_call(
                &chat_id,
                &current_assistant.id,
                &tool_call_id,
                &tool_name,
                &arguments,
                tool_status,
            ) {
                return send_error(sender, request_id, "chat_store_failed", &error).await;
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

            let result = tools::execute(state, &tool_name, &arguments).await;
            if is_chat_cancelled(state, &chat_id).await {
                return finish_cancelled_turn(
                    sender,
                    request_id,
                    state,
                    &scope,
                    &chat_id,
                    &current_assistant.id,
                    &streamed.completion.content,
                )
                .await;
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
                    &chat_id,
                    &tool_call_id,
                    &tool_name,
                    status,
                    &result.plugin_markdown,
                    &result.metadata,
                )
                .map(|_| ())
            }) {
                return send_error(sender, request_id, "chat_store_failed", &error).await;
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
            openrouter_messages.push(json!({
                "role": "tool",
                "tool_call_id": tool_call_id,
                "name": tool_name,
                "content": result.mcp_markdown
            }));
            openrouter_messages.extend(result.model_followup_messages.clone());
        }

        current_assistant = match store::insert_assistant_placeholder(&chat_id) {
            Ok(message) => message,
            Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
        };
    };

    send_json(
        sender,
        json!({
            "type": "chat_stream_done",
            "request_id": request_id.clone()
        }),
    )
    .await?;
    send_json(
        sender,
        json!({
            "type": "chat_response",
            "request_id": request_id.clone(),
            "chat_id": chat_id,
            "message": stored_assistant,
            "response": final_text,
            "usage": final_usage
        }),
    )
    .await?;
    send_chat_list(sender, None, &scope).await
}

async fn finish_cancelled_turn<S>(
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

struct StreamedAssistant {
    completion: openrouter::ChatCompletion,
    usage: Option<Value>,
    reasoning_content: Option<String>,
}

async fn stream_one_assistant<S>(
    sender: &mut S,
    request_id: Option<String>,
    api_key: &str,
    model: &str,
    reasoning_effort: &str,
    openrouter_messages: &[Value],
    assistant_message_id: &str,
    state: &AppState,
    chat_id: &str,
) -> Result<Result<StreamedAssistant, String>, S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let (item_tx, mut item_rx) = mpsc::unbounded_channel::<StreamItem>();
    let (done_tx, done_rx) = oneshot::channel::<Result<openrouter::ChatCompletion, String>>();
    let api_key = api_key.to_string();
    let model_for_task = model.to_string();
    let reasoning_effort_for_task = reasoning_effort.to_string();
    let messages_for_task = openrouter_messages.to_vec();
    let tools_for_task = tools::definitions();
    let state_for_task = state.clone();
    let chat_id_for_task = chat_id.to_string();

    tokio::spawn(async move {
        let result = stream_chat(
            &api_key,
            &model_for_task,
            &reasoning_effort_for_task,
            &messages_for_task,
            &tools_for_task,
            |item| {
                let item_tx = item_tx.clone();
                let state_for_item = state_for_task.clone();
                let chat_id_for_item = chat_id_for_task.clone();
                async move {
                    if is_chat_cancelled(&state_for_item, &chat_id_for_item).await {
                        return Ok(false);
                    }
                    item_tx
                        .send(item)
                        .map_err(|_| "Chat websocket disconnected.".to_string())?;
                    Ok(true)
                }
            },
        )
        .await;
        let _ = done_tx.send(result);
    });

    let mut usage: Option<Value> = None;
    let mut reasoning_content: Option<String> = None;
    let mut done_rx = done_rx;
    let completion = loop {
        tokio::select! {
            item = item_rx.recv() => {
                let Some(item) = item else {
                    continue;
                };
                match item {
                    StreamItem::Text { content, done } => {
                        send_json(
                            sender,
                            json!({
                                "type": "chat_item_update",
                                "request_id": request_id.clone(),
                                "item": {
                                    "id": assistant_message_id,
                                    "type": "message",
                                    "content": content,
                                    "status": if done { "done" } else { "in_progress" }
                                }
                            }),
                        )
                        .await?;
                    }
                    StreamItem::Reasoning { content, done } => {
                        reasoning_content = Some(content.clone());
                        send_json(
                            sender,
                            json!({
                                "type": "chat_item_update",
                                "request_id": request_id.clone(),
                                "item": {
                                    "id": "reasoning",
                                    "type": "reasoning",
                                    "content": content,
                                    "status": if done { "done" } else { "in_progress" }
                                }
                            }),
                        )
                        .await?;
                    }
                    StreamItem::FunctionCall { id, name, arguments, done } => {
                        send_json(
                            sender,
                            json!({
                                "type": "chat_item_update",
                                "request_id": request_id.clone(),
                                "item": {
                                    "id": id,
                                    "type": "function_call",
                                    "name": name,
                                    "arguments": arguments,
                                    "status": if done { "done" } else { "in_progress" }
                                }
                            }),
                        )
                        .await?;
                    }
                    StreamItem::Usage(next_usage) => {
                        usage = Some(next_usage);
                    }
                }
            }
            result = &mut done_rx => {
                break result.unwrap_or_else(|_| Err("OpenRouter chat task ended unexpectedly.".to_string()));
            }
        }
    };

    match completion {
        Ok(completion) => Ok(Ok(StreamedAssistant {
            completion,
            usage,
            reasoning_content,
        })),
        Err(error) => Ok(Err(error)),
    }
}

async fn is_chat_cancelled(state: &AppState, chat_id: &str) -> bool {
    state.cancelled_chats.read().await.contains(chat_id)
}

async fn chat_can_revert(state: &AppState, chat_id: &str) -> bool {
    state.revertable_chats.read().await.contains(chat_id)
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

async fn send_chat_list<S>(
    sender: &mut S,
    request_id: Option<String>,
    scope: &store::ProjectScope,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    match store::list_chats(scope) {
        Ok(chats) => {
            send_json(
                sender,
                json!({
                    "type": "chat_list",
                    "request_id": request_id,
                    "chats": chats
                }),
            )
            .await
        }
        Err(error) => send_error(sender, request_id, "chat_list_failed", &error).await,
    }
}

async fn send_chat_updated<S>(
    sender: &mut S,
    request_id: Option<String>,
    chat_id: &str,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    match store::chat_summary(chat_id) {
        Ok(chat) => {
            send_json(
                sender,
                json!({
                    "type": "chat_updated",
                    "request_id": request_id,
                    "chat": chat
                }),
            )
            .await
        }
        Err(error) => send_error(sender, request_id, "chat_store_failed", &error).await,
    }
}

async fn project_scope(state: &AppState) -> store::ProjectScope {
    active_project(state)
        .await
        .map(|project| store::ProjectScope {
            project_path: project.project_path,
            project_name: project.project_name,
        })
        .unwrap_or_default()
}

async fn active_project(state: &AppState) -> Option<GodotProjectStatus> {
    let active_session_id = state.active_session_id.read().await.clone();
    let projects = state.projects.read().await;
    active_session_id
        .as_ref()
        .and_then(|session_id| projects.get(session_id))
        .cloned()
}

async fn send_json<S>(sender: &mut S, payload: Value) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
{
    sender.send(Message::Text(payload.to_string().into())).await
}

async fn send_error<S>(
    sender: &mut S,
    request_id: Option<String>,
    code: &str,
    message: &str,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
{
    send_json(
        sender,
        json!({
            "type": "error",
            "request_id": request_id,
            "code": code,
            "message": message
        }),
    )
    .await
}
