use axum::extract::ws::Message;
use futures_util::Sink;
use serde_json::{Value, json};

use crate::runtime_daemon::{godot_bridge, state::AppState};

use super::super::{
    BoundChatProject, ClientRequest, images, providers, send_chat_list, send_chat_updated,
    send_error, send_json, settings, store,
};
use super::{
    cost, is_chat_cancelled,
    publisher::stream_one_assistant,
    request::build_provider_messages,
    tool_loop::{self, ToolLoopResult},
};

pub(in crate::runtime_daemon::chat) async fn run_chat<S>(
    sender: &mut S,
    active_chat_id: &mut Option<String>,
    state: &AppState,
    bound_project: &BoundChatProject,
    request: ClientRequest,
) -> Result<(), S::Error>
where
    S: Sink<Message> + Unpin,
    S::Error: std::fmt::Debug,
{
    let request_id = request.request_id.clone();
    let message = request.message.unwrap_or_default();
    let message = message.trim();
    let user_images = match images::validate_images(request.images) {
        Ok(images) => images,
        Err(error) => return send_error(sender, request_id, "bad_request", &error).await,
    };
    if message.is_empty() && user_images.is_empty() {
        return send_error(
            sender,
            request_id,
            "bad_request",
            "Message or image is required.",
        )
        .await;
    }

    let settings = settings::load_settings();
    let model = request
        .model
        .as_deref()
        .and_then(settings::clean_model)
        .unwrap_or_else(|| settings.model.clone());
    if let Some(error) = providers::missing_auth_for_model(&settings, &model) {
        return send_error(sender, request_id, error.code(), &error.user_message()).await;
    }
    let scope = &bound_project.scope;
    let reasoning_effort = settings::clean_reasoning_effort(
        request
            .reasoning_effort
            .as_deref()
            .unwrap_or(&settings.reasoning_effort),
    )
    .to_string();
    let chat_id = match request.chat_id.or_else(|| active_chat_id.clone()) {
        Some(chat_id) => chat_id,
        None => match store::create_chat(scope, &model, &reasoning_effort) {
            Ok(opened) => opened.chat.id,
            Err(error) => {
                return send_error(sender, request_id, "chat_create_failed", &error).await;
            }
        },
    };
    if let Err(error) = store::ensure_chat_in_scope(scope, &chat_id) {
        return send_error(sender, request_id, "chat_scope_mismatch", &error).await;
    }
    if let Err(error) = store::set_chat_model(&chat_id, &model, &reasoning_effort) {
        return send_error(sender, request_id, "chat_store_failed", &error).await;
    }
    state.cancelled_chats.write().await.remove(&chat_id);
    *active_chat_id = Some(chat_id.clone());
    let replay_messages = match store::replay_messages(&chat_id) {
        Ok(messages) => messages,
        Err(error) => return send_error(sender, request_id, "chat_replay_failed", &error).await,
    };
    let mut provider_messages = build_provider_messages(&replay_messages, message, &user_images);
    let snapshot_result = godot_bridge::begin_snapshot_turn_for_session(
        state,
        Some(&bound_project.session_id),
        &chat_id,
        message,
    )
    .await;
    if snapshot_result.get("ok").and_then(Value::as_bool) == Some(false) {
        let error = snapshot_result
            .get("error")
            .and_then(Value::as_str)
            .unwrap_or("Failed to begin a local revert snapshot.");
        return send_error(sender, request_id, "snapshot_failed", error).await;
    }
    state.revertable_chats.write().await.insert(chat_id.clone());

    let user_message = match store::insert_user_message(
        &chat_id,
        message,
        images::metadata_value(&user_images).as_ref(),
    ) {
        Ok(message) => message,
        Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
    };
    send_json(
        sender,
        json!({
            "type": "chat_user_message",
            "request_id": request_id.clone(),
            "chat_id": chat_id,
            "user_message": user_message
        }),
    )
    .await?;
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
    send_chat_list(sender, None, scope).await?;

    let assistant_message = match store::insert_assistant_placeholder(&chat_id) {
        Ok(message) => message,
        Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
    };
    let assistant_generation =
        match store::start_generation(&chat_id, &assistant_message.id, &model, &reasoning_effort) {
            Ok(generation) => generation,
            Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
        };
    send_json(
        sender,
        json!({
            "type": "chat_stream_start",
            "request_id": request_id.clone(),
            "chat_id": chat_id,
            "assistant_message": assistant_message,
            "model": model,
            "reasoning_effort": reasoning_effort,
            "can_revert": true
        }),
    )
    .await?;

    let mut current_assistant = assistant_message;
    let mut current_generation = assistant_generation;
    let provider_settings = providers::settings_from_chat(&settings);

    let (final_usage, final_text, stored_assistant) = loop {
        let streamed = stream_one_assistant(
            sender,
            request_id.clone(),
            provider_settings.clone(),
            &model,
            &reasoning_effort,
            &provider_messages,
            &current_assistant.id,
            state,
            &chat_id,
        )
        .await;

        let streamed = match streamed {
            Ok(Ok(streamed)) => streamed,
            Ok(Err(error)) => {
                let error_text = format!("Request failed: {}", error.user_message());
                let error_json = json!({
                    "code": error.code(),
                    "message": error.user_message()
                });
                let _ =
                    store::finish_generation(&current_generation.id, "failed", Some(&error_json));
                let _ = store::fail_assistant_message(&current_assistant.id, &error_text);
                send_json(
                    sender,
                    json!({
                        "type": "chat_item_update",
                        "request_id": request_id.clone(),
                        "item": {
                            "type": "message",
                            "content": error_text
                        }
                    }),
                )
                .await?;
                send_chat_updated(sender, request_id.clone(), &chat_id).await?;
                return send_error(sender, request_id, error.code(), &error.user_message()).await;
            }
            Err(error) => return Err(error),
        };

        if is_chat_cancelled(state, &chat_id).await {
            let partial = streamed.completion.content.clone();
            let _ = store::finish_generation(&current_generation.id, "cancelled", None);
            tool_loop::finish_cancelled_turn(
                sender,
                request_id,
                state,
                scope,
                &chat_id,
                &current_assistant.id,
                &partial,
            )
            .await?;
            return Ok(());
        }

        let usage = cost::usage_for_model(&provider_settings, &model, streamed.usage.as_ref());
        let tool_calls = streamed.completion.tool_calls;
        if tool_calls.is_empty() {
            let final_text = streamed.completion.content.clone();
            let stored_assistant = match store::finish_assistant_message(
                &current_assistant.id,
                &final_text,
                streamed.reasoning_content.as_deref(),
                Some(&usage),
                &model,
                Some(&current_generation.id),
            ) {
                Ok(message) => message,
                Err(error) => {
                    let error_json = json!({ "message": error });
                    let _ = store::finish_generation(
                        &current_generation.id,
                        "failed",
                        Some(&error_json),
                    );
                    return send_error(sender, request_id, "chat_store_failed", &error).await;
                }
            };
            if let Err(error) = store::finish_generation(&current_generation.id, "done", None) {
                return send_error(sender, request_id, "chat_store_failed", &error).await;
            }
            break (Some(usage), final_text, stored_assistant);
        }

        let tool_calls_value = Value::Array(tool_calls.clone());
        if let Err(error) =
            store::set_assistant_tool_calls(&current_assistant.id, &tool_calls_value).and_then(
                |_| {
                    store::finish_assistant_message(
                        &current_assistant.id,
                        &streamed.completion.content,
                        streamed.reasoning_content.as_deref(),
                        Some(&usage),
                        &model,
                        Some(&current_generation.id),
                    )
                    .map(|_| ())
                },
            )
        {
            let error_json = json!({ "message": error });
            let _ = store::finish_generation(&current_generation.id, "failed", Some(&error_json));
            return send_error(sender, request_id, "chat_store_failed", &error).await;
        }
        send_chat_updated(sender, request_id.clone(), &chat_id).await?;
        provider_messages.push(json!({
            "role": "assistant",
            "content": streamed.completion.content,
            "tool_calls": tool_calls
        }));

        match tool_loop::run_tool_calls(
            sender,
            request_id.clone(),
            state,
            bound_project,
            scope,
            &chat_id,
            &current_assistant.id,
            &current_generation.id,
            &streamed.completion.content,
            tool_calls,
        )
        .await?
        {
            ToolLoopResult::Completed {
                provider_messages: tool_messages,
            } => {
                provider_messages.extend(tool_messages);
            }
            ToolLoopResult::Stopped => return Ok(()),
        }
        if let Err(error) = store::finish_generation(&current_generation.id, "done", None) {
            return send_error(sender, request_id, "chat_store_failed", &error).await;
        }

        current_assistant = match store::insert_assistant_placeholder(&chat_id) {
            Ok(message) => message,
            Err(error) => return send_error(sender, request_id, "chat_store_failed", &error).await,
        };
        current_generation = match store::start_generation(
            &chat_id,
            &current_assistant.id,
            &model,
            &reasoning_effort,
        ) {
            Ok(generation) => generation,
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
    send_chat_list(sender, None, scope).await
}
