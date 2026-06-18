#include "fennara/tools/scrape_editor.hpp"

#include "fennara/runtime/runtime_debugger_snapshot.hpp"

#include <godot_cpp/variant/string.hpp>

namespace fennara {

void FennaraScrapeEditorTool::_bind_methods() {
    godot::ClassDB::bind_static_method(
        "FennaraScrapeEditorTool",
        godot::D_METHOD("execute", "args"),
        &FennaraScrapeEditorTool::execute);
}

godot::Dictionary FennaraScrapeEditorTool::execute(const godot::Dictionary &args) {
    godot::String target = godot::String(args.get("target", "")).strip_edges();
    if (target != "debugger") {
        godot::Dictionary error;
        error["success"] = false;
        error["tool_name"] = "scrape_editor";
        error["status"] = "failed";
        error["error"] = "scrape_editor currently supports only target='debugger'.";
        return error;
    }

    godot::Dictionary snapshot = runtime_debugger_snapshot::capture();
    godot::Dictionary result;
    result["success"] = true;
    result["tool_name"] = "scrape_editor";
    result["status"] = "success";
    result["target"] = "debugger";
    result["source"] = snapshot.get("source", "debugger_session");
    result["summary"] = snapshot.get("summary", godot::Dictionary());
    result["errors"] = snapshot.get("errors", godot::Array());
    result["captured_messages"] = snapshot.get("captured_messages", godot::Array());
    result["tree_path"] = snapshot.get("tree_path", "");
    return result;
}

} // namespace fennara
