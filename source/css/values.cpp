#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace zui {
namespace css {

bool is_custom_property(std::string_view name) { return name.size() >= 2 && name[0] == '-' && name[1] == '-'; }


}  // namespace css
}  // namespace zui
