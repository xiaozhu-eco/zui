const NAMES = [
  "on_restore", "on_add_column", "on_col_name", "on_col_remove",
  "on_card", "on_card_up", "on_card_down", "on_add_card", "on_pick_column",
  "on_save", "on_editor_cancel", "on_editor_remove", "on_confirm_ok", "on_confirm_cancel",
  "on_accel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
