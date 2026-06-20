use axum::extract::ws::Message;
use serde::Serialize;
use serde_json::Value;
use std::{
    collections::{HashMap, HashSet},
    path::PathBuf,
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicU64},
    },
};
use tokio::sync::{Mutex, RwLock, mpsc, oneshot};

#[derive(Clone)]
pub(crate) struct AppState {
    pub(crate) connection_counter: Arc<AtomicU64>,
    pub(crate) request_counter: Arc<AtomicU64>,
    pub(crate) projects: Arc<RwLock<HashMap<String, GodotProjectStatus>>>,
    pub(crate) active_session_id: Arc<RwLock<Option<String>>>,
    pub(crate) active_project_explicit: Arc<RwLock<bool>>,
    pub(crate) godot_senders: Arc<RwLock<HashMap<String, mpsc::UnboundedSender<Message>>>>,
    pub(crate) pending_tool_calls: Arc<RwLock<HashMap<String, PendingToolCall>>>,
    pub(crate) cancelled_chats: Arc<RwLock<HashSet<String>>>,
    pub(crate) revertable_chats: Arc<RwLock<HashSet<String>>>,
    pub(crate) runtime_sessions: Arc<Mutex<HashMap<String, RuntimeSession>>>,
    pub(crate) shutdown_sender: Arc<Mutex<Option<oneshot::Sender<()>>>>,
    pub(crate) docs_warmup_running: Arc<AtomicBool>,
}

impl AppState {
    pub(crate) fn new(shutdown_tx: oneshot::Sender<()>) -> Self {
        Self {
            connection_counter: Arc::new(AtomicU64::new(0)),
            request_counter: Arc::new(AtomicU64::new(0)),
            projects: Arc::new(RwLock::new(HashMap::new())),
            active_session_id: Arc::new(RwLock::new(None)),
            active_project_explicit: Arc::new(RwLock::new(false)),
            godot_senders: Arc::new(RwLock::new(HashMap::new())),
            pending_tool_calls: Arc::new(RwLock::new(HashMap::new())),
            cancelled_chats: Arc::new(RwLock::new(HashSet::new())),
            revertable_chats: Arc::new(RwLock::new(HashSet::new())),
            runtime_sessions: Arc::new(Mutex::new(HashMap::new())),
            shutdown_sender: Arc::new(Mutex::new(Some(shutdown_tx))),
            docs_warmup_running: Arc::new(AtomicBool::new(false)),
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub(crate) struct DaemonStatus {
    pub(crate) ok: bool,
    pub(crate) daemon: &'static str,
    pub(crate) version: &'static str,
    pub(crate) godot_plugin_connected: bool,
    pub(crate) active_project: Option<GodotProjectStatus>,
    pub(crate) active_session_id: Option<String>,
    pub(crate) connected_projects: Vec<GodotProjectStatus>,
}

#[derive(Clone, Debug, serde::Deserialize, Serialize)]
pub(crate) struct GodotProjectStatus {
    pub(crate) session_id: String,
    pub(crate) project_name: Option<String>,
    pub(crate) project_path: Option<String>,
    pub(crate) godot_version: Option<String>,
    pub(crate) plugin_version: Option<String>,
    #[serde(skip_serializing)]
    pub(crate) chat_token: Option<String>,
    pub(crate) tools: Vec<String>,
}

pub(crate) struct PendingToolCall {
    pub(crate) session_id: String,
    pub(crate) sender: oneshot::Sender<Value>,
}

pub(crate) struct RuntimeSession {
    pub(crate) session_id: String,
    pub(crate) scene_path: String,
    pub(crate) working_directory: PathBuf,
    pub(crate) artifact_dir: PathBuf,
    pub(crate) command_dir: PathBuf,
    pub(crate) raw_log_path: PathBuf,
    pub(crate) child: tokio::process::Child,
    pub(crate) started_ms: u128,
}
