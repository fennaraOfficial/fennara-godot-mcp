extends Node

const PREFIX := "fennara"

func _ready() -> void:
	if Engine.is_editor_hint():
		return
	EngineDebugger.register_message_capture(PREFIX, _on_debug_message)
	if EngineDebugger.is_active():
		EngineDebugger.send_message("fennara:hello", [])

func _exit_tree() -> void:
	if EngineDebugger.has_capture(PREFIX):
		EngineDebugger.unregister_message_capture(PREFIX)

func _on_debug_message(message: String, data: Array) -> bool:
	var action := message.trim_prefix("fennara:")
	if action == "take_screenshot":
		_take_screenshot(data)
		return true
	return false

func _take_screenshot(data: Array) -> void:
	var request_id := str(data[0]) if data.size() > 0 else ""
	var max_resolution := int(data[1]) if data.size() > 1 else 1280

	await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var viewport := get_tree().root
	var texture := viewport.get_texture()
	if texture == null:
		EngineDebugger.send_message("fennara:screenshot_error", [request_id, "Runtime viewport texture was unavailable."])
		return

	var image := texture.get_image()
	if image == null or image.is_empty():
		EngineDebugger.send_message("fennara:screenshot_error", [request_id, "Runtime viewport image was empty."])
		return

	var original_w := image.get_width()
	var original_h := image.get_height()
	if max_resolution > 0:
		var longest := maxi(original_w, original_h)
		if longest > max_resolution:
			var scale := float(max_resolution) / float(longest)
			image.resize(maxi(1, int(original_w * scale)), maxi(1, int(original_h * scale)))

	var png := image.save_png_to_buffer()
	if png.is_empty():
		EngineDebugger.send_message("fennara:screenshot_error", [request_id, "Failed to encode runtime screenshot PNG."])
		return

	EngineDebugger.send_message("fennara:screenshot_response", [
		request_id,
		png,
		image.get_width(),
		image.get_height(),
		original_w,
		original_h,
	])
