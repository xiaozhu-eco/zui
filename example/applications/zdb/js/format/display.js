/* Turn SQLite values into short grid text and a longer inspector dump. */

const TEXT_CAP = 240;
const TITLE_CAP = 2000;
const INSPECT_CAP = 100000;
const HEX_CAP = 4096;

export function formatCount(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return "";
    return n.toLocaleString("zh-CN");
}

export function formatSize(bytes) {
    const n = Number(bytes);
    if (!Number.isFinite(n) || n <= 0) return "";
    if (n < 1024) return `${n} B`;
    if (n < 1048576) return `${(n / 1024).toFixed(1)} KB`;
    return `${(n / 1048576).toFixed(1)} MB`;
}

export function formatMs(ms) {
    if (!Number.isFinite(ms)) return "";
    if (ms < 10) return `${ms.toFixed(1)} ms`;
    if (ms < 1000) return `${Math.round(ms)} ms`;
    return `${(ms / 1000).toFixed(2)} s`;
}

export function formatCell(value) {
    if (value === null) return { text: "NULL", title: "", kind: "null" };
    if (value instanceof Uint8Array) {
        const size = formatSize(value.byteLength) || "0 B";
        return { text: `BLOB ${size}`, title: `${value.byteLength} 字节`, kind: "blob" };
    }
    if (typeof value === "bigint" || typeof value === "number") {
        return { text: String(value), title: "", kind: "number" };
    }
    const full = String(value);
    if (full === "") return { text: "", title: "空字符串", kind: "empty" };
    const text = full.length > TEXT_CAP ? `${full.slice(0, TEXT_CAP)}…` : full;
    const title = full.length > text.length
        ? (full.length > TITLE_CAP ? `${full.slice(0, TITLE_CAP)}…` : full)
        : "";
    return { text, title, kind: "text" };
}

export function inspect(value) {
    if (value === null) return "NULL";
    if (typeof value === "bigint" || typeof value === "number") return String(value);
    if (value instanceof Uint8Array) return hexDump(value);
    const text = String(value);
    if (text.length <= INSPECT_CAP) return text;
    return `${text.slice(0, INSPECT_CAP)}\n…（其余 ${formatCount(text.length - INSPECT_CAP)} 个字符已省略）`;
}

function hexDump(bytes) {
    const n = Math.min(bytes.length, HEX_CAP);
    const lines = [];
    for (let offset = 0; offset < n; offset += 16) {
        const chunk = bytes.subarray(offset, Math.min(offset + 16, n));
        const hex = [...chunk].map((byte) => byte.toString(16).padStart(2, "0")).join(" ");
        lines.push(`${offset.toString(16).padStart(4, "0")}  ${hex}`);
    }
    if (bytes.length > HEX_CAP) lines.push(`…（共 ${formatCount(bytes.length)} 字节，只显示前 ${formatCount(HEX_CAP)} 个）`);
    else lines.push(`（${formatCount(bytes.length)} 字节）`);
    return lines.join("\n");
}
