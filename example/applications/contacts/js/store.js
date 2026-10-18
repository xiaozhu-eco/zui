const KEY = "xiaozhu-contacts";

export const GROUPS = [
  ["work", "同事"],
  ["family", "家人"],
  ["friend", "朋友"],
  ["other", "其他"],
];

export function groupLabel(id) {
  const found = GROUPS.find(([key]) => key === id);
  return found ? found[1] : "其他";
}

export function uid() {
  if (typeof crypto !== "undefined" && crypto.randomUUID) return crypto.randomUUID();
  return `${Date.now().toString(36)}${Math.random().toString(16).slice(2)}`;
}

export function seed() {
  return [
    { id: "p1", name: "林晚", org: "渡口书局", title: "编辑", phone: "13800001001", email: "linwan@example.com", city: "杭州", group: "work", note: "目录和校样找她。", star: true },
    { id: "p2", name: "陈屿", org: "竹纸工坊", title: "纸样", phone: "13800001002", email: "chenyu@example.com", city: "富阳", group: "work", note: "", star: false },
    { id: "p3", name: "赵岑", org: "灯塔印刷", title: "排版", phone: "13800001003", email: "zhaocen@example.com", city: "宁波", group: "work", note: "夜航表在他那儿。", star: false },
    { id: "p4", name: "周宁", org: "", title: "作者", phone: "13800001004", email: "zhouning@example.com", city: "绍兴", group: "friend", note: "渡口手记、灯塔摘录。", star: true },
    { id: "p5", name: "何清", org: "旧书店", title: "店主", phone: "13800001005", email: "heqing@example.com", city: "杭州", group: "friend", note: "", star: false },
    { id: "p6", name: "沈母", org: "", title: "", phone: "13800001006", email: "", city: "嘉兴", group: "family", note: "周末通常在家。", star: true },
    { id: "p7", name: "阿禾", org: "", title: "", phone: "13800001007", email: "", city: "杭州", group: "family", note: "", star: false },
    { id: "p8", name: "邮差老吴", org: "渡口邮局", title: "", phone: "13800001008", email: "", city: "杭州", group: "other", note: "包裹放传达室。", star: false },
  ];
}

export function load() {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    const data = JSON.parse(raw);
    if (!Array.isArray(data)) return null;
    return data.filter((item) => item && typeof item.name === "string" && item.id);
  } catch {
    return null;
  }
}

export function save(people) {
  localStorage.setItem(KEY, JSON.stringify(people));
}
