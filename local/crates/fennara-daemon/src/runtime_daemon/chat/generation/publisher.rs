use axum::extract::ws::Message;
use futures_util::Sink;
use serde_json::{Value, json};
use tokio::sync::{mpsc, oneshot};

use crate::runtime_daemon::state::AppState;

use super::super::{
    providers::{ChatCompletion, ChatRequest, LlmError, ProviderSettings, StreamItem, stream_chat},
    send_json, tools,
};
use super::is_chat_cancelled;

pub(super) struct StreamedAssistant {
    pub(super) completion: ChatCompletion,
    pub(super) usage: Option<Value>,
    pub(super) reasoning_content: Option<String>,
}

pub(super) async fn stream_one_assistant<S>(
    sender: &mut S,
    request_id: Option<String>,
    provider_settings: ProviderSettings,
    model: &str,
    reasoning_effort: &str,
    provider_messages: &[Value],
    assistant_message_id: &str,
    state: &AppState,
    chat_id: &str,
) -> Result<Result<StreamedAssistant, LlmError>, S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let (item_tx, mut item_rx) = mpsc::unbounded_channel::<StreamItem>();
    let (done_tx, done_rx) = oneshot::channel::<Result<ChatCompletion, LlmError>>();
    let model_for_task = model.to_string();
    let reasoning_effort_for_task = reasoning_effort.to_string();
    let messages_for_task = provider_messages.to_vec();
    let tools_for_task = tools::definitions();
    let state_for_task = state.clone();
    let chat_id_for_task = chat_id.to_string();

    tokio::spawn(async move {
        let result = stream_chat(
            &provider_settings,
            &ChatRequest {
                model: model_for_task,
                reasoning_effort: reasoning_effort_for_task,
                messages: messages_for_task,
                tools: tools_for_task,
            },
            |item| {
                let item_tx = item_tx.clone();
                let state_for_item = state_for_task.clone();
                let chat_id_for_item = chat_id_for_task.clone();
                async move {
                    if is_chat_cancelled(&state_for_item, &chat_id_for_item).await {
                        return Ok(false);
                    }
                    item_tx.send(item).map_err(|_| LlmError::Config {
                        message: "Chat websocket disconnected.".to_string(),
                    })?;
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
                        let clean_content = content.trim();
                        if clean_content.is_empty() {
                            continue;
                        }
                        reasoning_content = Some(clean_content.to_string());
                        send_json(
                            sender,
                            json!({
                                "type": "chat_item_update",
                                "request_id": request_id.clone(),
                                "item": {
                                    "id": "reasoning",
                                    "type": "reasoning",
                                    "content": clean_content,
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
                break result.unwrap_or_else(|_| Err(LlmError::ProviderApi {
                    provider: "chat".to_string(),
                    status: None,
                    message: "Chat provider task ended unexpectedly.".to_string(),
                    retryable: false,
                }));
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
