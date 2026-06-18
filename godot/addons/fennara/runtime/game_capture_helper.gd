extends Node
const RUNTIME_SPEC_ENV := "FENNARA_RT_SPEC"

var _file_session_id := ""
var _file_command_dir := ""
var _file_artifact_dir := ""
var _file_processed_commands := {}
var _runtime_session_closing := false

func _safe_file_component(value: String, fallback: String) -> String:
	var safe := value.strip_edges().to_lower()
	safe = safe.replace(" ", "_")
	safe = safe.replace("/", "_")
	safe = safe.replace("\\", "_")
	safe = safe.replace(":", "_")
	safe = safe.replace("@", "_")
	safe = safe.replace(".", "_")
	return fallback if safe.is_empty() else safe

func _absolute_path(path: String) -> String:
	return path if path.is_absolute_path() else ProjectSettings.globalize_path(path)

func _ensure_dir(path: String) -> bool:
	return DirAccess.make_dir_recursive_absolute(_absolute_path(path)) == OK

func _viewport_image(max_resolution: int) -> Dictionary:
	var texture := get_tree().root.get_texture()
	if texture == null:
		return {"success": false, "error": "Runtime viewport texture was unavailable."}

	var image := texture.get_image()
	if image == null or image.is_empty():
		return {"success": false, "error": "Runtime viewport image was empty."}

	var original_w := image.get_width()
	var original_h := image.get_height()
	if max_resolution > 0:
		var longest := maxi(original_w, original_h)
		if longest > max_resolution:
			var scale := float(max_resolution) / float(longest)
			image.resize(maxi(1, int(original_w * scale)), maxi(1, int(original_h * scale)))

	return {
		"success": true,
		"image": image,
		"width": image.get_width(),
		"height": image.get_height(),
		"original_width": original_w,
		"original_height": original_h,
	}

class RuntimeScriptContext:
	var _helper: Node
	var _session_id: String
	var _script_run_id: String
	var _status_path: String
	var _captures_dir: String
	var _captures: Array[Dictionary] = []
	var _close_requested := false
	var _pressed_actions: Array[String] = []

	func _init(helper: Node, session_id: String, script_run_id: String, status_path: String = "") -> void:
		_helper = helper
		_session_id = session_id
		_script_run_id = script_run_id
		var artifact_dir: String = _helper._file_artifact_dir
		if artifact_dir.strip_edges().is_empty():
			artifact_dir = ProjectSettings.globalize_path("user://.fennara/runtime_sessions/%s" % _helper._safe_file_component(session_id, "runtime"))
		_status_path = status_path
		if _status_path.strip_edges().is_empty():
			_status_path = artifact_dir.path_join("runtime_script_results").path_join("%s.json" % _helper._safe_file_component(script_run_id, "script"))
		_captures_dir = artifact_dir.path_join("captures")

	func log(message: String, data: Dictionary = {}) -> void:
		var event := data.duplicate(true)
		event["message"] = message
		_print_event("FENNARA_SCRIPT_LOG", event)

	func error(message: String) -> void:
		_print_event("FENNARA_SCRIPT_ERROR", {"message": message})

	func close_scene() -> void:
		if _close_requested:
			return
		_close_requested = true
		_print_event("FENNARA_SCRIPT_CLOSE_REQUESTED", {})
		_write_status("completed", "", {"scene_closed": true, "session_active": false})
		_helper._finish_runtime_script_session.call_deferred(self)

	func has_close_requested() -> bool:
		return _close_requested

	func wait(seconds: float) -> void:
		await _helper.get_tree().create_timer(maxf(0.0, seconds)).timeout

	func capture(label: String, max_resolution: int = 1280) -> Dictionary:
		var result: Dictionary = await _helper._capture_runtime_script(self, label, max_resolution)
		if result.get("success", false):
			_captures.append(result)
		return result

	func press_action(action: String, strength: float = 1.0) -> bool:
		if not InputMap.has_action(action):
			error("Input action does not exist: %s" % action)
			return false
		Input.action_press(action, clampf(strength, 0.0, 1.0))
		if not _pressed_actions.has(action):
			_pressed_actions.append(action)
		self.log("pressed action", {"action": action, "strength": strength})
		return true

	func release_action(action: String) -> bool:
		if not InputMap.has_action(action):
			error("Input action does not exist: %s" % action)
			return false
		Input.action_release(action)
		_pressed_actions.erase(action)
		self.log("released action", {"action": action})
		return true

	func tap_action(action: String, duration: float = 0.1, strength: float = 1.0) -> bool:
		if not press_action(action, strength):
			return false
		await wait(duration)
		return release_action(action)

	func action(action_name: String, phase_or_duration: Variant = "tap", duration: float = 0.1, strength: float = 1.0) -> bool:
		var actions: Array[String] = [action_name]
		return await _apply_action_phase(actions, phase_or_duration, duration, strength)

	func action_sequence(steps: Array) -> Dictionary:
		for i in range(steps.size()):
			var step = steps[i]
			if not step is Dictionary:
				var type_error := "Action sequence step %d must be a Dictionary." % i
				error(type_error)
				return {"success": false, "step_index": i, "error": type_error}

			var result: Dictionary = await _run_action_sequence_step(step)
			if not result.get("success", false):
				result["step_index"] = i
				return result

		self.log("action sequence completed", {"steps": steps.size()})
		return {"success": true, "steps": steps.size()}

	func set_mouse_position(position: Variant) -> bool:
		var point := _coerce_vector2(position)
		if point == null:
			error("Mouse position must be a Vector2 or Dictionary with x/y.")
			return false
		var event := InputEventMouseMotion.new()
		event.position = point
		event.global_position = point
		Input.parse_input_event(event)
		self.log("set mouse position", {"position": {"x": point.x, "y": point.y}})
		return true

	func click_at(position: Variant, options: Dictionary = {}) -> Dictionary:
		var point := _coerce_vector2(position)
		if point == null:
			var position_error := "Click position must be a Vector2 or Dictionary with x/y."
			error(position_error)
			return {"success": false, "error": position_error}

		var button := int(options.get("button", MOUSE_BUTTON_LEFT))
		var duration := float(options.get("duration", 0.05))
		set_mouse_position(point)

		var press := InputEventMouseButton.new()
		press.button_index = button
		press.position = point
		press.global_position = point
		press.pressed = true
		Input.parse_input_event(press)

		await wait(duration)

		var release := InputEventMouseButton.new()
		release.button_index = button
		release.position = point
		release.global_position = point
		release.pressed = false
		Input.parse_input_event(release)

		self.log("clicked position", {"button": button, "position": {"x": point.x, "y": point.y}})
		return {"success": true, "button": button, "position": {"x": point.x, "y": point.y}}

	func click_button(node_or_path: Variant, options: Dictionary = {}) -> Dictionary:
		var node := _resolve_node(node_or_path)
		if node == null:
			var missing_error := "Button node was not found: %s" % str(node_or_path)
			error(missing_error)
			return {"success": false, "error": missing_error}
		if not node is BaseButton:
			var type_error := "Node is not a BaseButton: %s" % _node_path_text(node)
			error(type_error)
			return {"success": false, "error": type_error}

		var button := node as BaseButton
		var mode := str(options.get("mode", "mouse")).to_lower()
		if mode == "signal":
			button.pressed.emit()
			self.log("clicked button by signal", {"path": _node_path_text(button), "text": button.text})
			return {"success": true, "mode": "signal", "path": _node_path_text(button), "text": button.text}
		if mode != "mouse":
			var mode_error := "Unsupported click_button mode: %s" % mode
			error(mode_error)
			return {"success": false, "error": mode_error}

		var center := button.get_global_rect().get_center()
		var result := await click_at(center, options)
		result["mode"] = "mouse"
		result["path"] = _node_path_text(button)
		result["text"] = button.text
		self.log("clicked button by mouse", {"path": result["path"], "text": result["text"], "success": result.get("success", false)})
		return result

	func find_button_by_text(text: String, options: Dictionary = {}) -> String:
		var root := get_scene_root()
		var case_sensitive := bool(options.get("case_sensitive", false))
		var exact := bool(options.get("exact", true))
		var visible_only := bool(options.get("visible_only", true))
		var found := _find_button_by_text_recursive(root, text, case_sensitive, exact, visible_only)
		if found == null:
			self.log("button text not found", {"text": text})
			return ""
		var path := _node_path_text(found)
		self.log("found button by text", {"text": text, "path": path, "button_text": found.text})
		return path

	func button_path_by_text(text: String, options: Dictionary = {}) -> String:
		return find_button_by_text(text, options)

	func release_all_actions() -> void:
		_release_pressed_actions()
		self.log("released all actions")

	func get_scene_root() -> Node:
		var tree := _helper.get_tree()
		if tree.current_scene != null:
			return tree.current_scene
		return tree.root

	func get_session_id() -> String:
		return _session_id

	func get_script_run_id() -> String:
		return _script_run_id

	func get_captures_dir() -> String:
		return _captures_dir

	func _run_action_sequence_step(step: Dictionary) -> Dictionary:
		if step.has("wait"):
			await wait(float(step.get("wait", 0.0)))
			return {"success": true}
		if step.has("capture"):
			var capture_result := await capture(str(step.get("capture", "")), int(step.get("max_resolution", 1280)))
			return {"success": bool(capture_result.get("success", false)), "capture": capture_result}
		if step.has("click_at"):
			return await click_at(step.get("click_at"), step.get("options", {}))
		if step.has("click_button"):
			return await click_button(step.get("click_button"), step.get("options", {}))
		if step.has("mouse_position"):
			var moved := set_mouse_position(step.get("mouse_position"))
			return {"success": moved}

		var actions := _actions_from_step(step)
		if actions.is_empty():
			var action_error := "Action sequence step must contain action/actions, wait, capture, click_at, click_button, or mouse_position."
			error(action_error)
			return {"success": false, "error": action_error}

		var phase: Variant = step.get("phase", "tap")
		if step.has("duration") and str(phase).to_lower() == "tap":
			phase = "tap"
		var duration := float(step.get("duration", 0.1))
		var strength := float(step.get("strength", 1.0))
		var ok := await _apply_action_phase(actions, phase, duration, strength)
		return {"success": ok}

	func _actions_from_step(step: Dictionary) -> Array[String]:
		var actions: Array[String] = []
		if step.has("action"):
			actions.append(str(step.get("action", "")))
		elif step.has("actions"):
			var raw_actions = step.get("actions", [])
			if raw_actions is Array:
				for action_name in raw_actions:
					actions.append(str(action_name))
		actions = actions.filter(func(action_name: String) -> bool: return not action_name.strip_edges().is_empty())
		return actions

	func _apply_action_phase(actions: Array[String], phase_or_duration: Variant, duration: float = 0.1, strength: float = 1.0) -> bool:
		if phase_or_duration is int or phase_or_duration is float:
			return await _hold_actions(actions, float(phase_or_duration), strength)

		var phase := str(phase_or_duration).to_lower()
		match phase:
			"press":
				for action_name in actions:
					if not press_action(action_name, strength):
						return false
				return true
			"release":
				for action_name in actions:
					if not release_action(action_name):
						return false
				return true
			"tap":
				return await _hold_actions(actions, duration, strength)
			_:
				error("Unsupported action phase: %s" % phase)
				return false

	func _hold_actions(actions: Array[String], duration: float, strength: float = 1.0) -> bool:
		for action_name in actions:
			if not press_action(action_name, strength):
				return false
		await wait(duration)
		var ok := true
		for action_name in actions:
			ok = release_action(action_name) and ok
		return ok

	func _coerce_vector2(value: Variant) -> Variant:
		if value is Vector2:
			return value
		if value is Dictionary and value.has("x") and value.has("y"):
			return Vector2(float(value.get("x")), float(value.get("y")))
		if value is Array and value.size() >= 2:
			return Vector2(float(value[0]), float(value[1]))
		return null

	func _resolve_node(node_or_path: Variant) -> Node:
		if node_or_path is Node:
			return node_or_path
		var root := get_scene_root()
		var path := str(node_or_path)
		if path.strip_edges().is_empty():
			return null
		if path.begins_with("/root/"):
			return _helper.get_tree().root.get_node_or_null(NodePath(path.trim_prefix("/root/")))
		if path.begins_with("/"):
			return _helper.get_tree().root.get_node_or_null(NodePath(path.trim_prefix("/")))
		return root.get_node_or_null(NodePath(path))

	func _node_path_text(node: Node) -> String:
		var root := get_scene_root()
		if node == root:
			return "."
		if root != null and root.is_ancestor_of(node):
			return str(root.get_path_to(node))
		return str(node.get_path())

	func _find_button_by_text_recursive(node: Node, text: String, case_sensitive: bool, exact: bool, visible_only: bool) -> BaseButton:
		if node is BaseButton:
			var button := node as BaseButton
			if (not visible_only or button.is_visible_in_tree()) and _text_matches(button.text, text, case_sensitive, exact):
				return button
		for child in node.get_children():
			var found := _find_button_by_text_recursive(child, text, case_sensitive, exact, visible_only)
			if found != null:
				return found
		return null

	func _text_matches(value: String, query: String, case_sensitive: bool, exact: bool) -> bool:
		var left := value if case_sensitive else value.to_lower()
		var right := query if case_sensitive else query.to_lower()
		return left == right if exact else left.contains(right)

	func _print_event(prefix: String, data: Dictionary = {}) -> void:
		var event := data.duplicate(true)
		event["session_id"] = _session_id
		event["script_run_id"] = _script_run_id
		event["time_ms"] = Time.get_ticks_msec()
		print("%s: %s" % [prefix, JSON.stringify(event)])

	func _write_status(status: String, error: String = "", extra: Dictionary = {}) -> void:
		_helper._write_runtime_script_status(_status_path, _session_id, _script_run_id, status, _captures, error, extra)

	func _release_pressed_actions() -> void:
		for action in _pressed_actions.duplicate():
			if InputMap.has_action(action):
				Input.action_release(action)
		_pressed_actions.clear()

func _ready() -> void:
	if Engine.is_editor_hint():
		return
	var runtime_spec := OS.get_environment(RUNTIME_SPEC_ENV)
	if not runtime_spec.strip_edges().is_empty():
		var request := _read_json_file(runtime_spec)
		if str(request.get("mode", "")) == "runtime_session":
			_run_env_runtime_session.call_deferred(request)
		else:
			_run_env_runtime_check.call_deferred(runtime_spec)

func _run_runtime_script(data: Array) -> void:
	var script_run_id := str(data[0]) if data.size() > 0 else ""
	var session_id := str(data[1]) if data.size() > 1 else ""
	var script_path := str(data[2]) if data.size() > 2 else ""
	var status_path := str(data[3]) if data.size() > 3 else ""
	get_tree().root.set_meta("_fennara_runtime_session_id", session_id)
	var ctx := RuntimeScriptContext.new(self, session_id, script_run_id, status_path)
	ctx._write_status("running", "", {"scene_closed": false, "session_active": true})
	ctx._print_event("FENNARA_SCRIPT_STARTED", {"script_path": script_path})

	var script := load(script_path)
	if script == null:
		var load_error := "Could not load runtime script: %s" % script_path
		ctx._print_event("FENNARA_SCRIPT_FAILED", {"error": load_error})
		ctx._write_status("failed", load_error)
		return

	if script is Script and not script.can_instantiate():
		var instantiate_error := "Runtime script could not instantiate, likely due to parse errors: %s" % script_path
		ctx._print_event("FENNARA_SCRIPT_FAILED", {"error": instantiate_error})
		ctx._write_status("failed", instantiate_error)
		return
	var instance = script.new()
	if instance == null or not instance.has_method("run"):
		var contract_error := "Runtime script must instantiate and define run(ctx)."
		ctx._print_event("FENNARA_SCRIPT_FAILED", {"error": contract_error})
		ctx._write_status("failed", contract_error)
		return

	await instance.call("run", ctx)
	if ctx.has_close_requested():
		ctx._print_event("FENNARA_SCRIPT_COMPLETED", {})
	else:
		ctx._release_pressed_actions()
		ctx._print_event("FENNARA_SCRIPT_COMPLETED", {"scene_closed": false})
		ctx._write_status("completed", "", {"scene_closed": false, "session_active": true})

func _finish_runtime_script_session(ctx) -> void:
	_runtime_session_closing = true
	ctx._release_pressed_actions()
	await get_tree().process_frame
	await get_tree().process_frame
	if get_tree().current_scene != null:
		get_tree().current_scene.queue_free()
	await get_tree().process_frame
	await get_tree().process_frame
	get_tree().quit(0)

func _run_env_runtime_session(request: Dictionary) -> void:
	_runtime_session_closing = false
	_file_session_id = str(request.get("session_id", ""))
	_file_command_dir = str(request.get("command_dir", ""))
	_file_artifact_dir = str(request.get("artifact_dir", ""))
	if _file_session_id.strip_edges().is_empty() or _file_command_dir.strip_edges().is_empty():
		return

	DirAccess.make_dir_recursive_absolute(_file_command_dir)
	if not _file_artifact_dir.strip_edges().is_empty():
		DirAccess.make_dir_recursive_absolute(_file_artifact_dir)

	var scene_frame := await _wait_for_env_runtime_scene_frame()
	await _raise_runtime_window_once()
	print("FENNARA_RUNTIME_SESSION_READY: %s" % JSON.stringify({
		"session_id": _file_session_id,
		"scene_frame": scene_frame,
		"scene_path": str(request.get("scene_path", "")),
		"time_ms": Time.get_ticks_msec(),
	}))

	while is_inside_tree() and not _runtime_session_closing:
		_poll_runtime_session_commands()
		for _i in range(6):
			if _runtime_session_closing:
				break
			await get_tree().process_frame

func _poll_runtime_session_commands() -> void:
	var dir := DirAccess.open(_file_command_dir)
	if dir == null:
		return
	dir.list_dir_begin()
	while true:
		var file_name := dir.get_next()
		if file_name.is_empty():
			break
		if dir.current_is_dir() or not file_name.ends_with(".json"):
			continue
		if _file_processed_commands.has(file_name):
			continue
		_file_processed_commands[file_name] = true
		var command_path := _file_command_dir.path_join(file_name)
		var command := _read_json_file(command_path)
		if command.is_empty():
			continue
		if str(command.get("action", "")) == "run_runtime_script":
			_run_runtime_script.call_deferred([
				str(command.get("script_run_id", "")),
				str(command.get("session_id", _file_session_id)),
				str(command.get("script_path", "")),
				str(command.get("status_path", "")),
			])
	dir.list_dir_end()

func _capture_runtime_script(ctx, label: String, max_resolution: int = 1280) -> Dictionary:
	await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var capture := _viewport_image(max_resolution)
	if not capture.get("success", false):
		var capture_error := str(capture.get("error", "Runtime screenshot failed."))
		ctx.error(capture_error)
		return {"success": false, "error": capture_error}

	var captures_dir: String = ctx.get_captures_dir()
	if not _ensure_dir(captures_dir):
		var dir_message := "Could not create runtime capture directory."
		ctx.error(dir_message)
		return {"success": false, "error": dir_message}

	var file_name := "%s_%s_%d.png" % [
		_safe_file_component(ctx.get_script_run_id(), "script"),
		_safe_file_component(label, "capture"),
		Time.get_ticks_msec(),
	]
	var image_res_path: String = captures_dir.path_join(file_name)
	var image: Image = capture["image"]
	var png_error_code := image.save_png(image_res_path)
	if png_error_code != OK:
		var png_error := "Failed to save runtime capture PNG."
		ctx.error(png_error)
		return {"success": false, "error": png_error}

	var result := {
		"success": true,
		"label": label,
		"image_res_path": image_res_path,
		"image_path": _absolute_path(image_res_path),
		"width": capture["width"],
		"height": capture["height"],
		"original_width": capture["original_width"],
		"original_height": capture["original_height"],
	}
	ctx._print_event("FENNARA_SCRIPT_CAPTURE", result)
	return result

func _run_env_runtime_check(spec_path: String) -> void:
	var request := _read_json_file(spec_path)
	if request.is_empty():
		return

	var status_path := str(request.get("status_path", ""))
	var check_id := str(request.get("check_id", ""))
	_write_runtime_check_status(status_path, check_id, "helper_started", [], [], {
		"spec_path": spec_path,
		"current_scene": get_tree().current_scene.scene_file_path if get_tree().current_scene != null else "",
		"timestamp_ms": Time.get_ticks_msec(),
	})

	var screenshot_dir := str(request.get("screenshot_dir", ""))
	var dir_error := DirAccess.make_dir_recursive_absolute(screenshot_dir)
	if dir_error != OK:
		_write_runtime_check_status(status_path, check_id, "failed", [], [], {
			"error": "Could not create screenshot directory: %s" % screenshot_dir,
		})
		return

	var times := _normalized_screenshot_times(request.get("screenshot_times", []))
	var run_seconds := maxf(0.0, float(request.get("run_seconds", 0.0)))
	var max_resolution := int(request.get("max_resolution", 1280))
	var captures: Array[Dictionary] = []
	var errors: Array[String] = []
	var last_time := 0.0
	var scene_frame := await _wait_for_env_runtime_scene_frame()
	if not scene_frame.get("success", false):
		_write_runtime_check_status(status_path, check_id, "failed", captures, errors, {
			"error": str(scene_frame.get("error", "Scene did not render a frame.")),
		})
		return

	await _raise_runtime_window_once()
	_write_runtime_check_status(status_path, check_id, "scene_frame_ready", captures, errors, {
		"scene_path": scene_frame.get("scene_path", ""),
		"scene_frame_ready_ms": scene_frame.get("scene_frame_ready_ms", 0),
	})

	for i in range(times.size()):
		var target_time := float(times[i])
		var wait_time := maxf(0.0, target_time - last_time)
		if wait_time > 0.0:
			await get_tree().create_timer(wait_time, true, false, true).timeout
		last_time = target_time

		var capture := await _capture_env_runtime_screenshot(
			screenshot_dir,
			check_id if not check_id.is_empty() else "runtime",
			i + 1,
			target_time,
			max_resolution
		)
		if capture.get("success", false):
			captures.append(capture)
		else:
			var capture_error := str(capture.get("error", "Runtime screenshot failed."))
			errors.append(capture_error)
			if capture_error.contains("viewport"):
				break
		_write_runtime_check_status(status_path, check_id, "running", captures, errors)

	if run_seconds > last_time:
		await get_tree().create_timer(run_seconds - last_time, true, false, true).timeout

	_write_runtime_check_status(status_path, check_id, "completed" if errors.is_empty() else "completed_with_errors", captures, errors)
	get_tree().quit(0)

func _raise_runtime_window_once() -> void:
	if DisplayServer.get_name() == "headless":
		return
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, true)
	DisplayServer.window_move_to_foreground()
	DisplayServer.window_request_attention()
	await get_tree().process_frame
	await get_tree().process_frame
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, false)

func _wait_for_env_runtime_scene_frame() -> Dictionary:
	var started_ms := Time.get_ticks_msec()
	var tree := get_tree()
	for _i in range(600):
		await tree.process_frame
		var current_scene := tree.current_scene
		if current_scene == null:
			continue
		await tree.process_frame
		await tree.process_frame
		var viewport := tree.root
		var texture := viewport.get_texture()
		if texture == null:
			continue
		var image := texture.get_image()
		if image == null or image.is_empty():
			continue
		return {
			"success": true,
			"scene_path": current_scene.scene_file_path,
			"scene_frame_ready_ms": Time.get_ticks_msec() - started_ms,
		}
	return {"success": false, "error": "Scene did not produce a readable viewport frame before the runtime helper wait limit."}

func _capture_env_runtime_screenshot(
	screenshot_dir: String,
	check_id: String,
	index: int,
	time_seconds: float,
	max_resolution: int
) -> Dictionary:
	await get_tree().process_frame
	await get_tree().process_frame

	var capture := _viewport_image(max_resolution)
	if not capture.get("success", false):
		return {
			"success": false,
			"error": str(capture.get("error", "Runtime screenshot failed.")),
			"time_seconds": time_seconds,
		}
	var file_name := "%s_%02d_%.2fs.png" % [
		_safe_file_component(check_id, "runtime"),
		index,
		time_seconds,
	]
	var image_path := screenshot_dir.path_join(file_name)
	var image: Image = capture["image"]
	var png_error := image.save_png(image_path)
	if png_error != OK:
		return {"success": false, "error": "Failed to save runtime screenshot PNG.", "time_seconds": time_seconds}

	return {
		"success": true,
		"time_seconds": time_seconds,
		"image_path": image_path,
		"width": capture["width"],
		"height": capture["height"],
		"original_width": capture["original_width"],
		"original_height": capture["original_height"],
	}

func _normalized_screenshot_times(value: Variant) -> Array[float]:
	var times: Array[float] = []
	if value is Array:
		for entry in value:
			var time := maxf(0.0, float(entry))
			if times.is_empty() or absf(times[times.size() - 1] - time) > 0.001:
				times.append(time)
	times.sort()
	return times

func _read_json_file(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if parsed is Dictionary:
		return parsed
	return {}

func _write_env_runtime_status(path: String, payload: Dictionary) -> void:
	if path.strip_edges().is_empty():
		return
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(JSON.stringify(payload, "\t"))
	file.close()

func _write_runtime_check_status(path: String, check_id: String, status: String, captures: Array[Dictionary], errors: Array[String], extra: Dictionary = {}) -> void:
	var payload := extra.duplicate(true)
	payload["success"] = errors.is_empty() and status != "helper_started" and status != "scene_frame_ready" and status != "failed"
	payload["status"] = status
	payload["check_id"] = check_id
	payload["captures"] = captures
	if not errors.is_empty():
		payload["errors"] = errors
	_write_env_runtime_status(path, payload)

func _write_runtime_script_status(status_path: String, session_id: String, script_run_id: String, status: String, captures: Array[Dictionary], error: String = "", extra: Dictionary = {}) -> void:
	var base_dir := status_path.get_base_dir()
	if base_dir.is_absolute_path():
		DirAccess.make_dir_recursive_absolute(base_dir)
	else:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(base_dir))
	var file := FileAccess.open(status_path, FileAccess.WRITE)
	if file == null:
		return
	var payload := {
		"session_id": session_id,
		"script_run_id": script_run_id,
		"status": status,
		"captures": captures,
		"updated_at_ms": Time.get_ticks_msec(),
	}
	for key in extra.keys():
		payload[key] = extra[key]
	if not error.is_empty():
		payload["error"] = error
	file.store_string(JSON.stringify(payload))
	file.close()
