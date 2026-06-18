#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace fennara {

struct FileSnapshot {
    godot::String path;        // res:// path
    godot::String action;      // "modified" | "created" | "deleted"
    godot::String old_content; // file content before modification (empty for "created")
    bool is_directory = false; // for create_dir operations
};

struct TurnSnapshot {
    godot::String user_message;
    std::vector<FileSnapshot> files;
};

class FennaraSnapshotManager : public godot::RefCounted {
    GDCLASS(FennaraSnapshotManager, godot::RefCounted)

protected:
    static void _bind_methods();

private:
    std::vector<TurnSnapshot> _stack;
    static const int MAX_TURNS = 20;

    // Static active pointer — tools call get_active() to snapshot files
    static FennaraSnapshotManager *_active;

    // Check if path is already snapshotted in current turn
    bool _is_path_in_current_turn(const godot::String &path) const;

public:
    // Called once before an AI turn starts
    void begin_turn(const godot::String &user_message);

    // Called before each file-modifying tool executes
    // Reads current file content and records it as "modified"
    void snapshot_file(const godot::String &path);

    // Called when a tool creates a new file (didn't exist before)
    void snapshot_created(const godot::String &path);

    // Called when a tool deletes a file
    void snapshot_deleted(const godot::String &path);

    // Revert the most recent turn. Returns the user message to restore.
    // Returns empty string if nothing to revert.
    godot::String revert();

    // How many turns can be reverted
    int revert_count() const;

    // Clear all snapshots (new tool session)
    void clear();

    // Static access for tools
    static FennaraSnapshotManager *get_active();
    static void set_active(FennaraSnapshotManager *mgr);
};

} // namespace fennara
