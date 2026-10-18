#ifndef ZUI_EXT_FILE_DIALOG_H
#define ZUI_EXT_FILE_DIALOG_H

#include <ext/platform.h>
#include <zui/host_surface.h>
#include <zui/host_types.h>

namespace zui {

class FileDialogService {
public:
    virtual ~FileDialogService() = default;
    virtual PickPaths open(NativeWindowId owner, const FileDialogOptions& opt) = 0;
    virtual PickPath save(NativeWindowId owner, const FileDialogOptions& opt) = 0;
    virtual PickPath folder(NativeWindowId owner, const FileDialogOptions& opt) = 0;
};

}  // namespace zui

#endif
