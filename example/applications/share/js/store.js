/*
 * Browser sample data. The native (zui) build feeds the same fields from C++; see
 * cpp/session.cpp. Sizes are bytes; kind is one of dir / pdf / apk / vid / img / doc.
 */
const MB = 1024 * 1024;
const GB = 1024 * 1024 * 1024;

export function seed() {
  return {
    me: "阿竹",
    cwd: "~/Downloads",
    saveDir: "~/Downloads/小竹分享",
    peers: [
      { id: "p1", name: "客厅电脑", short: "厅", ip: "192.168.1.8" },
      { id: "p2", name: "平板", short: "平", ip: "192.168.1.23" },
    ],
    files: [
      { id: "f1", path: "~/Downloads/相机", name: "相机", kind: "dir", size: 0 },
      { id: "f2", path: "~/Downloads/截图", name: "截图", kind: "dir", size: 0 },
      { id: "f3", path: "~/Downloads/文档", name: "文档", kind: "dir", size: 0, on: true },
      { id: "f4", path: "~/Downloads/xz-share.apk", name: "xz-share.apk", kind: "apk", size: 14.3 * MB },
      { id: "f5", path: "~/Downloads/说明书.pdf", name: "说明书.pdf", kind: "pdf", size: 2.4 * MB },
      { id: "f6", path: "~/Downloads/假期视频.mp4", name: "假期视频.mp4", kind: "vid", size: 186 * MB, on: true },
      { id: "f7", path: "~/Downloads/封面.png", name: "封面.png", kind: "img", size: 1.2 * MB, on: true },
      { id: "f8", path: "~/Downloads/笔记.txt", name: "笔记.txt", kind: "doc", size: 12 * 1024 },
      { id: "f9", path: "~/Downloads/面试题答案.pdf", name: "面试题答案.pdf", kind: "pdf", size: 2.9 * MB },
      { id: "f10", path: "~/Downloads/Music", name: "Music", kind: "dir", size: 0 },
    ],
    xfer: {
      peerId: "p1",
      peerName: "客厅电脑",
      ip: "192.168.1.8",
      sending: true,
      total: 187.2 * MB,
      done: 78.6 * MB,
      speed: 42.1 * MB,
      file: "假期视频.mp4",
      doneFiles: ["文档/", "封面.png"],
      elapsed: 3,
    },
    incoming: {
      fromId: "p1",
      fromName: "客厅电脑",
      fromShort: "厅",
      nfiles: 12,
      nbytes: 1.24 * GB,
      dir: "Download / 小竹分享",
    },
  };
}
