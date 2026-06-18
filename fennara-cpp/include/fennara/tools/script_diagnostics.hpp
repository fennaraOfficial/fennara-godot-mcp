#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/stream_peer_tcp.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace fennara {

class FennaraScriptDiagnosticsTool : public godot::RefCounted {
    GDCLASS(FennaraScriptDiagnosticsTool, godot::RefCounted)

    godot::Ref<godot::Thread> _thread;
    godot::Ref<godot::Mutex> _mutex;
    godot::Dictionary _result;
    godot::Dictionary _args;
    bool _finished = false;

    void _worker();

    void _on_complete();

protected:
    static void _bind_methods();

public:
    void execute(const godot::Dictionary &args);
    godot::Dictionary get_result() const;
    bool is_finished() const;
    ~FennaraScriptDiagnosticsTool();
};

} // namespace fennara
