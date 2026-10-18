//! Windowed demo. Run it from the directory that contains `c_hello.html`
//! (the CMake build directory) or pass the HTML path as the first argument.
//! `cargo test` does not run this.

fn main() {
    let page = std::env::args().nth(1).unwrap_or_else(|| "c_hello.html".to_string());
    let mut app = zui::App::new().unwrap_or_else(|| {
        eprintln!("zui_app_create failed");
        std::process::exit(1);
    });
    if !app.load(&page) {
        eprintln!("failed to load {page}");
        std::process::exit(1);
    }
    let handle = app.handle();
    app.bind("on_save", move |event| {
        if event.event_type() != "click" {
            return;
        }
        handle.set("status", "text", "Saved");
        handle.add_class("title", "error");
    });
    app.set("status", "text", "Ready");
    std::process::exit(app.run());
}
