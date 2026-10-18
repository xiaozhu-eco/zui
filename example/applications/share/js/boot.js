/*
 * Browser entry: mount the sample data, then add a demo bar so every screen is one click
 * away. The bar is injected by boot.js only; zui loads no script and has no such bar.
 */
import "./bridge.js";
import { mount, showScreen, openIncoming } from "./view.js";

mount();

const DEMO = [
  ["起名", () => showScreen("name")],
  ["浏览", () => showScreen("browse")],
  ["选设备", () => showScreen("pick")],
  ["等待", () => showScreen("wait")],
  ["传输", () => showScreen("transfer")],
  ["完成", () => showScreen("done")],
  ["拒绝", () => showScreen("fail")],
  ["收到分享", () => openIncoming()],
  ["设置", () => showScreen("settings")],
];

const bar = document.createElement("div");
bar.className = "demo-bar";
const title = document.createElement("span");
title.className = "demo-title";
title.textContent = "演示";
bar.append(title);
for (const [label, run] of DEMO) {
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = label;
  button.addEventListener("click", run);
  bar.append(button);
}
document.body.append(bar);
document.body.classList.add("has-demo");
