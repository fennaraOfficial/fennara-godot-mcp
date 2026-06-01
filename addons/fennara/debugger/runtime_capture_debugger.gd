extends EditorDebuggerPlugin

const PREFIX := "fennara"
const CAPTURED_MESSAGES := [
	"fennara:hello",
	"fennara:screenshot_error",
	"fennara:screenshot_response",
	"hello",
	"screenshot_error",
	"screenshot_response",
]

func _has_capture(capture: String) -> bool:
	return capture == PREFIX

func _setup_session(session_id: int) -> void:
	RuntimeCaptureStore.note_debugger_event("setup_session", session_id)

func _capture(message: String, data: Array, session_id: int) -> bool:
	if message not in CAPTURED_MESSAGES:
		return false

	var action := message.trim_prefix("fennara:") if message.begins_with("fennara:") else message
	RuntimeCaptureStore.note_debugger_event(action, session_id)
	if action == "hello":
		var session := get_session(session_id)
		if session == null or !session.is_active():
			for candidate in get_sessions():
				if candidate != null and candidate.is_active():
					session = candidate
					break
		RuntimeCaptureStore.set_session(session)
		return true

	if action == "screenshot_error":
		var request_id := str(data[0]) if data.size() > 0 else ""
		var error := str(data[1]) if data.size() > 1 else "Runtime screenshot failed."
		RuntimeCaptureStore.fail_screenshot(request_id, error)
		return true

	if action == "screenshot_response":
		if data.size() < 6 or not (data[1] is PackedByteArray):
			var bad_request_id := str(data[0]) if data.size() > 0 else ""
			RuntimeCaptureStore.fail_screenshot(bad_request_id, "Runtime screenshot response was malformed.")
			return true

		RuntimeCaptureStore.complete_screenshot(
			str(data[0]),
			data[1],
			int(data[2]),
			int(data[3]),
			int(data[4]),
			int(data[5])
		)
		return true

	return false
