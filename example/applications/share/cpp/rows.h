#ifndef SHARE_ROWS_H
#define SHARE_ROWS_H

#include "browser.h"
#include "net.h"

#include <zui/model.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace share {

/// The class suffix a file row draws with (`gl-dir`, `gl-pdf`, …).
std::string file_kind_class(std::string_view name, bool is_dir);

/// The 类型 column: 文件夹 / PDF / 安装包 / 视频 / 图片 / 文本.
std::string file_kind_label(std::string_view name, bool is_dir);

/// First UTF-8 code point of @p text, or "竹" when it is empty — the avatar glyph.
std::string first_glyph(std::string_view text);

/// `#peer-chips` — the narrow device strip: a green dot plus the name.
class PeerChipRows : public zui::ListModel {
public:
    explicit PeerChipRows(const std::vector<Peer>& peers) : peers_(&peers) {}

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;
    std::string row_key(std::size_t row) const override;

private:
    const std::vector<Peer>* peers_;
};

/// `#peer-side` and `#pick-list` — the device card: avatar glyph, name, ip, and the `data-id`
/// the 发送 button reports back.
class PeerCardRows : public zui::ListModel {
public:
    explicit PeerCardRows(const std::vector<Peer>& peers) : peers_(&peers) {}

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;
    std::string row_key(std::size_t row) const override;

private:
    const std::vector<Peer>* peers_;
};

/// `#file-list` — one row per directory entry. `{{id}}` is the row index, `{{path}}` the entry's
/// full path (what `Event::data("path")` reports back).
class FileRows : public zui::ListModel {
public:
    explicit FileRows(const FileBrowser& browser) : browser_(&browser) {}

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;
    std::string row_key(std::size_t row) const override;

private:
    const FileBrowser* browser_;
};

/// `#done-list` — the files a transfer already finished.
class DoneRows : public zui::ListModel {
public:
    explicit DoneRows(const std::vector<std::string>& names) : names_(&names) {}

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;

private:
    const std::vector<std::string>* names_;
};

/// `#xfer-bar` — a single row. `{{pct}}` lands inside the template's `style` attribute, because
/// the A-tier surface has no way to write a style property from C++.
class ProgressRows : public zui::ListModel {
public:
    explicit ProgressRows(double fraction) : percent_(fraction * 100.0) {}

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;

private:
    double percent_;
};

}  // namespace share

#endif  // SHARE_ROWS_H
