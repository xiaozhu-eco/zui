/*
 * In the browser, fill the native callback names with empty functions so an inline
 * onclick= reference resolves. zui never runs this; those names come from C++ App::bind.
 * view.js handles the real behavior through event delegation.
 */
const NAMES = [
  "on_accel", "on_name", "on_name_key", "on_start",
  "on_settings", "on_back", "on_up", "on_refresh", "on_path",
  "on_row", "on_clear", "on_share", "on_peer",
  "on_pick", "on_wait_cancel", "on_xfer_cancel", "on_done",
  "on_accept", "on_reject",
  "on_save_name", "on_save_dir", "on_pick_save_dir", "on_request_storage", "on_use_cwd",
  "on_gesture_down", "on_gesture_files_down", "on_gesture_incoming_down",
  "on_gesture_up", "on_gesture_cancel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
