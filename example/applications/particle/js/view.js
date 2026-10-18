import { createState, pullUniforms, pushUniforms, reseed, step } from "./sim.js";

const state = createState();
let drag = null;

function $(id) { return document.getElementById(id); }

function log(line) {
  state.log.push(line);
  const node = document.createElement("p");
  node.className = "log-line";
  node.textContent = line;
  $("log").appendChild(node);
}

function paintDots() {
  $("field").querySelectorAll(".dot").forEach((node) => node.remove());
  for (const dot of state.dots) {
    const node = document.createElement("div");
    node.className = "dot";
    node.style.left = dot.x.toFixed(1) + "%";
    node.style.top = dot.y.toFixed(1) + "%";
    $("field").appendChild(node);
  }
}

function paintPanel() {
  $("count-value").textContent = "数量 " + state.count;
  $("speed-value").textContent = "速度 " + state.speed.toFixed(1);
  $("spread-value").textContent = "散布 " + state.spread.toFixed(1);
  for (const name of ["fountain", "snow", "orbit"]) {
    $("effect-" + name).classList.toggle("is-on", state.effect === name);
  }
  if (document.activeElement !== $("shader-source")) $("shader-source").value = state.source;
}

function applyWidths() {
  const bench = $("bench");
  for (let i = 0; i <= 8; i++) {
    bench.classList.toggle("tree-" + i, i === state.tree);
    bench.classList.toggle("param-" + i, i === state.param);
  }
}

function selectEffect(id) {
  if (id !== "fountain" && id !== "snow" && id !== "orbit") return;
  state.effect = id;
  reseed(state);
  paintDots();
  paintPanel();
  const label = id === "snow" ? "飘雪" : id === "orbit" ? "环绕" : "喷泉";
  log("效果 " + label);
}

function readPickedFile(file) {
  const reader = new FileReader();
  reader.onload = () => {
    state.source = String(reader.result || "");
    pullUniforms(state);
    reseed(state);
    paintDots();
    paintPanel();
    log("导入着色器");
  };
  reader.readAsText(file);
}

export function mount() {
  pullUniforms(state);
  reseed(state);
  applyWidths();
  paintDots();
  paintPanel();
  log("工作台就绪");

  const file = document.createElement("input");
  file.type = "file";
  file.accept = ".glsl,.vert,.frag,text/plain";
  file.id = "shader-file";
  file.addEventListener("change", () => {
    if (file.files && file.files[0]) readPickedFile(file.files[0]);
  });
  $("params").appendChild(file);

  window.on_effect = (event) => {
    const node = event && event.target && event.target.closest ? event.target.closest("[data-id]") : null;
    if (node) selectEffect(node.getAttribute("data-id"));
  };
  $("effect-fountain").onclick = () => selectEffect("fountain");
  $("effect-snow").onclick = () => selectEffect("snow");
  $("effect-orbit").onclick = () => selectEffect("orbit");

  window.on_count_inc = () => bump("count", 1);
  window.on_count_dec = () => bump("count", -1);
  window.on_speed_inc = () => bump("speed", 1);
  window.on_speed_dec = () => bump("speed", -1);
  window.on_spread_inc = () => bump("spread", 0.4);
  window.on_spread_dec = () => bump("spread", -0.4);
  // The markup stores a handler name (`onclick="on_count_inc"`). The engine calls that name.
  // A browser compiles the attribute as a script statement, which does not call the function,
  // so the button itself has to.
  for (const id of ["count-dec", "count-inc", "speed-dec", "speed-inc", "spread-dec", "spread-inc", "shader-import", "shader-export"]) {
    const node = $(id);
    const name = node && node.getAttribute("onclick");
    if (name && typeof window[name] === "function") node.addEventListener("click", () => window[name]());
  }
  window.on_shader_input = () => {
    state.source = $("shader-source").value;
    pullUniforms(state);
    paintDots();
    paintPanel();
  };
  window.on_shader_import = () => {
    if (file.files && file.files[0]) {
      readPickedFile(file.files[0]);
      return;
    }
    log("导入失败：没有路径");
  };
  window.on_shader_export = () => {
    const named = $("shader-out").value.trim();
    if (!named) {
      log("导出失败：没有路径");
      return;
    }
    const blob = new Blob([state.source], { type: "text/plain" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = named.split(/[/\\]/).pop();
    link.click();
    URL.revokeObjectURL(link.href);
    log("导出着色器");
  };
  window.on_split_down = onSplitDown;
  window.on_split_move = onSplitMove;
  window.on_split_up = () => { drag = null; };

  for (const id of ["split-tree", "split-param"]) {
    const node = $(id);
    node.addEventListener("pointerdown", (event) => onSplitDown(event, id));
    node.addEventListener("pointermove", onSplitMove);
    node.addEventListener("pointerup", () => { drag = null; });
  }

  function bump(kind, delta) {
    if (kind === "count") state.count = Math.max(4, Math.min(48, state.count + delta));
    if (kind === "speed") state.speed = Math.max(1, Math.min(8, state.speed + delta));
    if (kind === "spread") state.spread = Math.max(0.4, Math.min(3, state.spread + delta));
    pushUniforms(state);
    if (kind === "count") {
      reseed(state);
      paintDots();
    }
    paintPanel();
    const label = kind === "count" ? "数量 " + state.count : kind === "speed" ? "速度 " + state.speed.toFixed(1) : "散布 " + state.spread.toFixed(1);
    log(label);
  }

  function onSplitDown(event, id) {
    const which = id || (event && event.currentTarget && event.currentTarget.id);
    drag = {
      which,
      x: event.clientX,
      start: which === "split-param" ? state.param : state.tree,
    };
    if (event.target && event.target.setPointerCapture && event.pointerId != null) {
      event.target.setPointerCapture(event.pointerId);
    }
  }

  function onSplitMove(event) {
    if (!drag) return;
    const steps = Math.round((event.clientX - drag.x) / 40);
    let next = drag.start + (drag.which === "split-param" ? -steps : steps);
    next = Math.max(0, Math.min(8, next));
    if (drag.which === "split-param") state.param = next;
    else state.tree = next;
    applyWidths();
  }

  let last = 0;
  function frame(now) {
    if (now - last > 30) {
      step(state);
      paintDots();
      last = now;
    }
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}
