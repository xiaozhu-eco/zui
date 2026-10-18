#ifndef ZUI_EXT_FILE_ASSOCIATION_H
#define ZUI_EXT_FILE_ASSOCIATION_H

#include <zui/plugin.h>

namespace zui {

/// File-extension and URL-scheme registration. Choosable openers are a separate service.
class FileAssociationService {
public:
    virtual ~FileAssociationService() = default;
    virtual bool register_file_extension(const plugin::FileAssociation& spec) = 0;
    virtual bool unregister_file_extension(const plugin::FileAssociation& spec) = 0;
    virtual bool register_url_scheme(const plugin::SchemeAssociation& spec) = 0;
    virtual bool unregister_url_scheme(const plugin::SchemeAssociation& spec) = 0;
};

}  // namespace zui

#endif
