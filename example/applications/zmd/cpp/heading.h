#ifndef ZMD_HEADING_H
#define ZMD_HEADING_H

#include <string>

namespace zmd {

/// One heading of the document: what the outline panel lists and jumps to.
struct Heading {
    int depth = 1;
    std::string text;
};

}  // namespace zmd

#endif
