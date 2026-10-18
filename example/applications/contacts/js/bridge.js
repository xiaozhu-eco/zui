const NAMES = [
  "on_query",
  "on_star",
  "on_filter",
  "on_person",
  "on_create",
  "on_restore",
  "on_edit",
  "on_delete",
  "on_group",
  "on_save",
  "on_editor_cancel",
  "on_confirm_ok",
  "on_confirm_cancel",
  "on_accel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
