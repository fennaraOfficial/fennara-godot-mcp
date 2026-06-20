#include "fennara/local_bridge.hpp"

#include "fennara/lsp/csharp_support.hpp"
#include "fennara/logger.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

namespace fennara {

void FennaraLocalBridge::_send_hello() {
    godot::Dictionary payload;
    payload["type"] = "hello";
    payload["session_id"] = _session_id;
    payload["project_name"] = _project_name();
    payload["project_path"] = _project_path();
    payload["plugin_version"] = PLUGIN_VERSION;
    payload["chat_token"] = _chat_token;
    payload["godot_version"] = godot::String(godot::Engine::get_singleton()->get_version_info()["string"]);
    payload["csharp_support"] = csharp_support::inspect_project();

    godot::Array tools;
    tools.append("read_file");
    tools.append("file_ops");
    tools.append("write_or_update_file");
    tools.append("run_scene_edit_script");
    tools.append("get_scene_tree");
    tools.append("save_custom_resource");
    tools.append("script_diagnostics");
    tools.append("screenshot_scene");
    tools.append("get_node_properties");
    tools.append("get_class_info");
    tools.append("validate_scene");
    tools.append("project_settings");
    tools.append("runtime_session");
    tools.append("runtime_script");
    tools.append("scrape_editor");
    payload["tools"] = tools;

    godot::String body = godot::JSON::stringify(payload);
    godot::Error err = _ws->send_text(body);
    if (err == godot::OK) {
        _sent_hello = true;
        FLOG_NET("Local bridge hello sent");
    } else {
        FLOG_ERR("Local bridge failed to send hello");
    }
}

void FennaraLocalBridge::request_get_class_info_warmup() {
    _queued_get_class_info_warmup = true;
    _maybe_send_get_class_info_warmup();
}

void FennaraLocalBridge::_maybe_send_get_class_info_warmup() {
    if (!_queued_get_class_info_warmup || _sent_get_class_info_warmup ||
        !_sent_hello || !_ws.is_valid() ||
        _ws->get_ready_state() != godot::WebSocketPeer::STATE_OPEN) {
        return;
    }

    godot::Dictionary payload;
    payload["type"] = "warm_get_class_info_docs";
    payload["branch"] = "master";

    godot::Array class_names;
    class_names.append("Object");
    class_names.append("RefCounted");
    class_names.append("Resource");
    class_names.append("Node");
    class_names.append("Node2D");
    class_names.append("Node3D");
    class_names.append("CanvasItem");
    class_names.append("Control");
    class_names.append("Sprite2D");
    class_names.append("AnimatedSprite2D");
    class_names.append("CharacterBody2D");
    class_names.append("RigidBody2D");
    class_names.append("StaticBody2D");
    class_names.append("Area2D");
    class_names.append("Camera2D");
    class_names.append("TileMap");
    class_names.append("CollisionShape2D");
    class_names.append("CollisionPolygon2D");
    class_names.append("RayCast2D");
    class_names.append("Marker2D");
    class_names.append("Path2D");
    class_names.append("PathFollow2D");
    class_names.append("Texture2D");
    class_names.append("Image");
    class_names.append("AtlasTexture");
    class_names.append("AnimationPlayer");
    class_names.append("GPUParticles2D");
    class_names.append("AudioStreamPlayer2D");
    class_names.append("Animation");
    class_names.append("AnimationLibrary");
    class_names.append("Curve");
    class_names.append("Curve2D");
    class_names.append("Shape2D");
    class_names.append("RectangleShape2D");
    class_names.append("CircleShape2D");
    class_names.append("CapsuleShape2D");
    class_names.append("World2D");
    class_names.append("ParticleProcessMaterial");
    class_names.append("Shader");
    class_names.append("Material");
    class_names.append("CanvasItemMaterial");
    class_names.append("AudioStream");
    class_names.append("AudioStreamWAV");
    class_names.append("Label");
    class_names.append("Button");
    class_names.append("TextureButton");
    class_names.append("LineEdit");
    class_names.append("TextEdit");
    class_names.append("RichTextLabel");
    class_names.append("Panel");
    class_names.append("PanelContainer");
    class_names.append("MarginContainer");
    class_names.append("VBoxContainer");
    class_names.append("HBoxContainer");
    class_names.append("GridContainer");
    class_names.append("ScrollContainer");
    class_names.append("TabContainer");
    class_names.append("ColorRect");
    class_names.append("TextureRect");
    class_names.append("OptionButton");
    class_names.append("CheckBox");
    class_names.append("Slider");
    class_names.append("ProgressBar");
    class_names.append("ItemList");
    class_names.append("Tree");
    class_names.append("TabBar");
    class_names.append("Window");
    class_names.append("Theme");
    class_names.append("StyleBox");
    class_names.append("StyleBoxFlat");
    class_names.append("StyleBoxTexture");
    class_names.append("Font");
    class_names.append("FontFile");
    class_names.append("FontVariation");
    class_names.append("ShaderMaterial");
    class_names.append("Gradient");
    class_names.append("GradientTexture1D");
    class_names.append("GradientTexture2D");
    class_names.append("MeshInstance3D");
    class_names.append("Sprite3D");
    class_names.append("CharacterBody3D");
    class_names.append("RigidBody3D");
    class_names.append("StaticBody3D");
    class_names.append("Area3D");
    class_names.append("Camera3D");
    class_names.append("Marker3D");
    class_names.append("RayCast3D");
    class_names.append("CollisionShape3D");
    class_names.append("CollisionPolygon3D");
    class_names.append("CSGBox3D");
    class_names.append("GPUParticles3D");
    class_names.append("DirectionalLight3D");
    class_names.append("OmniLight3D");
    class_names.append("SpotLight3D");
    class_names.append("WorldEnvironment");
    class_names.append("NavigationRegion3D");
    class_names.append("AudioStreamPlayer3D");
    class_names.append("Mesh");
    class_names.append("ArrayMesh");
    class_names.append("PrimitiveMesh");
    class_names.append("BoxMesh");
    class_names.append("SphereMesh");
    class_names.append("CylinderMesh");
    class_names.append("PlaneMesh");
    class_names.append("BaseMaterial3D");
    class_names.append("StandardMaterial3D");
    class_names.append("ORMMaterial3D");
    class_names.append("Environment");
    class_names.append("World3D");
    class_names.append("Shape3D");
    class_names.append("BoxShape3D");
    class_names.append("SphereShape3D");
    class_names.append("CapsuleShape3D");
    class_names.append("ConcavePolygonShape3D");
    class_names.append("ConvexPolygonShape3D");
    class_names.append("SpriteFrames");
    payload["class_names"] = class_names;

    _send_json(payload);
    _sent_get_class_info_warmup = true;
}

bool FennaraLocalBridge::set_as_active_project() {
    if (!_ws.is_valid() || _ws->get_ready_state() != godot::WebSocketPeer::STATE_OPEN) {
        return false;
    }

    godot::Dictionary payload;
    payload["type"] = "set_active_project";
    payload["session_id"] = _session_id;
    _send_json(payload);
    _active_mcp_target_name = _project_name();
    _active_mcp_target_path = _project_path();
    if (!_is_active_mcp_target) {
        _is_active_mcp_target = true;
        emit_signal("mcp_target_state_changed", true);
    }
    FLOG_NET("Local bridge requested active MCP project");
    return true;
}

void FennaraLocalBridge::_send_json(const godot::Dictionary &payload) {
    if (!_ws.is_valid() || _ws->get_ready_state() != godot::WebSocketPeer::STATE_OPEN) {
        return;
    }

    godot::String body = godot::JSON::stringify(payload);
    godot::Error err = _ws->send_text(body);
    if (err != godot::OK) {
        FLOG_ERR("Local bridge failed to send JSON payload");
    }
}

godot::String FennaraLocalBridge::_make_session_id() const {
    return _project_path() + "#" + godot::String::num_int64(godot::OS::get_singleton()->get_process_id());
}

godot::String FennaraLocalBridge::_make_chat_token() const {
    godot::Ref<godot::Crypto> crypto;
    crypto.instantiate();
    if (crypto.is_valid()) {
        godot::PackedByteArray bytes = crypto->generate_random_bytes(32);
        return godot::Marshalls::get_singleton()->raw_to_base64(bytes)
            .replace("+", "-")
            .replace("/", "_")
            .replace("=", "");
    }
    return _session_id.md5_text() + godot::String::num_int64(godot::Time::get_singleton()->get_ticks_msec());
}

godot::String FennaraLocalBridge::_project_name() const {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return "";
    }

    return settings->get_setting("application/config/name", "");
}

godot::String FennaraLocalBridge::_project_path() const {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return "";
    }

    return settings->globalize_path("res://");
}

} // namespace fennara
