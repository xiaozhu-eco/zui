import { load, save, seed, uid } from "./store.js";

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

function isoDate(date) {
  const y = date.getFullYear();
  const m = String(date.getMonth() + 1).padStart(2, "0");
  const d = String(date.getDate()).padStart(2, "0");
  return `${y}-${m}-${d}`;
}

function dayLabel(ts) {
  const date = new Date(ts);
  const key = isoDate(date);
  const today = isoDate(new Date());
  const yesterday = new Date();
  yesterday.setDate(yesterday.getDate() - 1);
  if (key === today) return "今天";
  if (key === isoDate(yesterday)) return "昨天";
  return `${date.getMonth() + 1}月${date.getDate()}日`;
}

function clock(ts) {
  const date = new Date(ts);
  return `${String(date.getHours()).padStart(2, "0")}:${String(date.getMinutes()).padStart(2, "0")}`;
}

function trap(dialog, event) {
  if (event.key !== "Tab") return;
  const nodes = [...dialog.querySelectorAll("button, input, textarea, select")].filter((node) => !node.disabled);
  if (!nodes.length) return;
  const first = nodes[0];
  const last = nodes[nodes.length - 1];
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault();
    first.focus();
  }
}

export function mount() {
  const list = document.getElementById("conv-list");
  const query = document.getElementById("q");
  const threadName = document.getElementById("thread-name");
  const log = document.getElementById("log");
  const jump = document.getElementById("jump");
  const composer = document.getElementById("composer");
  const draft = document.getElementById("draft");
  const empty = document.getElementById("thread-empty");
  const threadHead = document.getElementById("thread-head");
  const status = document.getElementById("status");
  const creator = document.getElementById("creator");
  const creatorForm = document.getElementById("creator-form");
  const creatorName = document.getElementById("creator-name");
  const creatorError = document.getElementById("creator-error");
  const confirmBack = document.getElementById("confirm");
  const confirmBody = document.getElementById("confirm-body");

  let data = load() ?? seed();
  let active = data.convos[0] ? data.convos[0].id : null;

  function persist() {
    try {
      save(data);
      status.textContent = "";
    } catch {
      status.textContent = "没能写入本地存储";
    }
  }

  function messagesOf(id) {
    return data.messages.filter((item) => item.convId === id).sort((a, b) => a.ts - b.ts);
  }

  function lastOf(id) {
    const rows = messagesOf(id);
    return rows.length ? rows[rows.length - 1] : null;
  }

  function confirmBox(body) {
    confirmBody.textContent = body;
    confirmBack.hidden = false;
    document.getElementById("confirm-ok").focus();
    return new Promise((resolve) => {
      const finish = (value) => {
        confirmBack.hidden = true;
        resolve(value);
      };
      document.getElementById("confirm-ok").onclick = () => finish(true);
      document.getElementById("confirm-cancel").onclick = () => finish(false);
    });
  }

  function renderList() {
    const top = list.scrollTop;
    const q = query.value.trim().toLowerCase();
    const rows = data.convos
      .slice()
      .sort((a, b) => {
        const ta = lastOf(a.id)?.ts ?? a.created;
        const tb = lastOf(b.id)?.ts ?? b.created;
        return tb - ta;
      })
      .filter((conv) => {
        if (!q) return true;
        const last = lastOf(conv.id);
        return conv.name.toLowerCase().includes(q) || (last && last.text.toLowerCase().includes(q));
      });
    list.replaceChildren();
    if (!rows.length) {
      list.append(el("p", "empty", data.convos.length ? "没有符合的会话" : "还没有会话"));
      return;
    }
    for (const conv of rows) {
      const last = lastOf(conv.id);
      const button = el("button", `conv${conv.id === active ? " is-on" : ""}`);
      button.type = "button";
      const avatar = el("span", "avatar", conv.name.slice(0, 1));
      const who = el("span", "who");
      const line = el("span", "conv-line");
      line.append(el("span", "name", conv.name));
      if (last) line.append(el("span", "when", dayLabel(last.ts) === "今天" ? clock(last.ts) : dayLabel(last.ts)));
      who.append(line);
      who.append(el("span", "preview", last ? last.text : "还没有消息"));
      button.append(avatar, who);
      button.addEventListener("click", () => {
        active = conv.id;
        render(true);
        draft.focus();
      });
      list.append(button);
    }
    list.scrollTop = top;
  }

  function renderLog(stick) {
    const conv = data.convos.find((item) => item.id === active);
    const show = Boolean(conv);
    threadHead.hidden = !show;
    composer.hidden = !show;
    empty.hidden = show;
    log.replaceChildren();
    if (!conv) {
      jump.hidden = true;
      const infoName = document.getElementById("info-name");
      const infoCount = document.getElementById("info-count");
      if (infoName) infoName.textContent = "";
      if (infoCount) infoCount.textContent = "";
      return;
    }
    threadName.textContent = conv.name;
    const infoName = document.getElementById("info-name");
    const infoCount = document.getElementById("info-count");
    const messages = messagesOf(conv.id);
    if (infoName) infoName.textContent = conv.name;
    if (infoCount) infoCount.textContent = `${messages.length} 条消息`;
    let prevDay = "";
    for (const message of messages) {
      const day = dayLabel(message.ts);
      if (day !== prevDay) {
        log.append(el("div", "day-sep", day));
        prevDay = day;
      }
      const row = el("div", `msg${message.mine ? " mine" : ""}`);
      const bubble = el("div", "bubble");
      bubble.append(document.createTextNode(message.text));
      bubble.append(el("span", "time", clock(message.ts)));
      row.append(bubble);
      log.append(row);
    }
    if (stick) log.scrollTop = log.scrollHeight;
    jump.hidden = log.scrollHeight - log.scrollTop - log.clientHeight < 64;
  }

  function render(stick) {
    renderList();
    renderLog(stick);
  }

  function send() {
    const text = draft.value.trim();
    const conv = data.convos.find((item) => item.id === active);
    if (!text || !conv) return;
    data.messages.push({ id: uid(), convId: conv.id, mine: true, text, ts: Date.now() });
    draft.value = "";
    fit();
    persist();
    render(true);
  }

  function fit() {
    draft.style.height = "auto";
    draft.style.height = `${Math.min(120, draft.scrollHeight)}px`;
  }

  composer.addEventListener("submit", (event) => {
    event.preventDefault();
    send();
  });
  draft.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      send();
    }
  });
  draft.addEventListener("input", fit);
  log.addEventListener("scroll", () => {
    jump.hidden = log.scrollHeight - log.scrollTop - log.clientHeight < 64;
  });
  jump.addEventListener("click", () => {
    log.scrollTop = log.scrollHeight;
    jump.hidden = true;
  });
  query.addEventListener("input", () => renderList());

  document.getElementById("create").addEventListener("click", () => {
    creatorError.textContent = "";
    creatorName.value = "";
    creator.hidden = false;
    creatorName.focus();
  });
  document.getElementById("creator-cancel").addEventListener("click", () => {
    creator.hidden = true;
  });
  creatorForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const name = creatorName.value.trim();
    if (!name) {
      creatorError.textContent = "请填写名字";
      return;
    }
    const id = uid();
    data.convos.push({ id, name, created: Date.now() });
    active = id;
    creator.hidden = true;
    persist();
    render(true);
    draft.focus();
  });
  creator.addEventListener("keydown", (event) => trap(creatorForm, event));

  document.getElementById("remove").addEventListener("click", async () => {
    const conv = data.convos.find((item) => item.id === active);
    if (!conv) return;
    const ok = await confirmBox(`删除和「${conv.name}」的会话？消息只存在这台浏览器里。`);
    if (!ok) return;
    data.convos = data.convos.filter((item) => item.id !== conv.id);
    data.messages = data.messages.filter((item) => item.convId !== conv.id);
    active = data.convos[0] ? data.convos[0].id : null;
    persist();
    render(true);
  });

  document.getElementById("restore").addEventListener("click", async () => {
    const ok = await confirmBox("用示例会话替换当前内容？你写过的消息会消失。");
    if (!ok) return;
    data = seed();
    active = data.convos[0].id;
    persist();
    render(true);
  });

  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") return;
    if (!confirmBack.hidden) {
      document.getElementById("confirm-cancel").click();
      return;
    }
    if (!creator.hidden) creator.hidden = true;
  });
  confirmBack.addEventListener("keydown", (event) => trap(confirmBack.querySelector(".dialog"), event));

  render(true);
  fit();
}
