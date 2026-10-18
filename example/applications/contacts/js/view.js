import { GROUPS, groupLabel, load, save, seed, uid } from "./store.js";

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

function trap(dialog, event) {
  if (event.key !== "Tab") return;
  const nodes = [...dialog.querySelectorAll("button, input, textarea, select")].filter(
    (node) => !node.disabled && node.tabIndex !== -1
  );
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
  const nativeGroup = document.getElementById("group-picks");
  if (nativeGroup) nativeGroup.hidden = true;
  const list = document.getElementById("list");
  const detail = document.getElementById("detail");
  const filters = document.getElementById("filters");
  const query = document.getElementById("q");
  const starOnly = document.getElementById("star-only");
  const status = document.getElementById("status");
  const editor = document.getElementById("editor");
  const editorForm = document.getElementById("editor-form");
  const editorTitle = document.getElementById("editor-title");
  const editorError = document.getElementById("editor-error");
  const confirmBack = document.getElementById("confirm");
  const confirmTitle = document.getElementById("confirm-title");
  const confirmBody = document.getElementById("confirm-body");
  const fields = {
    name: document.getElementById("f-name"),
    org: document.getElementById("f-org"),
    title: document.getElementById("f-title"),
    phone: document.getElementById("f-phone"),
    email: document.getElementById("f-email"),
    city: document.getElementById("f-city"),
    group: document.getElementById("f-group"),
    note: document.getElementById("f-note"),
    star: document.getElementById("f-star"),
  };

  let people = load() ?? seed();
  let selected = people[0] ? people[0].id : null;
  let group = "all";
  let editing = null;
  let opener = null;

  for (const [id, label] of [["all", "全部"], ...GROUPS]) {
    const button = el("button", "chip", label);
    button.type = "button";
    button.dataset.group = id;
    button.addEventListener("click", () => {
      group = id;
      paintFilters();
      render();
    });
    filters.append(button);
  }

  function paintFilters() {
    for (const button of filters.querySelectorAll(".chip")) {
      const on = button.dataset.group === group;
      button.classList.toggle("is-on", on);
      button.setAttribute("aria-pressed", on ? "true" : "false");
    }
  }

  function persist() {
    try {
      save(people);
      status.textContent = "";
    } catch {
      status.textContent = "没能写入本地存储";
    }
  }

  function confirmBox(title, body, okLabel) {
    confirmTitle.textContent = title;
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

  function matches(person) {
    if (group !== "all" && person.group !== group) return false;
    if (starOnly.checked && !person.star) return false;
    const q = query.value.trim().toLowerCase();
    if (!q) return true;
    return [person.name, person.org, person.title, person.phone, person.email, person.city, person.note]
      .join("\n")
      .toLowerCase()
      .includes(q);
  }

  function ordered() {
    return people
      .filter(matches)
      .sort((a, b) => Number(b.star) - Number(a.star) || a.name.localeCompare(b.name, "zh"));
  }

  function renderList() {
    const top = list.scrollTop;
    const rows = ordered();
    list.replaceChildren();
    if (!rows.length) {
      list.append(el("p", "empty", people.length ? "没有符合的联系人" : "还没有联系人"));
      list.scrollTop = 0;
      return;
    }
    for (const person of rows) {
      const button = el("button", `person${person.id === selected ? " is-on" : ""}`);
      button.type = "button";
      const avatar = el("span", "avatar", person.name.slice(0, 1));
      const who = el("span", "who");
      const name = el("span", "name", person.name);
      if (person.star) name.append(el("span", "star", " ★"));
      who.append(name);
      who.append(el("span", "meta", [groupLabel(person.group), person.org].filter(Boolean).join(" · ")));
      button.append(avatar, who);
      button.addEventListener("click", () => {
        selected = person.id;
        render();
      });
      list.append(button);
    }
    list.scrollTop = top;
  }

  function line(label, value, href) {
    const row = el("div", "line");
    row.append(el("span", "k", label));
    if (!value) row.append(el("span", "v muted", "—"));
    else if (href) {
      const link = el("a", "v", value);
      link.href = href;
      row.append(link);
    } else row.append(el("span", "v", value));
    return row;
  }

  function renderDetail() {
    const person = people.find((item) => item.id === selected);
    detail.replaceChildren();
    if (!person) {
      const empty = el("div", "detail-empty");
      empty.append(el("h2", "", people.length ? "没有选中的人" : "通讯录是空的"));
      empty.append(el("p", "", "新建一位，或恢复示例名单。"));
      const actions = el("div", "detail-actions");
      const create = el("button", "btn btn-primary", "新建");
      create.type = "button";
      create.addEventListener("click", () => openEditor(null));
      const restore = el("button", "btn", "恢复示例");
      restore.type = "button";
      restore.addEventListener("click", onRestore);
      actions.append(create, restore);
      empty.append(actions);
      detail.append(empty);
      return;
    }
    const head = el("div", "detail-head");
    head.append(el("span", "avatar avatar-lg", person.name.slice(0, 1)));
    const titles = el("div");
    titles.append(el("h2", "detail-name", person.name));
    titles.append(el("p", "detail-sub", [person.title, person.org].filter(Boolean).join(" · ") || groupLabel(person.group)));
    head.append(titles);
    const actions = el("div", "detail-actions");
    const edit = el("button", "btn btn-primary", "编辑");
    edit.type = "button";
    edit.addEventListener("click", () => openEditor(person));
    const remove = el("button", "btn btn-danger", "删除");
    remove.type = "button";
    remove.addEventListener("click", () => onDelete(person));
    actions.append(edit, remove);
    const tabs = el("div", "detail-tabs");
    tabs.setAttribute("role", "tablist");
    const tabCard = el("input", "hid");
    tabCard.type = "radio";
    tabCard.name = "detail-tab";
    tabCard.id = "tab-card";
    tabCard.checked = true;
    const tabCardLabel = el("label", "tab", "资料");
    tabCardLabel.htmlFor = "tab-card";
    const tabNote = el("input", "hid");
    tabNote.type = "radio";
    tabNote.name = "detail-tab";
    tabNote.id = "tab-note";
    const tabNoteLabel = el("label", "tab", "备注");
    tabNoteLabel.htmlFor = "tab-note";
    tabs.append(tabCard, tabCardLabel, tabNote, tabNoteLabel);
    const card = el("div", "card");
    const facts = el("div", "card-facts");
    const group = line("分组", person.star ? `${groupLabel(person.group)} · 已标星` : groupLabel(person.group));
    group.querySelector(".v").id = "d-group";
    const phone = line("电话", person.phone, person.phone ? `tel:${person.phone}` : "");
    phone.querySelector(".v").id = "d-phone";
    const email = line("邮箱", person.email, person.email ? `mailto:${person.email}` : "");
    email.querySelector(".v").id = "d-email";
    const city = line("城市", person.city);
    city.querySelector(".v").id = "d-city";
    facts.append(group, phone, email, city);
    const noteWrap = el("div", "card-note");
    const note = line("备注", person.note);
    note.querySelector(".v").id = "d-note";
    noteWrap.append(note);
    card.append(facts, noteWrap);
    detail.append(head, actions, tabs, card);
  }

  function render() {
    paintFilters();
    renderList();
    renderDetail();
  }

  function openEditor(person) {
    editing = person ? person.id : null;
    opener = document.activeElement;
    editorTitle.textContent = person ? "编辑联系人" : "新建联系人";
    editorError.textContent = "";
    fields.name.value = person?.name ?? "";
    fields.org.value = person?.org ?? "";
    fields.title.value = person?.title ?? "";
    fields.phone.value = person?.phone ?? "";
    fields.email.value = person?.email ?? "";
    fields.city.value = person?.city ?? "";
    fields.group.value = person?.group ?? "other";
    fields.note.value = person?.note ?? "";
    fields.star.checked = Boolean(person?.star);
    editor.hidden = false;
    fields.name.focus();
  }

  function closeEditor() {
    editor.hidden = true;
    if (opener && typeof opener.focus === "function") opener.focus();
  }

  function readForm() {
    const name = fields.name.value.trim();
    const email = fields.email.value.trim();
    if (!name) return "请填写姓名";
    if (email && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) return "邮箱格式不对";
    return {
      id: editing || uid(),
      name,
      org: fields.org.value.trim(),
      title: fields.title.value.trim(),
      phone: fields.phone.value.trim(),
      email,
      city: fields.city.value.trim(),
      group: fields.group.value,
      note: fields.note.value.trim(),
      star: fields.star.checked,
    };
  }

  editorForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const result = readForm();
    if (typeof result === "string") {
      editorError.textContent = result;
      return;
    }
    if (editing) people = people.map((item) => (item.id === editing ? result : item));
    else people = [result, ...people];
    selected = result.id;
    persist();
    closeEditor();
    render();
  });

  document.getElementById("editor-cancel").addEventListener("click", closeEditor);
  editor.addEventListener("keydown", (event) => trap(editorForm, event));
  confirmBack.addEventListener("keydown", (event) => trap(confirmBack.querySelector(".dialog"), event));

  async function onDelete(person) {
    const ok = await confirmBox("删除联系人", `删除「${person.name}」？此操作只影响这台浏览器里的名单。`);
    if (!ok) return;
    const index = people.findIndex((item) => item.id === person.id);
    people = people.filter((item) => item.id !== person.id);
    const next = people[index] || people[index - 1];
    selected = next ? next.id : null;
    persist();
    render();
  }

  async function onRestore() {
    const ok = await confirmBox("恢复示例", "用示例名单替换当前通讯录？你新增和改过的人会消失。", "恢复");
    if (!ok) return;
    people = seed();
    selected = people[0].id;
    persist();
    render();
  }

  document.getElementById("create").addEventListener("click", () => openEditor(null));
  document.getElementById("restore").addEventListener("click", onRestore);
  query.addEventListener("input", render);
  starOnly.addEventListener("change", render);
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
