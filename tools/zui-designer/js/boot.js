/* Browser entry. Native zui ignores these scripts and binds the same handler names. */
(function () {
    [
        "on_down", "on_move", "on_up", "on_key", "on_field_key", "on_field_blur", "on_text", "on_title",
        "on_nudge", "on_swatch", "on_device", "on_undo", "on_redo", "on_sample",
        "on_clear", "on_export", "on_copy", "on_tab", "on_layer",
    ].forEach(function (name) {
        window[name] = function () {};
    });

    const storageKey = "zui-designer-v1";
    let doc = window.DesignerModel.createDoc();
    try {
        const saved = JSON.parse(localStorage.getItem(storageKey) || "null");
        if (saved) {
            doc = window.DesignerModel.fromPlain(saved);
        }
    } catch (error) {
        doc = window.DesignerModel.createDoc();
    }

    window.Designer = {
        doc: doc,
        drag: null,
        textUndo: false,
        titleUndo: false,
        status: function (text) {
            const el = document.getElementById("status");
            if (el) {
                el.textContent = text;
            }
        },
        save: function () {
            try {
                localStorage.setItem(storageKey, JSON.stringify(window.DesignerModel.toPlain(doc)));
            } catch (error) {
                /* private mode */
            }
        },
    };

    window.DesignerInteract.bind();
    window.DesignerView.render(doc);
    window.DesignerInspector.refresh(doc);
})();
