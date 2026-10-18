import { cardById, load, moveCard, moveColumn, save, seed, uid } from "./store.js";

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

export function mount() {
  const nativePicks = document.getElementById("column-picks");
  if (nativePicks) nativePicks.hidden = true;
  const boardEl = document.getElementById("board");
  const status = document.getElementById("status");
  const editor = document.getElementById("editor");
  const editorForm = document.getElementById("editor-form");
  const editorTitle = document.getElementById("editor-title");
  const editorError = document.getElementById("editor-error");
  const titleInput = document.getElementById("f-title");
  const noteInput = document.getElementById("f-note");
  const columnInput = document.getElementById("f-column");
  const removeBtn = document.getElementById("editor-remove");
  const confirmBack = document.getElementById("confirm");
  const confirmBody = document.getElementById("confirm-body");

  let board = load() ?? seed();
  let editing = null;
  let drag = null;
  let scrollDir = 0;
  let scrollTimer = 0;

  function persist() {
    try {
      save(board);
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

  function cardQuery() {
    return (document.getElementById("card-q")?.value || "").trim().toLowerCase();
  }

  function cardMatches(card, query) {
    if (!query) return true;
    return `${card.title}\n${card.note || ""}`.toLowerCase().includes(query);
  }

  function nudgeCard(id, delta) {
    for (const column of board.columns) {
      const index = column.cardIds.indexOf(id);
      if (index < 0) continue;
      const next = index + delta;
      if (next < 0 || next >= column.cardIds.length) return;
      column.cardIds.splice(index, 1);
      column.cardIds.splice(next, 0, id);
      persist();
      render();
      return;
    }
  }

  function render() {
    const names = new Map();
    const query = cardQuery();
    let shown = 0;
    let total = 0;
    for (const input of boardEl.querySelectorAll(".col-name")) {
      if (document.activeElement === input) names.set(input.dataset.id, input);
    }
    boardEl.replaceChildren();
    for (const column of board.columns) {
      const columnEl = el("section", "column");
      columnEl.dataset.id = column.id;
      const head = el("div", "col-head");
      const grip = el("button", "grip", "⋮⋮");
      grip.type = "button";
      grip.title = "拖动调整列顺序";
      grip.setAttribute("aria-label", `拖动列 ${column.name}`);
      grip.addEventListener("pointerdown", (event) => startColumnDrag(event, column.id));
      const name = el("input", "col-name");
      name.value = column.name;
      name.dataset.id = column.id;
      name.setAttribute("aria-label", "列名");
      name.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
          event.preventDefault();
          name.blur();
        }
      });
      name.addEventListener("blur", () => {
        const next = name.value.trim();
        column.name = next || column.name;
        name.value = column.name;
        persist();
      });
      let visibleInColumn = 0;
      for (const id of column.cardIds) {
        const card = cardById(board, id);
        if (card && cardMatches(card, query)) visibleInColumn += 1;
      }
      const count = el("span", "count", query ? String(visibleInColumn) : String(column.cardIds.length));
      const remove = el("button", "icon-btn", "×");
      remove.type = "button";
      remove.title = "删除列";
      remove.setAttribute("aria-label", `删除列 ${column.name}`);
      remove.addEventListener("click", () => onRemoveColumn(column));
      head.append(grip, name, count, remove);

      const cards = el("div", "cards");
      for (const id of column.cardIds) {
        const card = cardById(board, id);
        if (!card) continue;
        total += 1;
        if (!cardMatches(card, query)) continue;
        shown += 1;
        const button = el("article", "card");
        button.dataset.id = card.id;
        button.append(el("div", "card-title", card.title));
        if (card.note) button.append(el("div", "card-note", card.note));
        const moves = el("div", "card-moves");
        const up = el("button", "btn", "上移");
        up.type = "button";
        up.addEventListener("pointerdown", (event) => event.stopPropagation());
        up.addEventListener("click", (event) => {
          event.stopPropagation();
          nudgeCard(card.id, -1);
        });
        const down = el("button", "btn", "下移");
        down.type = "button";
        down.addEventListener("pointerdown", (event) => event.stopPropagation());
        down.addEventListener("click", (event) => {
          event.stopPropagation();
          nudgeCard(card.id, 1);
        });
        moves.append(up, down);
        button.append(moves);
        button.addEventListener("pointerdown", (event) => startCardDrag(event, card.id));
        cards.append(button);
      }
      const adder = el("form", "adder");
      const addInput = el("input");
      addInput.placeholder = "添加卡片，回车保存";
      addInput.setAttribute("aria-label", `在${column.name}添加卡片`);
      adder.append(addInput);
      adder.addEventListener("submit", (event) => {
        event.preventDefault();
        const title = addInput.value.trim();
        if (!title) return;
        const card = { id: uid(), title, note: "" };
        board.cards.push(card);
        column.cardIds.push(card.id);
        persist();
        render();
      });
      columnEl.append(head, cards, adder);
      boardEl.append(columnEl);
    }
    const visibleLabel = document.getElementById("card-visible");
    if (visibleLabel) visibleLabel.textContent = query ? `显示 ${shown} / ${total}` : `${total} 张卡片`;
    for (const [id, input] of names) {
      const next = boardEl.querySelector(`.col-name[data-id="${CSS.escape(id)}"]`);
      if (next) next.focus();
    }
  }

  function startCardDrag(event, cardId) {
    if (event.button !== 0) return;
    if (event.target.closest("input, button, textarea, a")) return;
    event.preventDefault();
    trackDrag(event, {
      kind: "card",
      id: cardId,
      origin: event.currentTarget,
    });
  }

  function startColumnDrag(event, columnId) {
    if (event.button !== 0) return;
    event.preventDefault();
    trackDrag(event, {
      kind: "column",
      id: columnId,
      origin: event.currentTarget.closest(".column"),
    });
  }

  function trackDrag(event, spec) {
    const startX = event.clientX;
    const startY = event.clientY;
    const pointerId = event.pointerId;
    let active = false;
    const move = (ev) => {
      if (ev.pointerId !== pointerId) return;
      const dx = ev.clientX - startX;
      const dy = ev.clientY - startY;
      if (!active && dx * dx + dy * dy < 16) return;
      if (!active) {
        active = true;
        beginDrag(spec, ev);
      }
      updateDrag(ev);
    };
    const up = (ev) => {
      document.removeEventListener("pointermove", move);
      document.removeEventListener("pointerup", up);
      document.removeEventListener("pointercancel", up);
      if (!active) {
        if (spec.kind === "card") openEditor(spec.id);
        return;
      }
      finishDrag(ev.clientX, ev.clientY);
    };
    document.addEventListener("pointermove", move);
    document.addEventListener("pointerup", up);
    document.addEventListener("pointercancel", up);
  }

  function beginDrag(spec, event) {
    drag = spec;
    boardEl.classList.add("is-dragging");
    spec.origin.classList.add("is-dragging");
    const ghost = el("div", "ghost");
    if (spec.kind === "card") {
      const card = cardById(board, spec.id);
      ghost.append(el("div", "card-title", card.title));
      if (card.note) ghost.append(el("div", "card-note", card.note));
    } else {
      const column = board.columns.find((item) => item.id === spec.id);
      ghost.append(el("div", "card-title", column.name));
    }
    document.body.append(ghost);
    drag.ghost = ghost;
    positionGhost(event);
    scrollDir = 0;
    scrollTimer = requestAnimationFrame(autoScroll);
  }

  function positionGhost(event) {
    drag.ghost.style.left = `${event.clientX + 8}px`;
    drag.ghost.style.top = `${event.clientY + 8}px`;
  }

  function updateDrag(event) {
    positionGhost(event);
    const rect = boardEl.getBoundingClientRect();
    if (event.clientX < rect.left + 48) scrollDir = -14;
    else if (event.clientX > rect.right - 48) scrollDir = 14;
    else scrollDir = 0;
    if (drag.kind === "card") placeCardHint(event.clientX, event.clientY);
    else placeColumnHint(event.clientX);
  }

  function cardsIn(columnEl) {
    // moveCard inserts among the cards that remain, so the dragged card does not count.
    return [...columnEl.querySelectorAll(".card")].filter((card) => card.dataset.id !== drag.id);
  }

  function indexAmong(nodes, axis, coord) {
    for (let i = 0; i < nodes.length; i += 1) {
      const box = nodes[i].getBoundingClientRect();
      const mid = axis === "x" ? box.left + box.width / 2 : box.top + box.height / 2;
      if (coord < mid) return i;
    }
    return nodes.length;
  }

  function placeCardHint(x, y) {
    const ghost = drag.ghost;
    ghost.hidden = true;
    const under = document.elementFromPoint(x, y);
    ghost.hidden = false;
    const columnEl = under && under.closest(".column");
    if (!columnEl) {
      removeHint();
      drag.columnId = null;
      return;
    }
    const cards = cardsIn(columnEl);
    const index = indexAmong(cards, "y", y);
    drag.columnId = columnEl.dataset.id;
    drag.index = index;
    let hint = document.getElementById("drop-hint");
    if (!hint) {
      hint = el("div", "drop-hint");
      hint.id = "drop-hint";
    }
    const before = cards[index];
    const list = columnEl.querySelector(".cards");
    if (before) before.before(hint);
    else list.append(hint);
  }

  function placeColumnHint(x) {
    const columns = [...boardEl.querySelectorAll(".column")].filter((column) => column.dataset.id !== drag.id);
    const index = indexAmong(columns, "x", x);
    drag.index = index;
    let hint = document.getElementById("col-hint");
    if (!hint) {
      hint = el("div", "col-hint");
      hint.id = "col-hint";
    }
    const before = columns[index];
    if (before) before.before(hint);
    else boardEl.append(hint);
  }

  function removeHint() {
    document.getElementById("drop-hint")?.remove();
    document.getElementById("col-hint")?.remove();
  }

  function autoScroll() {
    if (!drag) return;
    if (scrollDir) boardEl.scrollLeft += scrollDir;
    scrollTimer = requestAnimationFrame(autoScroll);
  }

  function finishDrag(x, y) {
    const spec = drag;
    if (spec.kind === "card") {
      placeCardHint(x, y);
      if (spec.columnId) {
        moveCard(board, spec.id, spec.columnId, spec.index);
        persist();
      }
    } else {
      placeColumnHint(x);
      moveColumn(board, spec.id, spec.index);
      persist();
    }
    endDrag();
    render();
  }

  function endDrag() {
    if (!drag) return;
    drag.origin.classList.remove("is-dragging");
    drag.ghost.remove();
    drag = null;
    scrollDir = 0;
    cancelAnimationFrame(scrollTimer);
    boardEl.classList.remove("is-dragging");
    removeHint();
  }

  function openEditor(cardId) {
    const card = cardById(board, cardId);
    if (!card) return;
    editing = cardId;
    editorTitle.textContent = "编辑卡片";
    editorError.textContent = "";
    titleInput.value = card.title;
    noteInput.value = card.note;
    columnInput.replaceChildren();
    for (const column of board.columns) {
      const option = el("option", "", column.name);
      option.value = column.id;
      if (column.cardIds.includes(cardId)) option.selected = true;
      columnInput.append(option);
    }
    removeBtn.hidden = false;
    editor.hidden = false;
    titleInput.focus();
  }

  function closeEditor() {
    editor.hidden = true;
    editing = null;
  }

  editorForm.addEventListener("submit", (event) => {
    event.preventDefault();
    const card = cardById(board, editing);
    const title = titleInput.value.trim();
    if (!card) return;
    if (!title) {
      editorError.textContent = "请填写标题";
      return;
    }
    card.title = title;
    card.note = noteInput.value.trim();
    const nextColumn = columnInput.value;
    const current = board.columns.find((column) => column.cardIds.includes(card.id));
    if (current && current.id !== nextColumn) moveCard(board, card.id, nextColumn, 9999);
    persist();
    closeEditor();
    render();
  });

  removeBtn.addEventListener("click", async () => {
    const card = cardById(board, editing);
    if (!card) return;
    const ok = await confirmBox(`删除「${card.title}」？`);
    if (!ok) return;
    board.cards = board.cards.filter((item) => item.id !== card.id);
    for (const column of board.columns) column.cardIds = column.cardIds.filter((id) => id !== card.id);
    persist();
    closeEditor();
    render();
  });

  async function onRemoveColumn(column) {
    if (board.columns.length === 1) {
      status.textContent = "至少留一列";
      return;
    }
    const extra = column.cardIds.length ? `其中 ${column.cardIds.length} 张卡片会挪到旁边的列。` : "这一列是空的。";
    const ok = await confirmBox(`删除「${column.name}」？${extra}`);
    if (!ok) return;
    const index = board.columns.findIndex((item) => item.id === column.id);
    const [removed] = board.columns.splice(index, 1);
    const target = board.columns[Math.max(0, index - 1)];
    target.cardIds.push(...removed.cardIds);
    persist();
    render();
  }

  document.getElementById("add-column").addEventListener("click", () => {
    const column = { id: uid(), name: "新列", cardIds: [] };
    board.columns.push(column);
    persist();
    render();
    const input = boardEl.querySelector(`.col-name[data-id="${CSS.escape(column.id)}"]`);
    if (input) {
      input.focus();
      input.select();
    }
  });

  document.getElementById("restore").addEventListener("click", async () => {
    const ok = await confirmBox("用示例看板替换当前内容？你加过的卡片会消失。", "恢复");
    if (!ok) return;
    board = seed();
    persist();
    render();
  });

  document.getElementById("editor-cancel").addEventListener("click", closeEditor);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && drag) {
      endDrag();
      render();
      return;
    }
    if (event.key !== "Escape") return;
    if (!confirmBack.hidden) {
      document.getElementById("confirm-cancel").click();
      return;
    }
    if (!editor.hidden) closeEditor();
  });

  const cardSearch = document.getElementById("card-q");
  if (cardSearch) cardSearch.addEventListener("input", () => render());
  render();
}
