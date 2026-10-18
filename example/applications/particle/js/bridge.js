const NAMES = [
  "on_effect", "on_count_inc", "on_count_dec", "on_speed_inc", "on_speed_dec",
  "on_spread_inc", "on_spread_dec", "on_split_down", "on_split_move", "on_split_up",
  "on_shader_input", "on_shader_import", "on_shader_export",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}
