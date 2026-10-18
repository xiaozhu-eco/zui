export function emptyGrid() {
    return {
        columns: [],
        rows: [],
        total: 0,
        limit: 25,
        offset: 0,
        order: null,
        dir: "asc",
        error: "",
        browsable: false,
    };
}

export function initialState() {
    return {
        rev: 0,
        engine: null,
        engineError: "",
        db: null,
        fileName: "",
        version: "",
        bytes: 0,
        objects: [],
        catalogError: "",
        selected: null,
        structure: null,
        grid: emptyGrid(),
        mode: "data",
        queryResults: [],
        notice: "正在加载 SQLite…",
        noticeError: false,
    };
}
