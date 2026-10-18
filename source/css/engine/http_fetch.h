#ifndef ZUI_CSS_HTTP_FETCH_H
#define ZUI_CSS_HTTP_FETCH_H

#include <string>
#include <string_view>

namespace zui {
namespace css {

/// Fetch `http://` / `https://` into `out`. Process-wide URL cache; network errors return false
/// without throwing (caller fails soft). Non-http(s) schemes return false.
bool fetch_url(std::string_view url, std::string& out);

/// Test hook: seed the cache without a network round-trip.
void cache_url_for_test(std::string_view url, std::string_view body);

}  // namespace css
}  // namespace zui

#endif
