const KEY = "xiaozhu-kanban";

export function uid() {
  if (typeof crypto !== "undefined" && crypto.randomUUID) return crypto.randomUUID();
  return `${Date.now().toString(36)}${Math.random().toString(16).slice(2)}`;
}

export function seed() {
  return {
    columns: [
      { id: "todo", name: "待办", cardIds: ["c1", "c2", "c3"] },
      { id: "doing", name: "进行中", cardIds: ["c4", "c5"] },
      { id: "review", name: "待校对", cardIds: ["c6", "c7"] },
      { id: "done", name: "完成", cardIds: ["c8"] },
      { id: "later", name: "搁置", cardIds: ["c9"] },
    ],
    cards: [
      { id: "c1", title: "校对第三章", note: "第二节标题和那张被拆开的表。明天上午和林晚对目录。" },
      { id: "c2", title: "人名索引留白", note: "等周宁补注，先不要上墨。" },
      { id: "c3", title: "还夜航表", note: "灯塔印刷有人在等，周三下午之前。" },
      { id: "c4", title: "灯塔摘录封面", note: "色号用上次的赭石，别换新样。" },
      { id: "c5", title: "竹纸样", note: "毛边留下，不裁。" },
      { id: "c6", title: "渡口手记前言", note: "改过一轮，等林晚用铅笔批。" },
      { id: "c7", title: "海岸植物续借", note: "书局说这本可以续，夜航表不行。" },
      { id: "c8", title: "旧书店目录归档", note: "何清已经对过册数。" },
      { id: "c9", title: "潮汐口诀再版", note: "这季先不动。" },
    ],
  };
}

export function load() {
  try {
    const data = JSON.parse(localStorage.getItem(KEY) || "null");
    if (!data || !Array.isArray(data.columns) || !Array.isArray(data.cards)) return null;
    if (!data.columns.length) return null;
    return data;
  } catch {
    return null;
  }
}

export function save(board) {
  localStorage.setItem(KEY, JSON.stringify(board));
}

export function cardById(board, id) {
  return board.cards.find((card) => card.id === id) || null;
}

/* index is the slot among the cards that remain after cardId is removed. */
export function moveCard(board, cardId, toColumnId, index) {
  for (const column of board.columns) {
    column.cardIds = column.cardIds.filter((id) => id !== cardId);
  }
  const column = board.columns.find((item) => item.id === toColumnId);
  if (!column) return;
  const next = Math.max(0, Math.min(index, column.cardIds.length));
  column.cardIds.splice(next, 0, cardId);
}

export function moveColumn(board, columnId, index) {
  const from = board.columns.findIndex((column) => column.id === columnId);
  if (from < 0) return;
  const [column] = board.columns.splice(from, 1);
  const next = Math.max(0, Math.min(index, board.columns.length));
  board.columns.splice(next, 0, column);
}
