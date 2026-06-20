#include "fennara/snapshot_manager.hpp"
#include "fennara/helpers.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_file_system.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace fennara {

FennaraSnapshotManager *FennaraSnapshotManager::_active = nullptr;

namespace {

void _record_snapshot_incident(const godot::String &code,
                               const godot::String &message,
                               const godot::String &path,
                               const godot::Dictionary &extra = godot::Dictionary()) {
    godot::Dictionary details = extra;
    if (!path.is_empty()) {
        details["path"] = path;
    }
    Logger::record_incident("plugin_snapshot", code, message, details);
}

} // namespace

void FennaraSnapshotManager::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("begin_turn", "user_message", "chat_id"), &FennaraSnapshotManager::begin_turn);
    godot::ClassDB::bind_method(godot::D_METHOD("snapshot_file", "path"), &FennaraSnapshotManager::snapshot_file);
    godot::ClassDB::bind_method(godot::D_METHOD("snapshot_created", "path"), &FennaraSnapshotManager::snapshot_created);
    godot::ClassDB::bind_method(godot::D_METHOD("snapshot_deleted", "path"), &FennaraSnapshotManager::snapshot_deleted);
    godot::ClassDB::bind_method(godot::D_METHOD("revert", "chat_id"), &FennaraSnapshotManager::revert);
    godot::ClassDB::bind_method(godot::D_METHOD("can_revert_chat", "chat_id"), &FennaraSnapshotManager::can_revert_chat);
    godot::ClassDB::bind_method(godot::D_METHOD("revert_count"), &FennaraSnapshotManager::revert_count);
    godot::ClassDB::bind_method(godot::D_METHOD("clear"), &FennaraSnapshotManager::clear);
}

// ── Helpers ──────────────────────────────────────────────────────────────

bool FennaraSnapshotManager::_is_path_in_current_turn(const godot::String &path) const {
    if (_stack.empty()) return false;
    const auto &files = _stack.back().files;
    for (const auto &f : files) {
        if (f.path == path) return true;
    }
    return false;
}

// ── Turn lifecycle ───────────────────────────────────────────────────────

void FennaraSnapshotManager::begin_turn(const godot::String &user_message,
                                        const godot::String &chat_id) {
    if ((int)_stack.size() >= MAX_TURNS) {
        _stack.erase(_stack.begin());
    }
    TurnSnapshot turn;
    turn.chat_id = chat_id;
    turn.user_message = user_message;
    _stack.push_back(turn);
    FLOG_UI(godot::String("Snapshot: begin_turn, stack_size=") + godot::String::num_int64(_stack.size()));
}

// ── Snapshot recording ───────────────────────────────────────────────────

void FennaraSnapshotManager::snapshot_file(const godot::String &path) {
    if (_stack.empty()) return;
    if (_is_path_in_current_turn(path)) return; // first snapshot wins

    if (!godot::FileAccess::file_exists(path)) return;

    godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        _record_snapshot_incident("snapshot_read_failed", "Failed to snapshot existing file.", path);
        return;
    }

    FileSnapshot snap;
    snap.path = path;
    snap.action = "modified";
    snap.old_content = file->get_as_text();
    file->close();

    _stack.back().files.push_back(snap);
    FLOG_TOOL(godot::String("Snapshot: recorded modified ") + path);
}

void FennaraSnapshotManager::snapshot_created(const godot::String &path) {
    if (_stack.empty()) return;
    if (_is_path_in_current_turn(path)) return;

    FileSnapshot snap;
    snap.path = path;
    snap.action = "created";
    snap.old_content = "";

    _stack.back().files.push_back(snap);
    FLOG_TOOL(godot::String("Snapshot: recorded created ") + path);
}

void FennaraSnapshotManager::snapshot_deleted(const godot::String &path) {
    if (_stack.empty()) return;
    if (_is_path_in_current_turn(path)) return;

    // For directories, just record the path
    if (godot::DirAccess::dir_exists_absolute(path)) {
        FileSnapshot snap;
        snap.path = path;
        snap.action = "deleted";
        snap.old_content = "";
        snap.is_directory = true;
        _stack.back().files.push_back(snap);
        FLOG_TOOL(godot::String("Snapshot: recorded deleted dir ") + path);
        return;
    }

    // For files, read content before deletion
    godot::Ref<godot::FileAccess> file = godot::FileAccess::open(path, godot::FileAccess::READ);
    if (!file.is_valid()) {
        _record_snapshot_incident("snapshot_deleted_read_failed", "Failed to snapshot deleted file contents.", path);
        return;
    }

    FileSnapshot snap;
    snap.path = path;
    snap.action = "deleted";
    snap.old_content = file->get_as_text();
    file->close();

    _stack.back().files.push_back(snap);
    FLOG_TOOL(godot::String("Snapshot: recorded deleted ") + path);
}

// ── Revert ───────────────────────────────────────────────────────────────

bool FennaraSnapshotManager::can_revert_chat(const godot::String &chat_id) const {
    return !_stack.empty() && _stack.back().chat_id == chat_id;
}

godot::String FennaraSnapshotManager::revert(const godot::String &chat_id) {
    if (_stack.empty()) return "";
    if (_stack.back().chat_id != chat_id) return "";

    TurnSnapshot turn = _stack.back();
    _stack.pop_back();

    int restored = 0;
    int deleted = 0;
    int recreated = 0;

    // Process in reverse order
    for (int i = (int)turn.files.size() - 1; i >= 0; i--) {
        const FileSnapshot &snap = turn.files[i];

        if (snap.action == "modified") {
            // Restore original content
            godot::Ref<godot::FileAccess> file = godot::FileAccess::open(snap.path, godot::FileAccess::WRITE);
            if (file.is_valid()) {
                file->store_string(snap.old_content);
                file->close();
                restored++;
            } else {
                _record_snapshot_incident("revert_restore_failed", "Failed to restore modified file during revert.", snap.path);
            }
        } else if (snap.action == "created") {
            if (godot::DirAccess::dir_exists_absolute(snap.path)) {
                // Remove directory (only succeeds if empty — safe)
                if (godot::DirAccess::remove_absolute(snap.path) == godot::OK) {
                    deleted++;
                } else {
                    _record_snapshot_incident("revert_delete_created_dir_failed", "Failed to remove created directory during revert.", snap.path);
                }
            } else if (godot::FileAccess::file_exists(snap.path)) {
                // Delete the file that was created during this turn
                if (godot::DirAccess::remove_absolute(snap.path) == godot::OK) {
                    deleted++;
                } else {
                    _record_snapshot_incident("revert_delete_created_file_failed", "Failed to remove created file during revert.", snap.path);
                }
                // Also remove .uid sidecar if present
                godot::String uid_path = snap.path + godot::String(".uid");
                if (godot::FileAccess::file_exists(uid_path)) {
                    if (godot::DirAccess::remove_absolute(uid_path) != godot::OK) {
                        _record_snapshot_incident("revert_delete_uid_failed", "Failed to remove .uid sidecar during revert.", uid_path);
                    }
                }
            }
        } else if (snap.action == "deleted") {
            if (snap.is_directory) {
                // Re-create directory
                if (godot::DirAccess::make_dir_recursive_absolute(snap.path) == godot::OK) {
                    recreated++;
                } else {
                    _record_snapshot_incident("revert_recreate_dir_failed", "Failed to recreate deleted directory during revert.", snap.path);
                }
            } else {
                // Re-create the deleted file
                godot::String dir_path = snap.path.get_base_dir();
                if (!godot::DirAccess::dir_exists_absolute(dir_path)) {
                    if (godot::DirAccess::make_dir_recursive_absolute(dir_path) != godot::OK) {
                        _record_snapshot_incident("revert_prepare_parent_dir_failed", "Failed to recreate parent directory during revert.", dir_path);
                        continue;
                    }
                }
                godot::Ref<godot::FileAccess> file = godot::FileAccess::open(snap.path, godot::FileAccess::WRITE);
                if (file.is_valid()) {
                    file->store_string(snap.old_content);
                    file->close();
                    recreated++;
                } else {
                    _record_snapshot_incident("revert_recreate_file_failed", "Failed to recreate deleted file during revert.", snap.path);
                }
            }
        }
    }

    // Refresh editor filesystem for each restored file
    for (int i = (int)turn.files.size() - 1; i >= 0; i--) {
        fennara::notify_editor_filesystem(turn.files[i].path);
    }

    FLOG_UI(godot::String("Snapshot: reverted turn, restored=") +
            godot::String::num_int64(restored) + " deleted=" +
            godot::String::num_int64(deleted) + " recreated=" +
            godot::String::num_int64(recreated) + " stack_remaining=" +
            godot::String::num_int64(_stack.size()));

    return turn.user_message;
}

int FennaraSnapshotManager::revert_count() const {
    return (int)_stack.size();
}

void FennaraSnapshotManager::clear() {
    _stack.clear();
    FLOG_UI("Snapshot: cleared");
}

// ── Static access ────────────────────────────────────────────────────────

FennaraSnapshotManager *FennaraSnapshotManager::get_active() {
    return _active;
}

void FennaraSnapshotManager::set_active(FennaraSnapshotManager *mgr) {
    _active = mgr;
}

} // namespace fennara
