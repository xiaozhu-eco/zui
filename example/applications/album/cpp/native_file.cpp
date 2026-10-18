#include "native_file.h"

namespace album {

bool pick_images(zui::App& app, std::vector<std::filesystem::path>& out) {
    out.clear();
    zui::FileDialogOptions opt;
    opt.title = "Import Photos";
    opt.multi = true;
    opt.filters.push_back({"Images", "png;jpg;jpeg;svg"});
    const zui::PickPaths picked = app.host().pick_open(opt);
    if (picked.status != zui::PickStatus::Ok) {
        return false;
    }
    out.reserve(picked.paths.size());
    for (const std::string& path : picked.paths) {
        // The host hands back UTF-8. `std::filesystem::path`'s narrow constructor reads the
        // process code page instead, so a non-ASCII name either stats the wrong file (the import
        // then reports a bogus size) or throws when a character cannot be mapped at all.
        out.push_back(std::filesystem::u8path(path));
    }
    return !out.empty();
}

}  // namespace album
