#ifndef ZMD_COMMAND_H
#define ZMD_COMMAND_H

#include <string>

namespace zmd {

/// One row of the command palette: what to show and which handler id to run.
struct Command {
    std::string id;
    std::string label;
    std::string hint;
    std::string icon;
};

}  // namespace zmd

#endif
