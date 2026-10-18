const KEY = "xiaozhu-writer-root";

function empty() {
  return { seq: 0, active: "", read: false, docs: [] };
}

export function today() {
  const d = new Date();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  return `${d.getFullYear()}-${m}-${day}`;
}

export function load(root) {
  const raw = localStorage.getItem(bucket(root));
  if (!raw) return empty();
  try {
    const data = JSON.parse(raw);
    if (!data || !Array.isArray(data.docs)) return empty();
    return data;
  } catch (err) {
    return empty();
  }
}

export function save(root, data) {
  localStorage.setItem(bucket(root), JSON.stringify(data));
  localStorage.setItem(KEY, root || "");
}

export function rememberedRoot() {
  return localStorage.getItem(KEY) || "browser";
}

function bucket(root) {
  return "xiaozhu-writer:" + (root || "browser");
}

export function freshId(data) {
  data.seq += 1;
  return "d" + data.seq;
}

export function find(data, id) {
  return data.docs.find((doc) => doc.id === id) || null;
}

export function parentForNew(data) {
  const doc = find(data, data.active);
  if (!doc) return "";
  return doc.folder ? doc.id : doc.parent || "";
}

export function flatten(data, parent, depth, out) {
  for (const doc of data.docs) {
    if ((doc.parent || "") !== parent) continue;
    out.push({ doc, depth });
    flatten(data, doc.id, depth + 1, out);
  }
}

export function removeTree(data, id) {
  const drop = [];
  const walk = (cur) => {
    drop.push(cur);
    for (const doc of data.docs) {
      if (doc.parent === cur) walk(doc.id);
    }
  };
  walk(id);
  data.docs = data.docs.filter((doc) => drop.indexOf(doc.id) < 0);
  if (drop.indexOf(data.active) >= 0) {
    const piece = data.docs.find((doc) => !doc.folder);
    data.active = piece ? piece.id : "";
  }
}

export function snapshot(doc) {
  if (!doc || doc.folder) return;
  if (doc.saved !== undefined && doc.saved !== doc.body && doc.saved) {
    doc.history = doc.history || [];
    const id = "h" + (doc.history.length + 1);
    doc.history.push({ id, label: (doc.edited || "") + " " + id, text: doc.saved });
  }
  doc.saved = doc.body || "";
  doc.edited = today();
}
