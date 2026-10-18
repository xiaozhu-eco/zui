const NAMES = [
  "on_restore", "on_add_album", "on_import", "on_album", "on_album_name", "on_delete_album",
  "on_photo", "on_light_back", "on_light_close", "on_prev", "on_next", "on_zoom_in", "on_zoom_out",
  "on_wheel", "on_stage_down", "on_stage_move", "on_stage_up", "on_light_album",
  "on_note", "on_note_key", "on_accel", "on_pick_album", "on_light_delete", "on_album_cancel",
  "on_album_save", "on_confirm_ok", "on_confirm_cancel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
