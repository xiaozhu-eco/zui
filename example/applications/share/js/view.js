/*
 * Browser demo: feed store.js into the templates in index.html and switch screens by the
 * `onclick` names.
 *
 * Same convention as the other examples: onclick / oninput / onchange / onkeydown in the
 * HTML are just zui bind names. The browser does not route them through window; this file
 * dispatches them by event delegation. zui runs no script and binds those names in C++.
 */
import { seed } from "./store.js";

const KIND_LABEL = {
  dir: "文件夹",
  pdf: "PDF",
  apk: "安装包",
  vid: "视频",
  img: "图片",
  doc: "文本",
};

const SCREENS = ["name", "browse", "pick", "wait", "fail", "transfer", "done", "settings"];

const state = seed();

let app = null;
let current = "name";
let pendingPeer = null; // the peer being waited on / transferred with
let subdir = null; // non-null once a folder is entered; the sample list goes empty, 上级 returns
let timer = 0; // demo advancement timer (wait → transfer → done)

const $ = (id) => document.getElementById(id);

/* ---------------------------------------------------------------- numbers and text */
function trimNum(text) {
  if (!text.includes(".")) return text;
  return text.replace(/0+$/, "").replace(/\.$/, "");
}

function fmtBytes(n) {
  const units = ["B", "KB", "MB", "GB", "TB"];
  let x = n;
  let i = 0;
  while (x >= 1024 && i < 4) {
    x /= 1024;
    i += 1;
  }
  if (i === 0) return `${n} B`;
  let value;
  if (x >= 100) value = String(Math.round(x));
  else if (x >= 10) value = trimNum(x.toFixed(1));
  else value = trimNum(x.toFixed(2));
  return `${value} ${units[i]}`;
}

function fmtRate(bytesPerSec) {
  return `${fmtBytes(bytesPerSec)}/s`;
}

function initial(name) {
  if (!name) return "竹";
  return Array.from(name)[0];
}

function peerInitial(peer) {
  return peer.short || initial(peer.name);
}

/* ---------------------------------------------------------------- stamp templates */
function stamp(hostId, rows) {
  const host = $(hostId);
  if (!host) return;
  const tpl = host.querySelector("template");
  if (!tpl) return;
  const markup = tpl.innerHTML;
  for (const child of Array.from(host.children)) {
    if (child.tagName !== "TEMPLATE") child.remove();
  }
  let html = "";
  for (const row of rows) {
    html += markup.replace(/\{\{(\w+)\}\}/g, (whole, key) => (key in row ? String(row[key]) : whole));
  }
  host.insertAdjacentHTML("beforeend", html);
}

/* ---------------------------------------------------------------- selection */
function listedFiles() {
  return subdir ? [] : state.files;
}

function selectedFiles() {
  return state.files.filter((f) => f.on);
}

function selectedBytes() {
  return selectedFiles().reduce((total, f) => total + (f.kind === "dir" ? 0 : f.size), 0);
}

function selectedLabel() {
  const sel = selectedFiles();
  if (!sel.length) return "已选 0 项";
  const bytes = selectedBytes();
  return bytes > 0 ? `已选 ${sel.length} 项 · ${fmtBytes(bytes)}` : `已选 ${sel.length} 项`;
}

/* The app-level error line the native session drives with set("app-error", "text"/"visible"). */
function showError(text) {
  const el = $("app-error");
  el.textContent = text || "";
  el.hidden = !text;
}

/* ---------------------------------------------------------------- render */
function renderPeers() {
  const has = state.peers.length > 0;
  stamp("peer-chips", state.peers.map((p) => ({ id: p.id, name: p.name })));
  stamp(
    "peer-side",
    state.peers.map((p) => ({ id: p.id, initial: peerInitial(p), name: p.name, ip: p.ip })),
  );
  $("peer-count").textContent = `${state.peers.length} 台在线`;
  $("peer-count-side").textContent = `${state.peers.length} 在线`;
  $("peers-empty").hidden = has;
  // The pick screen's 发送 buttons are the same peer list. A peer that arrives
  // while that screen is open has to be stamped there too, matching render_peers.
  if (current === "pick") renderPick();
}

function renderSelection() {
  const sel = selectedFiles();
  $("sel-label").textContent = selectedLabel();
  $("clear").hidden = sel.length === 0;
  $("share").disabled = sel.length === 0;
}

function renderFiles() {
  $("path").value = subdir ? `${state.cwd}/${subdir}` : state.cwd;
  stamp(
    "file-list",
    listedFiles().map((f) => ({
      id: f.id,
      path: f.path || f.name,
      cls: `${f.on ? "on " : ""}${f.kind === "dir" ? "is-dir" : "is-file"}`,
      kind: f.kind,
      name: f.name,
      size: f.kind === "dir" ? "—" : fmtBytes(f.size),
      type: KIND_LABEL[f.kind] || "文件",
    })),
  );
  renderSelection();
}

function renderPick() {
  $("pick-sum").textContent = selectedLabel();
  stamp(
    "pick-list",
    state.peers.map((p) => ({ id: p.id, initial: peerInitial(p), name: p.name, ip: p.ip })),
  );
  $("pick-empty").hidden = state.peers.length > 0;
}

function renderWait() {
  const peer = pendingPeer || state.peers[0] || { name: "对方" };
  $("wait-avatar").textContent = peerInitial(peer);
  $("wait-title").textContent = `等待「${peer.name}」接受…`;
  $("wait-count").textContent = `${selectedFiles().length} 个项目`;
  $("wait-size").textContent = `共 ${fmtBytes(selectedBytes())}`;
}

function renderFail() {
  const peer = pendingPeer || { name: state.xfer.peerName };
  $("fail-title").textContent = `「${peer.name}」拒绝了这次分享`;
  $("fail-sub").textContent = "对方没有接受这批文件";
}

function renderTransfer() {
  const x = state.xfer;
  const frac = x.total > 0 ? x.done / x.total : 0;
  $("xfer-kicker").textContent = x.sending ? "正在发送" : "正在接收";
  $("xfer-peer").textContent = x.peerName;
  $("xfer-ip").textContent = x.ip;
  $("xfer-pct").textContent = `${Math.round(frac * 100)}%`;
  // The fill width rides on the model, same as the native build: {{pct}} lands in the
  // template's style attribute.
  stamp("xfer-bar", [{ pct: (frac * 100).toFixed(1) }]);
  $("xfer-bytes").textContent = `${fmtBytes(x.done)}  /  ${fmtBytes(x.total)}`;
  if (x.done < x.total && x.speed > 1) {
    const remain = Math.round((x.total - x.done) / x.speed);
    $("xfer-rate").textContent = `${fmtRate(x.speed)}  ·  剩余约 ${remain} 秒`;
  } else {
    $("xfer-rate").textContent = fmtRate(x.speed);
  }
  $("xfer-file").textContent = x.file;
  stamp("done-list", x.doneFiles.map((name) => ({ name })));
}

function renderDone() {
  const x = state.xfer;
  $("done-sub").textContent = `已${x.sending ? "发送" : "接收"}到「${x.peerName}」`;
  $("done-size").textContent = fmtBytes(x.total);
  $("done-time").textContent = `${x.elapsed} 秒`;
  $("done-rate").textContent = fmtRate(x.speed);
}

function renderSettings() {
  $("set-name").value = state.me;
  $("set-dir").value = state.saveDir;
  $("me-name").textContent = state.me;
}

function renderIncoming() {
  const inc = state.incoming;
  $("inc-avatar").textContent = inc.fromShort || initial(inc.fromName);
  $("inc-name").textContent = inc.fromName;
  $("inc-line1").textContent = `${inc.nfiles} 个项目  ·  ${fmtBytes(inc.nbytes)}`;
  $("inc-line2").textContent = `将保存到  ${inc.dir}`;
}

/* ---------------------------------------------------------------- screen switching */
function clearTimer() {
  if (timer) {
    clearInterval(timer);
    clearTimeout(timer);
    timer = 0;
  }
}

function setScreen(name) {
  clearTimer();
  showError("");
  for (const s of SCREENS) app.classList.toggle(`screen-${s}`, s === name);
  current = name;
  if (name === "browse") {
    renderPeers();
    renderFiles();
  } else if (name === "pick") {
    renderPick();
  } else if (name === "wait") {
    renderWait();
  } else if (name === "fail") {
    renderFail();
  } else if (name === "transfer") {
    renderTransfer();
  } else if (name === "done") {
    renderDone();
  } else if (name === "settings") {
    renderSettings();
  }
}

function showIncoming(show) {
  app.classList.toggle("incoming", show);
  if (show) renderIncoming();
}

function startTransfer(peer) {
  state.xfer.peerName = peer.name;
  state.xfer.ip = peer.ip;
  state.xfer.total = Math.max(selectedBytes(), 1);
  state.xfer.done = state.xfer.total * 0.12;
  state.xfer.elapsed = 3;
  setScreen("transfer");
  // Run the progress so the demo can reach transfer → done.
  timer = setInterval(() => {
    state.xfer.done = Math.min(state.xfer.total, state.xfer.done + state.xfer.total * 0.06);
    if (state.xfer.done >= state.xfer.total) {
      clearTimer();
      setScreen("done");
    } else {
      renderTransfer();
    }
  }, 200);
}

/* ---------------------------------------------------------------- behavior */
const HANDLERS = {
  on_accel(e) {
    if (e.key !== "Escape") return;
    if (app.classList.contains("incoming")) showIncoming(false);
    else if (current !== "browse" && current !== "name") setScreen("browse");
  },

  on_name(e, el) {
    state.me = el.value;
    $("me-name").textContent = state.me || "阿竹";
  },

  on_name_key(e) {
    if (e.key !== "Enter") return;
    e.preventDefault();
    HANDLERS.on_start(e);
  },

  on_start(e) {
    e.preventDefault();
    if (!state.me.trim()) {
      showError("名字不能为空");
      return;
    }
    setScreen("browse");
  },

  on_settings() {
    setScreen("settings");
  },

  on_back() {
    setScreen("browse");
  },

  on_up() {
    if (subdir) subdir = null;
    else if (state.cwd.includes("/")) state.cwd = state.cwd.slice(0, state.cwd.lastIndexOf("/")) || "~";
    renderFiles();
  },

  on_refresh() {
    renderFiles();
  },

  on_path(e, el) {
    state.cwd = el.value.trim() || state.cwd;
    subdir = null;
    renderFiles();
  },

  on_row(e, el) {
    const file = state.files.find((f) => (f.path || f.name) === el.dataset.path);
    if (!file) return;
    if (file.kind === "dir") {
      subdir = file.name;
      renderFiles();
      return;
    }
    file.on = !file.on;
    renderFiles();
  },

  on_clear() {
    for (const f of state.files) f.on = false;
    renderFiles();
  },

  on_share() {
    if (selectedFiles().length) setScreen("pick");
  },

  on_peer() {
    /* The device strip on the main screen is display only; Share opens the picker. */
  },

  on_pick(e, el) {
    if (!selectedFiles().length) {
      showError("没有可发送的文件");
      return;
    }
    pendingPeer = state.peers.find((p) => p.id === el.dataset.id) || state.peers[0];
    setScreen("wait");
    // The peer accepts after ~1.8s, then the progress runs.
    timer = setTimeout(() => {
      if (current === "wait") startTransfer(pendingPeer);
    }, 1800);
  },

  on_wait_cancel() {
    setScreen("browse");
  },

  on_xfer_cancel() {
    setScreen("browse");
  },

  on_done() {
    setScreen("browse");
  },

  on_accept() {
    showIncoming(false);
    pendingPeer = state.peers[0];
    setScreen("transfer");
  },

  on_reject() {
    showIncoming(false);
  },

  on_save_name() {
    const next = $("set-name").value.trim();
    if (!next) {
      showError("名字不能为空");
      return;
    }
    state.me = next;
    $("set-name").value = state.me;
    $("me-name").textContent = state.me;
    showError("");
  },

  on_save_dir() {
    const next = $("set-dir").value.trim();
    if (!next) {
      showError("目录不能为空");
      return;
    }
    state.saveDir = next;
    $("set-dir").value = state.saveDir;
    showError("");
  },

  on_use_cwd() {
    $("set-dir").value = state.cwd;
  },
};

/*
 * Event delegation: walk up from the hit element to the nearest onclick name. Stamped
 * template rows (with data-id) and static nodes take the same path. zui has no such code —
 * it looks names up in the bind table.
 */
function dispatch(event, attr) {
  let node = event.target;
  while (node && node !== document) {
    if (node.getAttribute) {
      const name = node.getAttribute(attr);
      if (name && HANDLERS[name]) {
        HANDLERS[name](event, node);
        return;
      }
    }
    node = node.parentElement;
  }
}

document.addEventListener("click", (e) => dispatch(e, "onclick"));
document.addEventListener("input", (e) => dispatch(e, "oninput"));
document.addEventListener("change", (e) => dispatch(e, "onchange"));
document.addEventListener("keydown", (e) => dispatch(e, "onkeydown"));

/* ---------------------------------------------------------------- entry */
export function showScreen(name) {
  setScreen(name);
}

export function openIncoming() {
  showIncoming(true);
}

/** Test seam: one peer, so a walk can click peer-{{id}} / chip-{{id}} / send-{{id}}. */
export function stagePeer(peer) {
  state.peers = [peer];
  if (app) renderPeers();
}

/** Test seam: the same incoming fields the native session stages, then the sheet. */
export function stageIncoming(inc) {
  state.incoming = Object.assign({}, state.incoming, inc);
  showIncoming(true);
}

export function mount() {
  app = $("app");
  renderSettings();
  renderPeers();
  renderFiles();
  setScreen("name");
}

/* The browser oracle drives the same handlers as a click. applyListing only replaces the
   demo's seed list so a large directory can be compared; mount() still starts from store.js. */
function applyListing(files) {
  state.files = (files || []).map((file) => ({
    id: file.id || file.name,
    path: file.path || file.name,
    name: file.name,
    kind: file.kind || "doc",
    size: file.size == null ? 1 : file.size,
    on: !!file.on,
  }));
  subdir = null;
  renderFiles();
}

window.__zuiShare = { applyListing, openIncoming, showScreen, stagePeer, stageIncoming };
