const KEY = "xiaozhu-chat";

export function uid() {
  if (typeof crypto !== "undefined" && crypto.randomUUID) return crypto.randomUUID();
  return `${Date.now().toString(36)}${Math.random().toString(16).slice(2)}`;
}

function ago(hours) {
  return Date.now() - hours * 3600 * 1000;
}

export function seed() {
  const lin = "lin";
  const shop = "shop";
  const chen = "chen";
  const print = "print";
  return {
    convos: [
      { id: lin, name: "林晚", created: ago(40) },
      { id: shop, name: "渡口书局", created: ago(20) },
      { id: chen, name: "陈屿", created: ago(10) },
      { id: print, name: "灯塔印刷", created: ago(6) },
    ],
    messages: [
      { id: "l1", convId: lin, mine: false, text: "第三章的校样我放在传达室了。", ts: ago(30) },
      { id: "l2", convId: lin, mine: true, text: "收到。目录那一页我今晚看。", ts: ago(29) },
      { id: "l3", convId: lin, mine: false, text: "第二节标题和正文对不齐，你留意一下。", ts: ago(26) },
      { id: "l4", convId: lin, mine: true, text: "是「潮汐」那节吗？", ts: ago(25) },
      { id: "l5", convId: lin, mine: false, text: "对。还有一张表被拆到了下一页。", ts: ago(24) },
      { id: "l6", convId: lin, mine: true, text: "表我拉回来。人名索引先不动。", ts: ago(22) },
      { id: "l7", convId: lin, mine: false, text: "索引可以等纸样。周宁说他会补一条注。", ts: ago(18) },
      { id: "l8", convId: lin, mine: true, text: "那我在页边留白。", ts: ago(17) },
      { id: "l9", convId: lin, mine: false, text: "明天上午十点，二楼对一下目录。", ts: ago(5) },
      { id: "l10", convId: lin, mine: true, text: "我带着校样过去。", ts: ago(4.5) },
      { id: "l11", convId: lin, mine: false, text: "如果迟到就先看渡口手记的前言。", ts: ago(3) },
      { id: "l12", convId: lin, mine: true, text: "前言我已经改过一轮。", ts: ago(2.5) },
      { id: "l13", convId: lin, mine: false, text: "好。改动用铅笔，别直接上墨。", ts: ago(2) },
      { id: "l14", convId: lin, mine: true, text: "知道。", ts: ago(1.2) },
      { id: "s1", convId: shop, mine: false, text: "这周的还书日改到周三下午。", ts: ago(16) },
      { id: "s2", convId: shop, mine: true, text: "夜航表和海岸植物都在我这儿。", ts: ago(15) },
      { id: "s3", convId: shop, mine: false, text: "海岸植物可以续借，夜航表有人等。", ts: ago(14) },
      { id: "c1", convId: chen, mine: false, text: "竹纸样打出来了，边有一点毛。", ts: ago(8) },
      { id: "c2", convId: chen, mine: true, text: "毛边留下吧，别裁。", ts: ago(7) },
      { id: "p1", convId: print, mine: false, text: "灯塔摘录的封面色号我发你了吗？", ts: ago(6) },
      { id: "p2", convId: print, mine: true, text: "还没有。用上次那张赭石就行。", ts: ago(5.5) },
    ],
  };
}

export function load() {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    const data = JSON.parse(raw);
    if (!data || !Array.isArray(data.convos) || !Array.isArray(data.messages)) return null;
    return data;
  } catch {
    return null;
  }
}

export function save(data) {
  localStorage.setItem(KEY, JSON.stringify(data));
}
