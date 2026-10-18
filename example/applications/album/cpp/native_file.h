#ifndef ALBUM_NATIVE_FILE_H
#define ALBUM_NATIVE_FILE_H

#include <zui/app.h>

#include <filesystem>
#include <vector>

namespace album {

bool pick_images(zui::App& app, std::vector<std::filesystem::path>& out);

}  // namespace album

#endif
