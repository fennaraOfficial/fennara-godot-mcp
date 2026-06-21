use axum::{Json, extract::State};
use serde::Deserialize;
use serde_json::{Value, json};
use std::{path::PathBuf, time::Duration};
use tokio::process::Command;

use super::{
    process_helpers::resolve_godot_executable,
    state::{AppState, RuntimeSession},
    util::{sanitize_path_component, unix_millis},
};

#[cfg(target_os = "windows")]
const CREATE_NEW_PROCESS_GROUP: u32 = 0x00000200;

#[derive(Debug, Deserialize)]
pub(crate) struct RuntimeSessionStartRequest {
    session_id: Option<String>,
    executable: String,
    working_directory: String,
    scene_path: String,
    artifact_dir: String,
}

#[derive(Debug, Deserialize)]
pub(crate) struct RuntimeSessionIdRequest {
    session_id: String,
}

#[derive(Debug, Deserialize)]
pub(crate) struct RuntimeScriptRequest {
    session_id: String,
    script_run_id: String,
    script_path: String,
    timeout_ms: Option<u64>,
}

pub(crate) async fn runtime_session_start(
    State(state): State<AppState>,
    Json(request): Json<RuntimeSessionStartRequest>,
) -> Json<Value> {
    match runtime_session_start_inner(&state, request).await {
        Ok(value) => Json(value),
        Err(error) => Json(json!({ "ok": false, "error": error })),
    }
}

pub(crate) async fn runtime_session_status(
    State(state): State<AppState>,
    Json(request): Json<RuntimeSessionIdRequest>,
) -> Json<Value> {
    match runtime_session_status_inner(&state, &request.session_id).await {
        Ok(value) => Json(value),
        Err(error) => Json(json!({ "ok": false, "error": error })),
    }
}

pub(crate) async fn runtime_session_stop(
    State(state): State<AppState>,
    Json(request): Json<RuntimeSessionIdRequest>,
) -> Json<Value> {
    match runtime_session_stop_inner(&state, &request.session_id).await {
        Ok(value) => Json(value),
        Err(error) => Json(json!({ "ok": false, "error": error })),
    }
}

pub(crate) async fn runtime_session_script(
    State(state): State<AppState>,
    Json(request): Json<RuntimeScriptRequest>,
) -> Json<Value> {
    match runtime_session_script_inner(&state, request).await {
        Ok(value) => Json(value),
        Err(error) => Json(json!({ "ok": false, "error": error })),
    }
}

async fn runtime_session_start_inner(
    state: &AppState,
    request: RuntimeSessionStartRequest,
) -> Result<Value, String> {
    {
        let mut sessions = state.runtime_sessions.lock().await;
        for (existing_id, existing) in sessions.iter_mut() {
            if existing
                .child
                .try_wait()
                .map_err(|err| format!("runtime session wait failed: {err}"))?
                .is_none()
            {
                return Err(format!(
                    "Runtime session already running: {existing_id}. Fennara currently allows one managed runtime session across all connected Godot editors. Use runtime_session.status or runtime_session.stop before starting another scene."
                ));
            }
        }
    }

    if request.scene_path.trim().is_empty() {
        return Err("scene_path is required.".to_string());
    }
    let executable = resolve_godot_executable(&request.executable).ok_or_else(|| {
        format!(
            "Could not find Godot executable. Tried sent path '{}' and PATH candidates: godot, godot4, godot-mono, godot4-mono.",
            request.executable
        )
    })?;
    let working_directory = PathBuf::from(request.working_directory.trim());
    if !working_directory.is_dir() {
        return Err(format!(
            "Working directory was not found: {}",
            working_directory.display()
        ));
    }
    let artifact_dir = PathBuf::from(request.artifact_dir.trim());
    if artifact_dir.as_os_str().is_empty() {
        return Err("artifact_dir is required.".to_string());
    }
    tokio::fs::create_dir_all(&artifact_dir)
        .await
        .map_err(|err| format!("create artifact_dir failed: {err}"))?;
    let command_dir = artifact_dir.join("commands");
    tokio::fs::create_dir_all(&command_dir)
        .await
        .map_err(|err| format!("create command_dir failed: {err}"))?;

    let session_id = request
        .session_id
        .clone()
        .filter(|value| !value.trim().is_empty())
        .unwrap_or_else(|| format!("runtime-{}", unix_millis()));
    let raw_log_path = artifact_dir.join("runtime_session.log");
    let spec_path = artifact_dir.join("runtime_session_spec.json");
    let spec = json!({
        "mode": "runtime_session",
        "session_id": session_id,
        "command_dir": command_dir.to_string_lossy(),
        "artifact_dir": artifact_dir.to_string_lossy(),
        "scene_path": request.scene_path,
    });
    tokio::fs::write(
        &spec_path,
        serde_json::to_string_pretty(&spec)
            .map_err(|err| format!("serialize runtime spec failed: {err}"))?,
    )
    .await
    .map_err(|err| format!("write runtime spec failed: {err}"))?;

    let log_file = std::fs::File::create(&raw_log_path)
        .map_err(|err| format!("create runtime session log failed: {err}"))?;
    let stderr_file = log_file
        .try_clone()
        .map_err(|err| format!("clone runtime session log failed: {err}"))?;

    let mut command = Command::new(&executable);
    command
        .arg("--windowed")
        .arg("--path")
        .arg(&working_directory)
        .arg("--scene")
        .arg(&request.scene_path)
        .current_dir(&working_directory)
        .env("FENNARA_RT_SPEC", &spec_path)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::from(log_file))
        .stderr(std::process::Stdio::from(stderr_file));

    #[cfg(target_os = "windows")]
    command.creation_flags(CREATE_NEW_PROCESS_GROUP);

    let child = command
        .spawn()
        .map_err(|err| format!("failed to start runtime session: {err}"))?;
    let pid = child.id().unwrap_or_default();
    let session = RuntimeSession {
        session_id: session_id.clone(),
        scene_path: request.scene_path.clone(),
        working_directory,
        artifact_dir: artifact_dir.clone(),
        command_dir: command_dir.clone(),
        raw_log_path: raw_log_path.clone(),
        child,
        started_ms: unix_millis(),
    };
    state
        .runtime_sessions
        .lock()
        .await
        .insert(session_id.clone(), session);

    Ok(json!({
        "ok": true,
        "status": "started",
        "scope": "global",
        "scope_note": "Fennara currently allows one managed runtime session across all connected Godot editors.",
        "session_id": session_id,
        "pid": pid,
        "scene_path": request.scene_path,
        "artifact_dir": artifact_dir.to_string_lossy(),
        "command_dir": command_dir.to_string_lossy(),
        "raw_log_path": raw_log_path.to_string_lossy(),
        "spec_path": spec_path.to_string_lossy(),
        "executable": executable.to_string_lossy(),
    }))
}

async fn runtime_session_status_inner(state: &AppState, session_id: &str) -> Result<Value, String> {
    let mut sessions = state.runtime_sessions.lock().await;
    let session = sessions
        .get_mut(session_id)
        .ok_or_else(|| format!("Runtime session not found: {session_id}"))?;
    let exit_status = session
        .child
        .try_wait()
        .map_err(|err| format!("runtime session wait failed: {err}"))?;
    Ok(json!({
        "ok": true,
        "session_id": session.session_id,
        "scene_path": session.scene_path,
        "running": exit_status.is_none(),
        "scope": "global",
        "scope_note": "Fennara currently allows one managed runtime session across all connected Godot editors.",
        "exit_code": exit_status.and_then(|status| status.code()),
        "artifact_dir": session.artifact_dir.to_string_lossy(),
        "command_dir": session.command_dir.to_string_lossy(),
        "raw_log_path": session.raw_log_path.to_string_lossy(),
        "working_directory": session.working_directory.to_string_lossy(),
        "started_ms": session.started_ms,
    }))
}

async fn runtime_session_stop_inner(state: &AppState, session_id: &str) -> Result<Value, String> {
    let mut session = state
        .runtime_sessions
        .lock()
        .await
        .remove(session_id)
        .ok_or_else(|| format!("Runtime session not found: {session_id}"))?;
    let mut exit_code = None;
    if let Some(status) = session
        .child
        .try_wait()
        .map_err(|err| format!("runtime session wait failed: {err}"))?
    {
        exit_code = status.code();
    } else {
        let _ = session.child.kill().await;
        if let Ok(status) = session.child.wait().await {
            exit_code = status.code();
        }
    }
    Ok(json!({
        "ok": true,
        "status": "stopped",
        "scope": "global",
        "session_id": session_id,
        "exit_code": exit_code,
        "artifact_dir": session.artifact_dir.to_string_lossy(),
        "raw_log_path": session.raw_log_path.to_string_lossy(),
    }))
}

async fn runtime_session_script_inner(
    state: &AppState,
    request: RuntimeScriptRequest,
) -> Result<Value, String> {
    let (command_dir, artifact_dir, raw_log_path) = {
        let sessions = state.runtime_sessions.lock().await;
        let session = sessions
            .get(&request.session_id)
            .ok_or_else(|| format!("Runtime session not found: {}", request.session_id))?;
        (
            session.command_dir.clone(),
            session.artifact_dir.clone(),
            session.raw_log_path.clone(),
        )
    };
    tokio::fs::create_dir_all(&command_dir)
        .await
        .map_err(|err| format!("create command_dir failed: {err}"))?;
    let status_dir = artifact_dir.join("runtime_script_results");
    tokio::fs::create_dir_all(&status_dir)
        .await
        .map_err(|err| format!("create runtime_script_results dir failed: {err}"))?;
    let status_path = status_dir.join(format!(
        "{}.json",
        sanitize_path_component(&request.script_run_id)
    ));
    let _ = tokio::fs::remove_file(&status_path).await;
    let command_path = command_dir.join(format!(
        "{}.json",
        sanitize_path_component(&request.script_run_id)
    ));
    let command = json!({
        "action": "run_runtime_script",
        "session_id": request.session_id,
        "script_run_id": request.script_run_id,
        "script_path": request.script_path,
        "status_path": status_path.to_string_lossy(),
    });
    tokio::fs::write(
        &command_path,
        serde_json::to_string_pretty(&command)
            .map_err(|err| format!("serialize script command failed: {err}"))?,
    )
    .await
    .map_err(|err| format!("write script command failed: {err}"))?;

    let deadline = tokio::time::Instant::now()
        + Duration::from_millis(request.timeout_ms.unwrap_or(10_000).clamp(500, 120_000));
    loop {
        if tokio::time::Instant::now() >= deadline {
            return Ok(json!({
                "ok": false,
                "status": "timeout",
                "scope": "global",
                "session_id": request.session_id,
                "script_run_id": request.script_run_id,
                "command_path": command_path.to_string_lossy(),
                "artifact_dir": artifact_dir.to_string_lossy(),
                "status_path": status_path.to_string_lossy(),
                "raw_log_path": raw_log_path.to_string_lossy(),
                "error": "Runtime script result did not arrive before timeout.",
            }));
        }
        if status_path.is_file() {
            let text = tokio::fs::read_to_string(&status_path)
                .await
                .map_err(|err| format!("read script status failed: {err}"))?;
            if let Ok(value) = serde_json::from_str::<Value>(&text) {
                let status = value.get("status").and_then(Value::as_str).unwrap_or("");
                if status == "completed" || status == "failed" {
                    return Ok(json!({
                        "ok": status == "completed",
                        "status": status,
                        "scope": "global",
                        "session_id": request.session_id,
                        "script_run_id": request.script_run_id,
                        "command_path": command_path.to_string_lossy(),
                        "artifact_dir": artifact_dir.to_string_lossy(),
                        "status_path": status_path.to_string_lossy(),
                        "raw_log_path": raw_log_path.to_string_lossy(),
                        "result": value,
                    }));
                }
            }
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}
