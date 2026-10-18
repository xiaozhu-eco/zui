const DB_NAME = "xiaozhu-album";
const DB_VERSION = 1;

const BUILTINS = [
  { id: "ferry", name: "渡口" },
  { id: "light", name: "灯塔" },
  { id: "shop", name: "工坊" },
  { id: "loose", name: "未分类" },
];

const SEED = [
  { id: "p-ferry", albumId: "ferry", name: "渡口", file: "ferry.svg", note: "傍晚的栈桥，潮还没退干净。" },
  { id: "p-lighthouse", albumId: "light", name: "灯塔", file: "lighthouse.svg", note: "灯塔印刷社门口那盏，不是海上的。" },
  { id: "p-coast", albumId: "ferry", name: "海岸", file: "coast.svg", note: "渡口往东的一段，石头是赭色的。" },
  { id: "p-bamboo", albumId: "shop", name: "竹林", file: "bamboo.svg", note: "工坊后面的竹，纸样就晒在这里。" },
  { id: "p-night", albumId: "light", name: "夜航", file: "night.svg", note: "夜航表上画的那条线，灯还亮着。" },
  { id: "p-paper", albumId: "shop", name: "纸样", file: "paper.svg", note: "毛边留下，不裁。" },
];

export const MAX_BYTES = 12 * 1024 * 1024;

export function uid() {
  if (typeof crypto !== "undefined" && crypto.randomUUID) return crypto.randomUUID();
  return `${Date.now().toString(36)}${Math.random().toString(16).slice(2)}`;
}

export function rejection(file) {
  const name = file.name || "未命名";
  const type = file.type || "";
  const image = type.startsWith("image/") || /\.(avif|bmp|gif|jpe?g|png|svg|webp|ico)$/i.test(name);
  if (!image) return `${name} 不是图片`;
  if (file.size > MAX_BYTES) return `${name} 超过 12MB`;
  return "";
}

function blobType(file) {
  if (file.type && file.type.startsWith("image/")) return file.type;
  if (/\.svg$/i.test(file.name || "")) return "image/svg+xml";
  return "application/octet-stream";
}

function stripExt(name) {
  const base = (name || "未命名").replace(/^.*[\\/]/, "");
  const stripped = base.replace(/\.[^.]+$/, "");
  return stripped || base;
}

let opening = null;

function openDb() {
  if (opening) return opening;
  opening = new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains("albums")) db.createObjectStore("albums", { keyPath: "id" });
      if (!db.objectStoreNames.contains("photos")) {
        const photos = db.createObjectStore("photos", { keyPath: "id" });
        photos.createIndex("albumId", "albumId", { unique: false });
      }
      if (!db.objectStoreNames.contains("meta")) db.createObjectStore("meta", { keyPath: "key" });
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => {
      opening = null;
      reject(req.error);
    };
  });
  return opening;
}

function run(storeNames, mode, work) {
  return openDb().then((db) => new Promise((resolve, reject) => {
    const tx = db.transaction(storeNames, mode);
    tx.oncomplete = () => resolve();
    tx.onabort = () => reject(tx.error || new Error("aborted"));
    tx.onerror = () => reject(tx.error);
    work(tx);
  }));
}

function getAll(storeName) {
  return openDb().then((db) => new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, "readonly");
    const req = tx.objectStore(storeName).getAll();
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  }));
}

function getMeta() {
  return openDb().then((db) => new Promise((resolve, reject) => {
    const tx = db.transaction("meta", "readonly");
    const req = tx.objectStore("meta").get("seeded");
    req.onsuccess = () => resolve(req.result || null);
    req.onerror = () => reject(req.error);
  }));
}

async function fetchSeedPhotos() {
  const start = Date.parse("2026-09-28T18:00:00");
  const photos = [];
  for (let i = 0; i < SEED.length; i += 1) {
    const item = SEED[i];
    const url = new URL(`../pictures/${item.file}`, import.meta.url);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`打不开 ${item.file}`);
    const bytes = await res.arrayBuffer();
    photos.push({
      id: item.id,
      albumId: item.albumId,
      name: item.name,
      note: item.note,
      added: start - i * 60 * 60 * 1000,
      blob: new Blob([bytes], { type: "image/svg+xml" }),
    });
  }
  return photos;
}

async function writeSeed() {
  const photos = await fetchSeedPhotos();
  await run(["albums", "photos", "meta"], "readwrite", (tx) => {
    for (const album of BUILTINS) tx.objectStore("albums").put(album);
    for (const photo of photos) tx.objectStore("photos").put(photo);
    tx.objectStore("meta").put({ key: "seeded", value: 1 });
  });
}

export function sortAlbums(albums) {
  const rank = (id) => {
    if (id === "loose") return 100;
    const index = BUILTINS.findIndex((album) => album.id === id);
    return index >= 0 ? index : 50;
  };
  return [...albums].sort((a, b) => {
    const delta = rank(a.id) - rank(b.id);
    if (delta) return delta;
    return a.name.localeCompare(b.name, "zh");
  });
}

function sortPhotos(photos) {
  return [...photos].sort((a, b) => b.added - a.added || a.name.localeCompare(b.name, "zh"));
}

async function readLibrary() {
  const [albums, photos] = await Promise.all([getAll("albums"), getAll("photos")]);
  return { albums: sortAlbums(albums), photos: sortPhotos(photos) };
}

export async function loadLibrary() {
  const seeded = await getMeta();
  if (!seeded) await writeSeed();
  return readLibrary();
}

export async function restoreLibrary() {
  await run(["albums", "photos", "meta"], "readwrite", (tx) => {
    tx.objectStore("albums").clear();
    tx.objectStore("photos").clear();
    tx.objectStore("meta").clear();
  });
  await writeSeed();
  return readLibrary();
}

export function saveAlbum(album) {
  return run(["albums"], "readwrite", (tx) => {
    tx.objectStore("albums").put(album);
  });
}

export function savePhoto(photo) {
  return run(["photos"], "readwrite", (tx) => {
    tx.objectStore("photos").put(photo);
  });
}

export function savePhotos(photos) {
  return run(["photos"], "readwrite", (tx) => {
    for (const photo of photos) tx.objectStore("photos").put(photo);
  });
}

export function removePhoto(id) {
  return run(["photos"], "readwrite", (tx) => {
    tx.objectStore("photos").delete(id);
  });
}

export function removeAlbum(id) {
  if (id === "loose") return Promise.reject(new Error("keep"));
  return openDb().then((db) => new Promise((resolve, reject) => {
    const tx = db.transaction(["albums", "photos"], "readwrite");
    const photos = tx.objectStore("photos");
    const req = photos.index("albumId").getAll(id);
    req.onsuccess = () => {
      for (const photo of req.result) {
        photo.albumId = "loose";
        photos.put(photo);
      }
      tx.objectStore("albums").delete(id);
    };
    tx.oncomplete = () => resolve();
    tx.onabort = () => reject(tx.error || new Error("aborted"));
    tx.onerror = () => reject(tx.error);
  }));
}

export async function addFiles(fileList, albumId) {
  const accepted = [];
  const skipped = [];
  let stamp = Date.now() + fileList.length * 1000;
  for (const file of fileList) {
    const reason = rejection(file);
    if (reason) {
      skipped.push(reason);
      continue;
    }
    stamp -= 1000;
    const bytes = await file.arrayBuffer();
    accepted.push({
      id: uid(),
      albumId,
      name: stripExt(file.name),
      note: "",
      added: stamp,
      blob: new Blob([bytes], { type: blobType(file) }),
    });
  }
  if (accepted.length) await savePhotos(accepted);
  return { accepted, skipped };
}
