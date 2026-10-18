import * as store from "./store.js";

let root = store.rememberedRoot();
let data = store.load(root);

function $(id) { return document.getElementById(id); }

function persist() { store.save(root, data); }

function treeHtml() {
  const rows = [];
  store.flatten(data, "", 0, rows);
  return rows.map(({ doc, depth }) => {
    const on = doc.id === data.active ? " is-on" : "";
    const kind = doc.folder ? "folder" : "piece";
    return `<button type="button" class="twig ${kind} d${Math.min(depth, 6)}${on}" id="node-${doc.id}" onclick="on_open" data-id="${doc.id}">${escapeText(doc.name)}</button>`;
  }).join("");
}

function escapeText(text) {
  return String(text).replace(/[&<>]/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[ch]));
}

function paintTree() {
  $("tree").querySelectorAll("button").forEach((node) => node.remove());
  $("tree").insertAdjacentHTML("beforeend", treeHtml());
}

function paintDoc() {
  const doc = store.find(data, data.active);
  const tools = $("doc-tools");
  if (tools) tools.hidden = !doc;
  $("doc-title").textContent = doc ? doc.name : "未选择";
  $("rename-name").value = doc ? doc.name : "";
  const piece = doc && !doc.folder;
  $("editor").disabled = (!!doc && doc.folder) || !!data.read;
  $("editor").hidden = !!data.read;
  $("reader").hidden = !data.read;
  const body = piece ? (doc.body || "") : doc ? `文件夹「${doc.name}」` : "";
  if (!data.read && piece && document.activeElement !== $("editor")) $("editor").value = doc.body || "";
  $("reader").textContent = body || " ";
  $("mode-read").classList.toggle("is-on", !!data.read);
  $("mode-edit").classList.toggle("is-on", !data.read);
  paintToday();
  paintOutline();
}

const kGoalChars = 1000;
const textNames = ["tx0", "tx1", "tx2", "tx3", "tx4", "tx5", "tx6", "tx7", "tx8"];
let textStep = 3;
let saveTimer = 0;
let autoTimer = 0;

function charCount(text) {
  return Array.from(text || "").length;
}

function paintToday() {
  const day = store.today();
  let chars = 0;
  for (const doc of data.docs) {
    if (doc.folder || (doc.edited !== day && doc.created !== day)) continue;
    chars += charCount(doc.body);
  }
  const pct = Math.min(100, Math.floor(chars * 100 / kGoalChars));
  $("today-info").textContent = "今天 " + chars + " 字";
  $("goal-pct").textContent = pct + "%";
  $("goal-fill").className = "w" + Math.floor(pct / 10);
}

function headingTitle(line) {
  const trimmed = line.trim();
  const marks = trimmed.match(/^(#{1,3})\s+(.+)$/);
  if (marks) return { level: marks[1].length, title: marks[2] };
  if (trimmed.length <= 40 && trimmed.includes("第") && (trimmed.includes("章") || trimmed.includes("节"))) {
    const level = trimmed.includes("节") && !trimmed.includes("章") ? 2 : 1;
    return { level, title: trimmed };
  }
  return null;
}

function paintOutline() {
  const doc = store.find(data, data.active);
  const node = $("outline-body");
  if (!doc || doc.folder) {
    node.textContent = " ";
    return;
  }
  const lines = [];
  for (const line of (doc.body || "").split("\n")) {
    const heading = headingTitle(line);
    if (!heading) continue;
    lines.push("  ".repeat(heading.level - 1) + heading.title);
  }
  node.textContent = lines.length ? lines.join("\n") : " ";
}

function applyText() {
  const app = $("app");
  for (const name of textNames) app.classList.remove(name);
  app.classList.add(textNames[textStep]);
}

function flashSave() {
  const node = $("autosave");
  if (!node) return;
  node.hidden = false;
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => { node.hidden = true; }, 700);
}

function paintCalendar() {
  const now = new Date();
  const year = now.getFullYear();
  const month = now.getMonth();
  $("cal-title").textContent = `${year}年${month + 1}月`;
  const first = new Date(year, month, 1);
  const lead = (first.getDay() + 6) % 7;
  const count = new Date(year, month + 1, 0).getDate();
  const cells = [];
  for (let i = 0; i < lead; i++) cells.push(`<div class="cal-day pad"> </div>`);
  for (let day = 1; day <= count; day++) {
    const iso = `${year}-${String(month + 1).padStart(2, "0")}-${String(day).padStart(2, "0")}`;
    const names = data.docs.filter((doc) => !doc.folder && (doc.created === iso || doc.edited === iso)).map((doc) => doc.name);
    const label = names.length ? `${day} ${names.join(" ")}` : String(day);
    cells.push(`<div class="cal-day${names.length ? " has" : ""}">${escapeText(label)}</div>`);
  }
  $("cal-grid").querySelectorAll(".cal-day").forEach((node) => node.remove());
  $("cal-grid").insertAdjacentHTML("beforeend", cells.join(""));
}

function paintHistory() {
  const doc = store.find(data, data.active);
  const snaps = doc && doc.history ? doc.history : [];
  $("history-list").querySelectorAll("button").forEach((node) => node.remove());
  $("history-list").insertAdjacentHTML("beforeend", snaps.map((snap) =>
    `<button type="button" class="snap" id="snap-${snap.id}" onclick="on_snap" data-id="${snap.id}">${escapeText(snap.label || snap.id)}</button>`).join(""));
  if (!doc || doc.folder) $("history-note").textContent = "打开一篇文稿后可以回看保存过的版本。";
  else if (!snaps.length) $("history-note").textContent = "还没有更早的版本。再保存一次修改就会留下快照。";
  else $("history-note").textContent = "点一条快照查看当时的正文。编辑区仍是当前文字。";
}

function showView(name) {
  $("workspace").hidden = name !== "tree";
  $("calendar-view").hidden = name !== "calendar";
  $("history-view").hidden = name !== "history";
  $("view-tree").classList.toggle("is-on", name === "tree");
  $("view-calendar").classList.toggle("is-on", name === "calendar");
  $("view-history").classList.toggle("is-on", name === "history");
  if (name === "calendar") paintCalendar();
  if (name === "history") paintHistory();
}

function openDoc(id) {
  data.active = id;
  paintTree();
  paintDoc();
  persist();
}

function create(folder) {
  const id = store.freshId(data);
  const doc = {
    id,
    parent: store.parentForNew(data),
    name: (folder ? "新建文件夹 " : "未命名文稿 ") + id,
    folder,
    body: "",
    saved: "",
    created: store.today(),
    edited: store.today(),
    history: [],
  };
  data.docs.push(doc);
  data.active = id;
  paintTree();
  paintDoc();
  persist();
  $("status").textContent = folder ? "已新建文件夹" : "已新建文稿";
}

async function fragment(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(url);
  const parsed = new DOMParser().parseFromString(await response.text(), "text/html");
  const frag = document.createDocumentFragment();
  for (const child of [...parsed.body.childNodes]) frag.append(child);
  return frag;
}

async function mountPages() {
  if ($("workspace")) return;
  const stage = $("stage");
  const dock = $("dock");
  stage.append(await fragment("pages/desk.html"));
  stage.append(await fragment("pages/calendar.html"));
  stage.append(await fragment("pages/history.html"));
  dock.append(await fragment("pages/bar.html"));
  $("settings-slot").append(await fragment("pages/storage.html"));
}

export async function mount() {
  try {
    await mountPages();
  } catch (err) {
    const note = $("file-notice");
    if (note) note.hidden = false;
    return;
  }
  const note = $("file-notice");
  if (note) note.hidden = true;
  $("storage-path").value = root;
  $("storage-status").textContent = root;
  paintTree();
  paintDoc();
  applyText();
  showView("tree");

  window.on_new_piece = () => create(false);
  window.on_new_folder = () => create(true);
  window.on_open = (event) => {
    const node = event && event.target && event.target.closest ? event.target.closest("[data-id]") : null;
    if (node) openDoc(node.getAttribute("data-id"));
  };
  window.on_rename = () => {
    const doc = store.find(data, data.active);
    const name = $("rename-name").value.trim();
    if (!doc || !name) return;
    doc.name = name;
    doc.edited = store.today();
    paintTree();
    paintDoc();
    persist();
    $("status").textContent = "已重命名";
  };
  window.on_editor = () => {
    if (data.read) return;
    let doc = store.find(data, data.active);
    if (doc && doc.folder) return;
    if (!doc) {
      create(false);
      doc = store.find(data, data.active);
    }
    if (!doc || doc.folder) return;
    doc.body = $("editor").value;
    doc.edited = store.today();
    $("reader").textContent = doc.body || " ";
    paintToday();
    paintOutline();
    clearTimeout(autoTimer);
    autoTimer = setTimeout(() => window.on_save(), 800);
  };
  window.on_mode_read = () => {
    const doc = store.find(data, data.active);
    if (doc && !doc.folder) doc.body = $("editor").value;
    data.read = true;
    paintDoc();
  };
  window.on_mode_edit = () => {
    data.read = false;
    paintDoc();
  };
  window.on_delete = () => {
    if (!data.active) return;
    store.removeTree(data, data.active);
    paintTree();
    paintDoc();
    persist();
    $("status").textContent = "已删除";
  };
  window.on_save = () => {
    const doc = store.find(data, data.active);
    if (!doc || doc.folder) return;
    if (!data.read) doc.body = $("editor").value;
    store.snapshot(doc);
    paintDoc();
    persist();
    $("status").textContent = "已保存";
    paintToday();
    paintOutline();
    flashSave();
  };
  window.on_storage = () => {
    const next = $("storage-path").value.trim();
    if (!next) {
      $("storage-status").textContent = "请填写存储位置";
      return;
    }
    persist();
    root = next;
    data = store.load(root);
    $("storage-status").textContent = "已设为存储位置";
    paintTree();
    paintDoc();
  };
  window.on_view_tree = () => showView("tree");
  window.on_view_calendar = () => showView("calendar");
  window.on_view_history = () => showView("history");
  window.on_snap = (event) => {
    const node = event && event.target && event.target.closest ? event.target.closest("[data-id]") : null;
    if (!node) return;
    const doc = store.find(data, data.active);
    const snap = doc && doc.history && doc.history.find((item) => item.id === node.getAttribute("data-id"));
    if (!snap) return;
    $("history-body").textContent = snap.text || " ";
    $("history-note").textContent = "这是较早的版本，和当前正文不同。";
  };
  window.on_tree_toggle = () => {
    document.getElementById("app").classList.toggle("show-tree");
  };
  window.on_settings = () => {
    const card = $("storage-bar");
    if (card) card.hidden = !card.hidden;
  };
  window.on_text_wheel = (event) => {
    if (!event || !event.ctrlKey) return;
    event.preventDefault();
    const dy = event.deltaY || 0;
    if (dy < 0) textStep = Math.min(8, textStep + 1);
    else if (dy > 0) textStep = Math.max(0, textStep - 1);
    applyText();
  };
}
