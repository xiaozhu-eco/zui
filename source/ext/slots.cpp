#include "ext/slots.h"

namespace zui {

ExtensionRegistry& ExtensionRegistry::instance() {
    static ExtensionRegistry registry;
    return registry;
}

}  // namespace zui
