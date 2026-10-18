import {
  HOUR_PX,
  addDays,
  isoDate,
  layoutDay,
  longDate,
  minutes,
  monthCells,
  monthTitle,
  parseIso,
  startOfWeek,
  weekTitle,
} from "./dates.js";
import { COLORS, load, save, seed, uid } from "./store.js";

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

function pad(hour) {
  return `${String(hour).padStart(2, "0")}:00`;
}

function trap(dialog, event) {
  if (event.key !== "Tab") return;
  const nodes = [...dialog.querySelectorAll("button, input, textarea, select")].filter((node) => !node.disabled && !node.closest("[hidden]"));
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
  const monthView = document.getElementById("month-view");
  const weekView = document.getElementById("week-view");
  const weekScroll = document.getElementById("week-scroll");
  const weekHead = document.getElementById("week-head");
  const weekGrid = document.getElementById("week-grid");
  const agendaTitle = document.getElementById("agenda-title");
  const agendaEmpty = document.getElementById("agenda-empty");
  const agenda = document.getElementById("agenda-list");
  const title = document.getElementById("title");
  const status = document.getElementById("status");
  const editor = document.getElementById("editor");
  const editorForm = document.getElementById("editor-form");
  const editorTitle = document.getElementById("editor-title");
  const editorError = document.getElementById("editor-error");
  const removeBtn = document.getElementById("editor-remove");
  const confirmBack = document.getElementById("confirm");
  const confirmBody = document.getElementById("confirm-body");
  const fields = {
    title: document.getElementById("f-title"),
    date: document.getElementById("f-date"),
    start: document.getElementById("f-start"),
    end: document.getElementById("f-end"),
    place: document.getElementById("f-place"),
  };

  const today = new Date();
  let events = load() ?? seed();
  let selected = isoDate(today);
  let shown = new Date(today.getFullYear(), today.getMonth(), 1);
  let mode = "month";
  let editing = null;
  let opener = null;

  function persist() {
    try {
      save(events);
      status.textContent = "";
    } catch {
      status.textContent = "没能写入本地存储";
    }
  }

  function confirmBox(body, okLabel) {
    confirmBody.textContent = body;
    document.getElementById("confirm-ok").textContent = okLabel || "删除";
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

  function onDay(iso) {
    return events.filter((item) => item.date === iso).sort((a, b) => a.start.localeCompare(b.start));
  }

  function paintMode() {
    monthView.hidden = mode !== "month";
    weekView.hidden = mode !== "week";
    document.getElementById("view-month").classList.toggle("is-on", mode === "month");
    document.getElementById("view-week").classList.toggle("is-on", mode === "week");
    document.getElementById("view-month").setAttribute("aria-pressed", mode === "month" ? "true" : "false");
    document.getElementById("view-week").setAttribute("aria-pressed", mode === "week" ? "true" : "false");
    document.getElementById("prev").setAttribute("aria-label", mode === "month" ? "上个月" : "上一周");
    document.getElementById("next").setAttribute("aria-label", mode === "month" ? "下个月" : "下一周");
    title.textContent = mode === "month"
      ? monthTitle(shown.getFullYear(), shown.getMonth())
      : weekTitle(startOfWeek(selected));
  }

  function renderMonth() {
    const year = shown.getFullYear();
    const month = shown.getMonth();
    const todayIso = isoDate(new Date());
    monthView.replaceChildren();
    for (const name of ["一", "二", "三", "四", "五", "六", "日"]) monthView.append(el("div", "mh", name));
    for (const iso of monthCells(year, month)) {
      const date = parseIso(iso);
      const outside = date.getMonth() !== month;
      const cell = el("div", `cell${outside ? " is-out" : ""}${iso === selected ? " is-on" : ""}`);
      const num = el("span", `num${iso === todayIso ? " is-today" : ""}`, String(date.getDate()));
      cell.append(num);
      const rows = onDay(iso);
      for (const ev of rows.slice(0, 3)) {
        const chip = el("button", "chip", ev.title);
        chip.type = "button";
        chip.dataset.color = ev.color || "green";
        chip.addEventListener("click", (event) => {
          event.stopPropagation();
          selected = iso;
          render();
          openEditor(ev);
        });
        cell.append(chip);
      }
      if (rows.length > 3) cell.append(el("span", "more", `还有 ${rows.length - 3} 项`));
      cell.addEventListener("click", () => {
        selected = iso;
        render();
      });
      monthView.append(cell);
    }
  }

  function renderWeek() {
    const seen = weekScroll.dataset.ready === "1";
    const top = seen ? weekScroll.scrollTop : 8 * HOUR_PX;
    const todayIso = isoDate(new Date());
    const start = startOfWeek(selected);
    weekHead.replaceChildren();
    weekHead.append(el("div", "gutter-spacer"));
    weekGrid.replaceChildren();
    const gutter = el("div", "gutter");
    for (let hour = 0; hour < 24; hour += 1) {
      const label = el("span", "hour", pad(hour));
      label.style.top = `${hour * HOUR_PX}px`;
      gutter.append(label);
    }
    weekGrid.append(gutter);
    for (let i = 0; i < 7; i += 1) {
      const iso = addDays(start, i);
      const head = el("button", `dow${iso === selected ? " is-on" : ""}`);
      head.type = "button";
      head.append(el("span", "dow-name", "一二三四五六日"[i]));
      head.append(el("span", `dow-num${iso === todayIso ? " is-today" : ""}`, String(parseIso(iso).getDate())));
      head.addEventListener("click", () => {
        selected = iso;
        render();
      });
      weekHead.append(head);
      const col = el("div", `day${iso === selected ? " is-on" : ""}`);
      col.addEventListener("click", (event) => {
        if (event.target.closest(".block")) return;
        const y = event.clientY - col.getBoundingClientRect().top;
        let hour = Math.floor(y / HOUR_PX);
        if (hour < 0) hour = 0;
        if (hour > 23) hour = 23;
        selected = iso;
        render();
        openEditor(null, {
          date: iso,
          start: pad(hour),
          end: hour === 23 ? "23:59" : pad(hour + 1),
        });
      });
      for (const block of layoutDay(onDay(iso))) {
        const node = el("button", "block");
        node.type = "button";
        node.dataset.color = block.ev.color || "green";
        node.style.top = `${block.top}px`;
        node.style.height = `${block.height - 2}px`;
        node.style.left = `calc(${block.left}% + 2px)`;
        node.style.width = `calc(${block.width}% - 4px)`;
        node.append(el("span", "block-title", block.ev.title));
        node.append(el("span", "block-time", `${block.ev.start}–${block.ev.end}`));
        node.addEventListener("click", (event) => {
          event.stopPropagation();
          selected = iso;
          render();
          openEditor(block.ev);
        });
        col.append(node);
      }
      weekGrid.append(col);
    }
    weekScroll.scrollTop = top;
    weekScroll.dataset.ready = "1";
  }

  function renderAgenda() {
    agendaTitle.textContent = longDate(selected);
    const rows = onDay(selected);
    agendaEmpty.hidden = rows.length > 0;
    agenda.replaceChildren();
    if (!rows.length) return;
    for (const ev of rows) {
      const button = el("button", "agenda-item");
      button.type = "button";
      button.dataset.color = ev.color || "green";
      button.append(el("span", "agenda-time", `${ev.start}–${ev.end}`));
      button.append(el("span", "agenda-name", ev.title));
      if (ev.place) button.append(el("span", "agenda-place", ev.place));
      button.addEventListener("click", () => openEditor(ev));
      agenda.append(button);
    }
  }

  function render() {
    paintMode();
    renderMonth();
    renderWeek();
    renderAgenda();
  }

  function openEditor(event, defaults) {
    editing = event ? event.id : null;
    opener = document.activeElement;
    editorTitle.textContent = event ? "编辑日程" : "新建日程";
    editorError.textContent = "";
    fields.title.value = event ? event.title : "";
    fields.date.value = event ? event.date : defaults?.date || selected;
    fields.start.value = event ? event.start : defaults?.start || "09:00";
    fields.end.value = event ? event.end : defaults?.end || "10:00";
    fields.place.value = event ? event.place || "" : "";
    const color = event ? event.color : "green";
    for (const input of editorForm.querySelectorAll('input[name="color"]')) input.checked = input.value === color;
    removeBtn.hidden = !event;
    editor.hidden = false;
    fields.title.focus();
  }

  function closeEditor() {
    editor.hidden = true;
    if (opener && typeof opener.focus === "function") opener.focus();
  }

  function readForm() {
    const titleText = fields.title.value.trim();
    const date = fields.date.value;
    const start = fields.start.value;
    const end = fields.end.value;
    if (!titleText) return "请填写标题";
    if (!date) return "请选择日期";
    const startMin = minutes(start);
    const endMin = minutes(end);
    if (startMin == null || endMin == null) return "请填写开始和结束时间";
    if (endMin <= startMin) return "结束要晚于开始";
    const picked = editorForm.querySelector('input[name="color"]:checked');
    return {
      id: editing || uid(),
      title: titleText,
      date,
      start,
      end,
      place: fields.place.value.trim(),
      color: picked ? picked.value : "green",
    };
  }

  editorForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const result = readForm();
    if (typeof result === "string") {
      editorError.textContent = result;
      return;
    }
    if (editing) events = events.map((item) => (item.id === editing ? result : item));
    else events = [...events, result];
    selected = result.date;
    shown = new Date(parseIso(result.date).getFullYear(), parseIso(result.date).getMonth(), 1);
    persist();
    closeEditor();
    render();
  });

  removeBtn.addEventListener("click", async () => {
    const current = events.find((item) => item.id === editing);
    if (!current) return;
    const ok = await confirmBox(`删除「${current.title}」？`);
    if (!ok) return;
    events = events.filter((item) => item.id !== current.id);
    persist();
    closeEditor();
    render();
  });

  document.getElementById("editor-cancel").addEventListener("click", closeEditor);
  editor.addEventListener("keydown", (event) => trap(editorForm, event));
  confirmBack.addEventListener("keydown", (event) => trap(confirmBack.querySelector(".dialog"), event));

  document.getElementById("create").addEventListener("click", () => openEditor(null, { date: selected }));
  document.getElementById("today").addEventListener("click", () => {
    const now = new Date();
    selected = isoDate(now);
    shown = new Date(now.getFullYear(), now.getMonth(), 1);
    render();
  });
  document.getElementById("prev").addEventListener("click", () => shift(-1));
  document.getElementById("next").addEventListener("click", () => shift(1));
  document.getElementById("view-month").addEventListener("click", () => {
    mode = "month";
    shown = new Date(parseIso(selected).getFullYear(), parseIso(selected).getMonth(), 1);
    render();
  });
  document.getElementById("view-week").addEventListener("click", () => {
    mode = "week";
    render();
  });
  document.getElementById("restore").addEventListener("click", async () => {
    const ok = await confirmBox("用示例日程替换当前日历？你加过的日程会消失。", "恢复");
    if (!ok) return;
    events = seed();
    const now = new Date();
    selected = isoDate(now);
    shown = new Date(now.getFullYear(), now.getMonth(), 1);
    persist();
    render();
  });

  function shift(delta) {
    if (mode === "month") {
      shown = new Date(shown.getFullYear(), shown.getMonth() + delta, 1);
      const prefix = `${shown.getFullYear()}-${String(shown.getMonth() + 1).padStart(2, "0")}`;
      if (!selected.startsWith(prefix)) {
        const todayIso = isoDate(new Date());
        selected = todayIso.startsWith(prefix) ? todayIso : isoDate(shown);
      }
    } else {
      selected = addDays(selected, delta * 7);
      const date = parseIso(selected);
      shown = new Date(date.getFullYear(), date.getMonth(), 1);
    }
    render();
  }

  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") return;
    if (!confirmBack.hidden) {
      document.getElementById("confirm-cancel").click();
      return;
    }
    if (!editor.hidden) closeEditor();
  });

  render();
}
