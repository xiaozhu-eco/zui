export function defaultShader() {
  return [
    "#version 300 es",
    "precision highp float;",
    "#define U_COUNT 12.0",
    "#define U_SPEED 1.0",
    "#define U_SPREAD 0.6",
    "uniform float u_count;",
    "uniform float u_speed;",
    "uniform float u_spread;",
    "// keep-panel-ignored",
    "uniform float u_custom;",
    "void main() {",
    "  gl_FragColor = vec4(u_custom, u_speed / U_COUNT, U_SPREAD, 1.0);",
    "}",
    "",
  ].join("\n");
}

export function readDefine(src, name) {
  const key = "#define " + name;
  let pos = 0;
  while ((pos = src.indexOf(key, pos)) >= 0) {
    let i = pos + key.length;
    if (src[i] !== " " && src[i] !== "\t") {
      pos += key.length;
      continue;
    }
    while (src[i] === " " || src[i] === "\t") i += 1;
    const value = parseFloat(src.slice(i));
    if (!Number.isNaN(value)) return value;
    pos += key.length;
  }
  return null;
}

export function writeDefine(src, name, value) {
  const key = "#define " + name;
  const text = Number(value).toFixed(1);
  const pos = src.indexOf(key);
  if (pos < 0) return (src.endsWith("\n") ? src : src + "\n") + key + " " + text + "\n";
  let i = pos + key.length;
  while (src[i] === " " || src[i] === "\t") i += 1;
  let end = i;
  while (end < src.length && src[end] !== "\n" && src[end] !== "\r" && src[end] !== " " && src[end] !== "\t") end += 1;
  return src.slice(0, i) + text + src.slice(end);
}

export function createState() {
  return {
    effect: "fountain",
    count: 12,
    speed: 1,
    spread: 0.6,
    phase: 0.2,
    dots: [],
    source: defaultShader(),
    log: [],
    tree: 3,
    param: 3,
  };
}

export function reseed(state) {
  state.dots = [];
  state.phase = 0.2;
  for (let i = 0; i < state.count; i++) {
    const dot = { phase: 0.35 * i, x: 50, y: 50 };
    if (state.effect === "snow") {
      dot.x = 12 + (i % 8) * 9;
      dot.y = 8 + (i % 4) * 4;
    } else if (state.effect === "orbit") {
      dot.x = 50 + Math.cos(dot.phase) * (16 + state.spread * 8);
      dot.y = 50 + Math.sin(dot.phase) * (16 + state.spread * 8);
    } else {
      dot.x = 18 + (i % 8) * 7;
      dot.y = 72 - (i % 3) * 4;
    }
    state.dots.push(dot);
  }
}

export function step(state) {
  while (state.dots.length < state.count) {
    state.dots.push({ phase: 0.35 * state.dots.length, x: 10 + (state.dots.length % 8) * 8, y: 40 });
  }
  if (state.dots.length > state.count) state.dots.length = state.count;
  if (state.effect === "orbit") state.phase += state.speed * 0.45;
  for (const dot of state.dots) {
    if (state.effect === "orbit") {
      const angle = state.phase + dot.phase;
      dot.x = 50 + Math.cos(angle) * (16 + state.spread * 10);
      dot.y = 50 + Math.sin(angle) * (16 + state.spread * 10);
    } else if (state.effect === "snow") {
      dot.y += state.speed * 8;
      dot.x += state.spread * 6;
    } else {
      dot.y -= state.speed * 8;
      dot.x += state.spread * 6;
    }
    dot.x = Math.max(2, Math.min(96, dot.x));
    dot.y = Math.max(2, Math.min(96, dot.y));
  }
}

export function pullUniforms(state) {
  const count = readDefine(state.source, "U_COUNT");
  const speed = readDefine(state.source, "U_SPEED");
  const spread = readDefine(state.source, "U_SPREAD");
  if (count != null) state.count = Math.max(4, Math.min(48, Math.round(count)));
  if (speed != null && speed > 0) state.speed = speed;
  if (spread != null && spread > 0) state.spread = spread;
}

export function pushUniforms(state) {
  state.source = writeDefine(state.source, "U_COUNT", state.count);
  state.source = writeDefine(state.source, "U_SPEED", state.speed);
  state.source = writeDefine(state.source, "U_SPREAD", state.spread);
}
