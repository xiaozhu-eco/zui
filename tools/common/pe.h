#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace zui_tool {

// Imported DLL names of a PE image, in import-table order. Fails when the file
// is not a PE image. A PE image with no import directory yields an empty list.
bool read_pe_imports(const std::filesystem::path& file, std::vector<std::string>& names,
                     std::string& error);

// Every DLL the executable imports that exists in one of `dirs`, plus the same
// closure over those DLLs. `external` receives the imports that resolve
// nowhere; those must come from Windows or a redistributable on the machine
// that runs the package. An unresolved name of a zui image fails: every zui
// DLL is built here, so a package that cannot ship one is broken.
bool collect_local_dlls(const std::filesystem::path& exe,
                        const std::vector<std::filesystem::path>& dirs,
                        std::vector<std::filesystem::path>& out, std::vector<std::string>& external,
                        std::string& error);

// True for the names zui builds itself (zui.dll, zui_plugin_*.dll).
bool dll_name_is_zui(const std::string& name);

// True for the Visual C++ runtime names. A package that does not ship one
// needs the redistributable installed.
bool dll_name_is_redistributable(const std::string& name);

}  // namespace zui_tool
