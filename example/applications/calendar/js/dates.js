export const HOUR_PX = 44;

export function isoDate(date) {
  const y = date.getFullYear();
  const m = String(date.getMonth() + 1).padStart(2, "0");
  const d = String(date.getDate()).padStart(2, "0");
  return `${y}-${m}-${d}`;
}

export function parseIso(iso) {
  const [y, m, d] = iso.split("-").map(Number);
  return new Date(y, m - 1, d);
}

export function addDays(iso, days) {
  const date = parseIso(iso);
  date.setDate(date.getDate() + days);
  return isoDate(date);
}

export function startOfWeek(iso) {
  const date = parseIso(iso);
  const lead = (date.getDay() + 6) % 7;
  date.setDate(date.getDate() - lead);
  return isoDate(date);
}

export function monthCells(year, monthIndex) {
  const first = new Date(year, monthIndex, 1);
  const lead = (first.getDay() + 6) % 7;
  const start = new Date(year, monthIndex, 1 - lead);
  const cells = [];
  for (let i = 0; i < 42; i += 1) {
    const date = new Date(start);
    date.setDate(start.getDate() + i);
    cells.push(isoDate(date));
  }
  return cells;
}

export function minutes(hhmm) {
  if (!hhmm || !hhmm.includes(":")) return null;
  const [h, m] = hhmm.split(":").map(Number);
  if (!Number.isFinite(h) || !Number.isFinite(m)) return null;
  return h * 60 + m;
}

export function weekday(iso) {
  return `周${"一二三四五六日"[(parseIso(iso).getDay() + 6) % 7]}`;
}

export function longDate(iso) {
  const date = parseIso(iso);
  return `${date.getMonth() + 1}月${date.getDate()}日 ${weekday(iso)}`;
}

export function monthTitle(year, monthIndex) {
  return `${year}年${monthIndex + 1}月`;
}

export function weekTitle(startIso) {
  const end = parseIso(addDays(startIso, 6));
  const start = parseIso(startIso);
  return `${start.getMonth() + 1}月${start.getDate()}日 – ${end.getMonth() + 1}月${end.getDate()}日`;
}

/* Greedy columns. Width is the cluster's column count, not the day's. */
export function layoutDay(events) {
  const items = [];
  for (const ev of events) {
    const start = minutes(ev.start);
    const end = minutes(ev.end);
    if (start == null || end == null || end <= start) continue;
    items.push({ ev, start, end });
  }
  items.sort((a, b) => a.start - b.start || a.end - b.end);
  const colEnds = [];
  let cluster = -1;
  let clusterEnd = -1;
  for (const item of items) {
    if (cluster < 0 || item.start >= clusterEnd) {
      cluster += 1;
      clusterEnd = item.end;
    } else {
      clusterEnd = Math.max(clusterEnd, item.end);
    }
    item.cluster = cluster;
    let col = colEnds.findIndex((end) => end <= item.start);
    if (col < 0) {
      col = colEnds.length;
      colEnds.push(item.end);
    } else {
      colEnds[col] = item.end;
    }
    item.col = col;
  }
  const widths = new Map();
  for (const item of items) {
    widths.set(item.cluster, Math.max(widths.get(item.cluster) || 0, item.col + 1));
  }
  return items.map((item) => {
    const cols = widths.get(item.cluster);
    return {
      ev: item.ev,
      top: (item.start / 60) * HOUR_PX,
      height: Math.max(22, ((item.end - item.start) / 60) * HOUR_PX),
      left: (item.col / cols) * 100,
      width: (1 / cols) * 100,
    };
  });
}
