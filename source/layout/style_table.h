#ifndef ZUI_LAYOUT_STYLE_TABLE_H
#define ZUI_LAYOUT_STYLE_TABLE_H

#include "layout/layout_style.h"

#include <memory>
#include <unordered_map>

namespace zui {
namespace layout {

class StyleTable {
public:
    static StyleTable& process();

    std::shared_ptr<const LayoutStyle> intern(LayoutStyle fields);

    /// Live entries. LAYOUT.md section 16 exports this as the intern-table counter, and
    /// section 12 uses it to assert that an animation's style churn is reclaimed.
    std::size_t size() const { return entries_.size(); }

private:
    struct EntryHash {
        std::size_t operator()(const LayoutStyle& style) const noexcept;
    };
    struct EntryEq {
        bool operator()(const LayoutStyle& left, const LayoutStyle& right) const;
    };

    std::unordered_map<LayoutStyle, std::weak_ptr<const LayoutStyle>, EntryHash, EntryEq> entries_;
};

}  // namespace layout
}  // namespace zui

#endif
