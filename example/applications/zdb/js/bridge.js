/* Names the shared HTML calls. zui binds the same names in C++. The browser fills the ones it still owns. */

const NAMES = [
    "on_open",
    "on_sample",
    "on_export",
    "on_mode",
    "on_filter",
    "on_run",
    "on_sql_key",
    "on_clear",
    "on_detail_close",
    "on_query_detail_close",
];

export function installBridge() {
    for (const name of NAMES) {
        if (typeof window[name] !== "function") {
            window[name] = function () {};
        }
    }
}
