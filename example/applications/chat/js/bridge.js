const NAMES = [
  "on_query",
  "on_conv",
  "on_create",
  "on_restore",
  "on_remove",
  "on_send",
  "on_draft_enter",
  "on_jump",
  "on_log_scroll",
  "on_creator_cancel",
  "on_creator_save",
  "on_confirm_ok",
  "on_confirm_cancel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
