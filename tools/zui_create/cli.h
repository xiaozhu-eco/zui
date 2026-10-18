#pragma once

// The command line of zui_create. Parsing touches neither the filesystem nor the
// terminal; whatever it left out is what the wizard then asks for.

#include "version.h"

#include <string>
#include <utility>
#include <vector>

namespace zui_tool {

class Cli {
public:
    /// One answer per question the wizard can ask. An empty string (and
    /// targets_set / permissions_set / files_set false) means "not on the
    /// command line", which is what makes the wizard ask.
    struct Answers {
        bool help = false;
        bool targets_set = false;
        bool permissions_set = false;
        bool files_set = false;
        std::string name;
        std::string out_dir;
        std::string version;
        std::string identifier;
        std::string page;
        std::string entry;
        std::string icon;
        std::string target;
        std::vector<std::string> targets;
        std::vector<std::string> files;
        std::vector<std::pair<std::string, std::string>> permissions;
        std::string error;
    };

    static Answers parse(int argc, char** argv);
    static void usage();
};

}  // namespace zui_tool
