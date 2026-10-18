const NAMES = [
  "on_new_piece", "on_new_folder", "on_open", "on_rename", "on_editor",
  "on_mode_read", "on_mode_edit", "on_delete", "on_save", "on_storage",
  "on_view_tree", "on_view_calendar", "on_view_history", "on_snap", "on_tree_toggle",
  "on_settings", "on_text_wheel",
];

for (const name of NAMES) {
  if (typeof window[name] !== "function") window[name] = function () {};
}

// The onclick value is the bind name (`onclick="on_save"`), which the native
// host looks up. In the browser that attribute is a statement, not a call.
function invoke(event, attr) {
  const node = event.target && event.target.closest ? event.target.closest("[" + attr + "]") : null;
  if (!node) return;
  const name = node.getAttribute(attr);
  const fn = name && window[name];
  if (typeof fn === "function") fn(event);
}

document.addEventListener("click", (event) => invoke(event, "onclick"));
document.addEventListener("input", (event) => invoke(event, "oninput"));
document.addEventListener("wheel", (event) => {
  if (!event.ctrlKey) return;
  invoke(event, "onwheel");
}, { passive: false });
