/*
  zmd 自己那一层的行为。原型脚本负责它自己的那一屏（悬停面板、格式条、命令面板、
  设置弹层、编辑规则），这里只接原型没有的那几件：视图切换、查找、对话框、文件读写。
  两边都走同一套委托语义：读 data-act，调同名方法。
*/

// Native binds these names from the on* attributes. In the browser they must exist
// so the attribute handler does not throw; the prototype scripts still do the work.
[
  "on_accel", "on_source", "on_find_input", "on_edit_find", "on_find_prev", "on_find_next",
  "on_find_close", "on_settings_open", "on_settings_close", "on_format_h1", "on_format_h2",
  "on_format_quote", "on_format_ul", "on_format_ol", "on_format_table", "on_format_image",
  "on_format_hr", "on_format_bold", "on_format_italic", "on_format_code", "on_format_link",
  "on_format_apply_link", "on_format_apply_image", "on_format_pick_image", "on_view_live",
  "on_view_source", "on_view_split", "on_view_preview", "on_view_focus", "on_focus_sw",
  "on_view_font_sans", "on_view_font_serif", "on_view_theme_light", "on_view_theme_dark",
  "on_view_theme_system", "on_size_slide", "on_width_slide", "on_line_slide", "on_set_type",
  "on_set_look", "on_set_file", "on_set_export", "on_set_keys", "on_ws_edit", "on_assets_edit",
  "on_autosave_toggle", "on_new_in_root", "on_new_in_here", "on_file_export_html", "on_file_new",
  "on_folder_new", "on_file_pick", "on_pin_files", "on_pin_outline", "on_scroll_top", "on_heading",
  "on_pal_open", "on_pal_close", "on_pal_input", "on_pal_pick", "on_dialog_cancel",
  "on_dialog_discard", "on_dialog_save", "on_dialog_close", "on_beforeunload", "on_write_click",
  "on_drop", "on_accent_pick", "on_radius_slide", "on_folder_up", "on_doc_scroll",
  "on_source_focus", "on_source_blur", "on_visibility"
].forEach(function (name) {
  if (typeof window[name] !== "function") {
    window[name] = function () {};
  }
});

(function () {
  const mock = document.getElementById("md-editor-mock");
  if (!mock) {
    return;
  }

  const views = ["view-live", "view-source", "view-split", "view-preview"];
  const state = { view: "view-live", query: "", cursor: 0, hits: [], matches: 0, dirty: false };

  const doc = () => document.getElementById("mdc-a-doc");
  const src = () => document.getElementById("mdc-a-source");
  const prev = () => document.getElementById("mdc-a-preview");
  const findBar = () => document.getElementById("zmd-find");
  const findInput = () => document.getElementById("zmd-find-q");
  const findCount = () => document.getElementById("zmd-find-count");
  const sheet = (id) => document.getElementById(id);

  function text() {
    const node = src();
    return node ? node.value : doc().innerText;
  }

  // 预览面直接镜像写作面渲染出来的 DOM：浏览器这一层不重复实现排版，
  // 真正的渲染器是原型脚本那一层（原生侧是 cpp/markdown.cpp）。
  function render() {
    if (prev() && doc()) {
      prev().innerHTML = doc().innerHTML;
    }
  }

  function setView(view) {
    state.view = view;
    views.forEach((name) => mock.classList.toggle(name, name === view));
    if (view !== "view-live") {
      render();
    }
    const node = src();
    if (node && (view === "view-source" || view === "view-split")) {
      node.focus();
    }
  }

  // 与原型 markEditing() 同一套：存盘点切 data-state，窗口挂上 is-typing 罩层，1.2s 后收回。
  // 原型只在写作面上打字时才跑它；源码面是 zmd 加的，就在这边照抄一遍（原生侧是 Chrome::pulse()）。
  let veilTimer = 0;
  function setDirty(on) {
    state.dirty = on;
    const dot = document.getElementById("mdc-a-save");
    if (dot) {
      dot.setAttribute("data-state", on ? "typing" : "saved");
      dot.setAttribute("aria-label", on ? "正在保存" : "已保存");
    }
    const win = document.getElementById("mdc-a");
    if (win) {
      win.classList.toggle("is-typing", on);
    }
    if (veilTimer) {
      window.clearTimeout(veilTimer);
      veilTimer = 0;
    }
    if (on) {
      veilTimer = window.setTimeout(function () {
        setDirty(false);
      }, 1200);
    }
  }

  function openSheet(id) {
    const node = sheet(id);
    if (node) {
      node.hidden = false;
      node.classList.add("zmd-open");
    }
  }

  function closeSheets() {
    ["zmd-dlg-unsaved", "zmd-dlg-link", "zmd-dlg-image"].forEach((id) => {
      const node = sheet(id);
      if (node) {
        node.classList.remove("zmd-open");
        node.hidden = true;
      }
    });
  }

  function insert(text_) {
    const node = src();
    if (!node) {
      return;
    }
    const start = node.selectionStart || 0;
    const end = node.selectionEnd || 0;
    node.value = node.value.slice(0, start) + text_ + node.value.slice(end);
    node.focus();
    node.setSelectionRange(start + text_.length, start + text_.length);
    setDirty(true);
    render();
  }

  // 命中位置一次扫完存进 hits，游标是 hits 的下标——计数器显示的是名次（3 / 7），
  // 不是字节偏移；两者差得很远，拿偏移当名次会印出一个错的数字。原生侧同构（Find::hits_）。
  function recount() {
    const haystack = text();
    state.hits = [];
    if (state.query) {
      let at = haystack.indexOf(state.query);
      while (at !== -1) {
        state.hits.push(at);
        at = haystack.indexOf(state.query, at + state.query.length);
      }
    }
    state.matches = state.hits.length;
    if (state.cursor >= state.matches) {
      state.cursor = 0;
    }
    if (findCount()) {
      findCount().textContent = state.matches ? state.cursor + 1 + " / " + state.matches : "0 处";
    }
  }

  function selectCursor() {
    if (!state.matches) {
      return;
    }
    const at = state.hits[state.cursor];
    const node = src();
    if (node) {
      setView("view-source");
      node.focus();
      node.setSelectionRange(at, at + state.query.length);
    }
    recount();
  }

  function step(delta) {
    if (!state.matches) {
      return;
    }
    state.cursor = (state.cursor + delta + state.matches) % state.matches;
    selectCursor();
  }

  const acts = {
    "view-write": () => setView("view-live"),
    "view-source": () => setView("view-source"),
    "view-split": () => setView("view-split"),
    "view-preview": () => setView("view-preview"),
    "find-open": () => {
      if (findBar()) {
        findBar().hidden = false;
        findInput().focus();
      }
    },
    "find-next": () => step(1),
    "find-prev": () => step(-1),
    "find-close": () => {
      if (findBar()) {
        findBar().hidden = true;
      }
    },
    "dlg-cancel": () => closeSheets(),
    "dlg-discard": () => {
      closeSheets();
      setDirty(false);
    },
    "dlg-save": () => {
      closeSheets();
      save();
    },
    "link-apply": () => {
      const label = document.getElementById("zmd-link-text").value;
      const url = document.getElementById("zmd-link-url").value;
      closeSheets();
      if (url) {
        insert("[" + (label || url) + "](" + url + ")");
      }
    },
    "image-pick": () => document.getElementById("zmd-image-file").click(),
    "image-apply": () => {
      const alt = document.getElementById("zmd-image-alt").value;
      const url = document.getElementById("zmd-image-url").value;
      closeSheets();
      if (url) {
        insert("![" + alt + "](" + url + ")");
      }
    }
  };

  function save() {
    const blob = new Blob([text()], { type: "text/markdown;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "未命名.md";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    setDirty(false);
  }

  document.addEventListener("click", (event) => {
    const node = event.target.closest ? event.target.closest("[data-act]") : null;
    if (!node) {
      return;
    }
    const act = node.getAttribute("data-act");
    const run = acts[act];
    if (run) {
      event.preventDefault();
      run();
    }
  });

  document.addEventListener("input", (event) => {
    if (event.target === findInput()) {
      state.query = findInput().value;
      state.cursor = 0;
      recount();
      if (state.matches) {
        selectCursor();
      }
      return;
    }
    if (event.target === src()) {
      setDirty(true);
      render();
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") {
      return;
    }
    if (findBar() && !findBar().hidden) {
      findBar().hidden = true;
    } else {
      closeSheets();
    }
  });

  const openFile = document.getElementById("zmd-open-file");
  if (openFile) {
    openFile.addEventListener("change", () => {
      const file = openFile.files && openFile.files[0];
      if (!file) {
        return;
      }
      file.text().then((body) => {
        if (src()) {
          src().value = body;
        }
        render();
        setDirty(false);
        setView("view-live");
      });
    });
  }

  const imageFile = document.getElementById("zmd-image-file");
  if (imageFile) {
    imageFile.addEventListener("change", () => {
      const file = imageFile.files && imageFile.files[0];
      if (file) {
        document.getElementById("zmd-image-url").value = "assets/" + file.name;
        document.getElementById("zmd-image-alt").value = file.name.replace(/\.[^.]+$/, "");
      }
    });
  }

  window.addEventListener("beforeunload", (event) => {
    if (!state.dirty) {
      return;
    }
    event.preventDefault();
    event.returnValue = "";
    openSheet("zmd-dlg-unsaved");
  });

  render();
  window.zmdLayer = { setView: setView, state: state };
})();

