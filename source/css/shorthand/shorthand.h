#ifndef ZUI_CSS_SHORTHAND_H
#define ZUI_CSS_SHORTHAND_H

#include <string>
#include <utility>
#include <vector>

struct lxb_css_rule_declaration;

namespace zui {
namespace css {

/// Expands one declaration into longhands. Returns false when `decl` is already
/// a longhand; the caller stores it unchanged.
bool expand_shorthand(const lxb_css_rule_declaration* decl,
                      std::vector<std::pair<std::string, std::string>>& out);

/// A shorthand whose value contains var() or env(). Substitution has to finish
/// before the shorthand is parsed, because one function can expand into several
/// component tokens. Returns the property name and the raw specified value.
bool deferred_shorthand(const lxb_css_rule_declaration* decl, std::string& name, std::string& value);

/// Parse an already-substituted shorthand value into longhands.
bool expand_shorthand_value(std::string name, std::string_view value,
                            std::vector<std::pair<std::string, std::string>>& out);

/// Expand a `flex` value from its specified text. Lexbor's flex parser accepts only a
/// length or a percentage as `flex-basis`, and a `calc()` / `min()` / `max()` / `clamp()`
/// basis is rewritten into a unitless number while the parse still succeeds. Callers that
/// still have the stylesheet span use this instead of that struct.
bool expand_flex_text(std::string_view value,
                      std::vector<std::pair<std::string, std::string>>& out);

}  // namespace css
}  // namespace zui

#endif
