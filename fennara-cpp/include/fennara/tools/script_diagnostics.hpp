#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/stream_peer_tcp.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <atomic>

namespace fennara {

class FennaraScriptDiagnosticsTool : public godot::RefCounted {
    GDCLASS(FennaraScriptDiagnosticsTool, godot::RefCounted)

    godot::Ref<godot::Thread> _thread;
    godot::Ref<godot::Mutex> _mutex;
    godot::Dictionary _result;
    godot::Dictionary _args;
    godot::Dictionary _pending_state;
    godot::Dictionary _scene_load_requested_paths;
    godot::Dictionary _scene_load_per_file;
    godot::Dictionary _scene_load_summary;
    godot::Array _scene_load_scene_paths;
    int _scene_load_index = 0;
    bool _finished = false;
    std::atomic_bool _cancelled{false};

    void _worker();

    void _finish_on_main_thread();
    void _process_scene_load_diagnostics();
    void _finish_with_result(const godot::Dictionary &result);
    void _on_complete();
    bool _is_cancelled() const;

protected:
    static void _bind_methods();

public:
    void execute(const godot::Dictionary &args);
    void cancel();
    godot::Dictionary get_result() const;
    bool is_finished() const;
    ~FennaraScriptDiagnosticsTool();
};

} // namespace fennara
