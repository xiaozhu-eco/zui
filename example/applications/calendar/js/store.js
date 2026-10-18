import { addDays, isoDate } from "./dates.js";

const KEY = "xiaozhu-calendar";

export const COLORS = [
  ["green", "竹"],
  ["clay", "赭"],
  ["sea", "蓝"],
  ["gold", "金"],
];

export function uid() {
  if (typeof crypto !== "undefined" && crypto.randomUUID) return crypto.randomUUID();
  return `${Date.now().toString(36)}${Math.random().toString(16).slice(2)}`;
}

export function seed() {
  const today = isoDate(new Date());
  return [
    { id: "e1", title: "校对第三章", date: today, start: "10:00", end: "11:30", place: "渡口书局", color: "green" },
    { id: "e2", title: "和林晚对目录", date: today, start: "10:30", end: "12:00", place: "二楼", color: "clay" },
    { id: "e3", title: "还书", date: addDays(today, 1), start: "15:00", end: "15:30", place: "灯塔印刷", color: "sea" },
    { id: "e4", title: "看纸样", date: addDays(today, 2), start: "09:00", end: "10:00", place: "竹纸工坊", color: "gold" },
    { id: "e5", title: "夜校", date: addDays(today, -1), start: "19:00", end: "20:30", place: "", color: "green" },
  ];
}

export function load() {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    const data = JSON.parse(raw);
    if (!Array.isArray(data)) return null;
    return data.filter((item) => item && item.id && item.date && item.start && item.end && item.title);
  } catch {
    return null;
  }
}

export function save(events) {
  localStorage.setItem(KEY, JSON.stringify(events));
}
