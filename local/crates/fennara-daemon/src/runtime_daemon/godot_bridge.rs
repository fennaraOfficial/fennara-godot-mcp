use axum::{
    Json,
    extract::{
        State,
        ws::{Message, WebSocket, WebSocketUpgrade},
    },
    response::IntoResponse,
};
use futures_util::{SinkExt, StreamExt};
use serde::Deserialize;
use serde_json::{Value, json};
use std::{sync::atomic::Ordering, time::Duration};
use tokio::sync::{mpsc, oneshot};

use super::{
    DAEMON_VERSION,
    docs_cache::handle_docs_warmup_request,
    state::{AppState, DaemonStatus, GodotProjectStatus, PendingToolCall},
    util::{optional_string, string_array},
};

#[derive(Debug, Deserialize)]
pub(crate) struct ToolCallRequest {
    tool: String,
    args: Value,
}

pub(crate) async fn status(State(state): State<AppState>) -> Json<DaemonStatus> {
    Json(current_status(&state).await)
}

pub(crate) async fn call_tool(
    State(state): State<AppState>,
    Json(request): Json<ToolCallRequest>,
) -> Json<Value> {
    Json(call_tool_value(&state, &request.tool, request.args).await)
}

pub(crate) async fn call_tool_value(state: &AppState, tool: &str, args: Value) -> Value {
    let request_id = format!(
        "local-tool-{}",
        state.request_counter.fetch_add(1, Ordering::Relaxed) + 1
    );
    let (session_id, sender) = match select_target_session(state).await {
        Ok(target) => target,
        Err(error) => return json!({ "ok": false, "error": error }),
    };

    let (response_tx, response_rx) = oneshot::channel();
    state.pending_tool_calls.write().await.insert(
        request_id.clone(),
        PendingToolCall {
            session_id: session_id.clone(),
            sender: response_tx,
        },
    );

    let payload = json!({
        "type": "tool_call",
        "request_id": request_id,
        "session_id": session_id,
        "tool": tool,
        "args": args
    });

    if sender
        .send(Message::Text(payload.to_string().into()))
        .is_err()
    {
        state.pending_tool_calls.write().await.remove(&request_id);
        return json!({
            "ok": false,
            "error": "Failed to send tool call to the Godot plugin."
        });
    }

    match tokio::time::timeout(Duration::from_secs(295), response_rx).await {
        Ok(Ok(response)) => response,
        Ok(Err(_)) => json!({
            "ok": false,
            "error": "Godot plugin disconnected before returning a tool result."
        }),
        Err(_) => {
            state.pending_tool_calls.write().await.remove(&request_id);
            json!({
                "ok": false,
                "error": "Timed out waiting for the Godot plugin tool result."
            })
        }
    }
}

pub(crate) async fn begin_snapshot_turn(
    state: &AppState,
    chat_id: &str,
    user_message: &str,
) -> Value {
    call_plugin_request(
        state,
        json!({
            "type": "snapshot_begin_turn",
            "chat_id": chat_id,
            "user_message": user_message
        }),
        Duration::from_secs(10),
    )
    .await
}

pub(crate) async fn revert_snapshot_turn(state: &AppState, chat_id: &str) -> Value {
    call_plugin_request(
        state,
        json!({
            "type": "snapshot_revert",
            "chat_id": chat_id
        }),
        Duration::from_secs(30),
    )
    .await
}

async fn call_plugin_request(state: &AppState, mut payload: Value, timeout: Duration) -> Value {
    let request_id = format!(
        "local-plugin-{}",
        state.request_counter.fetch_add(1, Ordering::Relaxed) + 1
    );
    let (session_id, sender) = match select_target_session(state).await {
        Ok(target) => target,
        Err(error) => return json!({ "ok": false, "error": error }),
    };

    let (response_tx, response_rx) = oneshot::channel();
    state.pending_tool_calls.write().await.insert(
        request_id.clone(),
        PendingToolCall {
            session_id: session_id.clone(),
            sender: response_tx,
        },
    );

    payload["request_id"] = json!(request_id);
    payload["session_id"] = json!(session_id);

    if sender
        .send(Message::Text(payload.to_string().into()))
        .is_err()
    {
        state.pending_tool_calls.write().await.remove(
            payload
                .get("request_id")
                .and_then(Value::as_str)
                .unwrap_or_default(),
        );
        return json!({
            "ok": false,
            "error": "Failed to send request to the Godot plugin."
        });
    }

    match tokio::time::timeout(timeout, response_rx).await {
        Ok(Ok(response)) => response,
        Ok(Err(_)) => json!({
            "ok": false,
            "error": "Godot plugin disconnected before returning a response."
        }),
        Err(_) => {
            state.pending_tool_calls.write().await.remove(
                payload
                    .get("request_id")
                    .and_then(Value::as_str)
                    .unwrap_or_default(),
            );
            json!({
                "ok": false,
                "error": "Timed out waiting for the Godot plugin response."
            })
        }
    }
}

pub(crate) async fn godot_ws(
    ws: WebSocketUpgrade,
    State(state): State<AppState>,
) -> impl IntoResponse {
    ws.on_upgrade(move |socket| handle_godot_socket(socket, state))
}

async fn handle_godot_socket(socket: WebSocket, state: AppState) {
    let connection_id = state.connection_counter.fetch_add(1, Ordering::Relaxed) + 1;
    let fallback_session_id = format!("connection-{connection_id}");
    let mut session_id: Option<String> = None;
    let (mut ws_sender, mut ws_receiver) = socket.split();
    let (outbound_tx, mut outbound_rx) = mpsc::unbounded_channel::<Message>();

    let writer = tokio::spawn(async move {
        while let Some(message) = outbound_rx.recv().await {
            if ws_sender.send(message).await.is_err() {
                break;
            }
        }
    });

    while let Some(message) = ws_receiver.next().await {
        match message {
            Ok(Message::Text(text)) => {
                if let Ok(value) = serde_json::from_str::<Value>(&text) {
                    if value.get("type").and_then(Value::as_str) == Some("hello") {
                        let next_session_id = optional_string(&value, "session_id")
                            .unwrap_or_else(|| fallback_session_id.clone());
                        let project = GodotProjectStatus {
                            session_id: next_session_id.clone(),
                            project_name: optional_string(&value, "project_name"),
                            project_path: optional_string(&value, "project_path"),
                            godot_version: optional_string(&value, "godot_version"),
                            plugin_version: optional_string(&value, "plugin_version"),
                            chat_token: optional_string(&value, "chat_token"),
                            tools: string_array(&value, "tools"),
                        };

                        session_id = Some(next_session_id.clone());
                        state
                            .godot_senders
                            .write()
                            .await
                            .insert(next_session_id.clone(), outbound_tx.clone());
                        state
                            .projects
                            .write()
                            .await
                            .insert(next_session_id.clone(), project);
                        ensure_active_project_after_connect(&state, &next_session_id).await;
                        broadcast_active_project_changed(&state).await;
                    } else if matches!(
                        value.get("type").and_then(Value::as_str),
                        Some("tool_result" | "snapshot_result")
                    ) {
                        if let Some(request_id) = value.get("request_id").and_then(Value::as_str) {
                            if let Some(pending) =
                                state.pending_tool_calls.write().await.remove(request_id)
                            {
                                let _ = pending.sender.send(value);
                            }
                        }
                    } else if value.get("type").and_then(Value::as_str)
                        == Some("set_active_project")
                    {
                        if let Some(next_session_id) = value
                            .get("session_id")
                            .and_then(Value::as_str)
                            .or(session_id.as_deref())
                        {
                            if state.projects.read().await.contains_key(next_session_id) {
                                *state.active_session_id.write().await =
                                    Some(next_session_id.to_string());
                                *state.active_project_explicit.write().await = true;
                                broadcast_active_project_changed(&state).await;
                            }
                        }
                    } else if value.get("type").and_then(Value::as_str)
                        == Some("warm_get_class_info_docs")
                    {
                        handle_docs_warmup_request(&state, &value).await;
                    }
                }
            }
            Ok(Message::Close(_)) => break,
            Ok(_) => {}
            Err(_) => break,
        }
    }

    writer.abort();
    if let Some(session_id) = session_id {
        state.godot_senders.write().await.remove(&session_id);
        state.projects.write().await.remove(&session_id);

        let mut active = state.active_session_id.write().await;
        if active.as_deref() == Some(session_id.as_str()) {
            *active = None;
        }
        drop(active);

        let pending = {
            let mut pending = state.pending_tool_calls.write().await;
            let ids: Vec<String> = pending
                .iter()
                .filter_map(|(request_id, call)| {
                    (call.session_id == session_id).then(|| request_id.clone())
                })
                .collect();
            ids.into_iter()
                .filter_map(|request_id| pending.remove(&request_id))
                .collect::<Vec<_>>()
        };
        for pending in pending {
            let _ = pending.sender.send(json!({
                "ok": false,
                "error": "Godot plugin disconnected."
            }));
        }

        normalize_active_project_after_disconnect(&state).await;
        broadcast_active_project_changed(&state).await;
        schedule_idle_shutdown_if_empty(state.clone()).await;
    }
}

async fn current_status(state: &AppState) -> DaemonStatus {
    let projects = state.projects.read().await;
    let active_session_id = state.active_session_id.read().await.clone();
    let mut connected_projects: Vec<GodotProjectStatus> = projects.values().cloned().collect();
    connected_projects.sort_by(|a, b| {
        a.project_name
            .clone()
            .unwrap_or_default()
            .cmp(&b.project_name.clone().unwrap_or_default())
    });
    let active_project = active_session_id
        .as_ref()
        .and_then(|session_id| projects.get(session_id))
        .cloned();

    DaemonStatus {
        ok: true,
        daemon: "fennara-daemon",
        version: DAEMON_VERSION,
        godot_plugin_connected: !projects.is_empty(),
        active_project,
        active_session_id,
        connected_projects,
    }
}

async fn select_target_session(
    state: &AppState,
) -> Result<(String, mpsc::UnboundedSender<Message>), String> {
    let senders = state.godot_senders.read().await;
    if senders.is_empty() {
        return Err("Open a Godot project with Fennara enabled.".to_string());
    }

    if let Some(active_session_id) = state.active_session_id.read().await.clone() {
        if let Some(sender) = senders.get(&active_session_id) {
            return Ok((active_session_id, sender.clone()));
        }
    }

    if senders.len() == 1 {
        let (session_id, sender) = senders.iter().next().expect("single sender should exist");
        return Ok((session_id.clone(), sender.clone()));
    }

    Err("Multiple Fennara projects are open. In the Fennara dock, choose Set as MCP target for the project you want to control.".to_string())
}

async fn ensure_active_project_after_connect(state: &AppState, session_id: &str) {
    let project_count = state.projects.read().await.len();
    let mut active = state.active_session_id.write().await;
    let mut explicit = state.active_project_explicit.write().await;

    if project_count == 1 {
        *active = Some(session_id.to_string());
        *explicit = false;
    } else if !*explicit {
        *active = None;
    } else if active.is_none() {
        *active = Some(session_id.to_string());
    }
}

async fn normalize_active_project_after_disconnect(state: &AppState) {
    let projects = state.projects.read().await;
    let mut active = state.active_session_id.write().await;
    let mut explicit = state.active_project_explicit.write().await;

    if projects.len() == 1 {
        *active = projects.keys().next().cloned();
        *explicit = false;
    } else if active
        .as_ref()
        .is_some_and(|session_id| !projects.contains_key(session_id))
    {
        *active = None;
        *explicit = false;
    }
}

async fn broadcast_active_project_changed(state: &AppState) {
    let active_session_id = state.active_session_id.read().await.clone();
    let active_project = {
        let projects = state.projects.read().await;
        active_session_id
            .as_ref()
            .and_then(|session_id| projects.get(session_id))
            .cloned()
    };
    let senders = state.godot_senders.read().await;
    for (session_id, sender) in senders.iter() {
        let payload = json!({
            "type": "active_project_changed",
            "active_session_id": active_session_id,
            "active_project_name": active_project.as_ref().and_then(|project| project.project_name.clone()),
            "active_project_path": active_project.as_ref().and_then(|project| project.project_path.clone()),
            "session_id": session_id,
            "is_active": active_session_id.as_deref() == Some(session_id.as_str())
        });
        let _ = sender.send(Message::Text(payload.to_string().into()));
    }
}

async fn schedule_idle_shutdown_if_empty(state: AppState) {
    if !state.projects.read().await.is_empty() {
        return;
    }

    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_secs(8)).await;
        if !state.projects.read().await.is_empty() {
            return;
        }

        if let Some(sender) = state.shutdown_sender.lock().await.take() {
            let _ = sender.send(());
        }
    });
}
