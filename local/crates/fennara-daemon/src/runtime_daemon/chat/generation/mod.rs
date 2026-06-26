pub(super) mod cost;
pub(super) mod publisher;
pub(super) mod request;
pub(super) mod runner;
pub(super) mod tool_loop;

use crate::runtime_daemon::state::AppState;

pub(super) async fn is_chat_cancelled(state: &AppState, chat_id: &str) -> bool {
    state.cancelled_chats.read().await.contains(chat_id)
}
