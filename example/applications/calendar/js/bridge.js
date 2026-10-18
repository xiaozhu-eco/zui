const NAMES = [
  "on_today", "on_prev", "on_next", "on_month", "on_week", "on_restore", "on_create",
  "on_day", "on_week_day", "on_day_new", "on_event", "on_agenda", "on_color",
  "on_save", "on_editor_cancel", "on_editor_remove", "on_confirm_ok", "on_confirm_cancel",
  "on_accel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
