use serde_json::Value;

use super::super::{images, providers};

pub(super) fn build_provider_messages(
    replay_messages: &[Value],
    user_message: &str,
    user_images: &[images::ChatImage],
) -> Vec<Value> {
    // Historical media is stripped to placeholders by store::replay_messages.
    // Only current-turn attachments are allowed to carry image bytes forward.
    providers::build_messages(replay_messages, user_message, user_images)
}
