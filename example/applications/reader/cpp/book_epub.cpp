#include "book_epub.h"

#include "book_detail.h"
#include "book_format.h"
#include "book_zip.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>
#include <functional>

namespace reader {
namespace {

std::string percent_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return -1;
            };
            const int hi = hex(in[i + 1]);
            const int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}


std::filesystem::path epub_cache_root(const Book& book) {
    if (!book.cache_dir.empty()) {
        return book.cache_dir;
    }
    return book.path.parent_path() / ".cache" / book.key;
}

bool safe_cache_rel(std::string_view rel) {
    if (rel.empty() || rel[0] == '/' || rel.find(':') != std::string_view::npos) {
        return false;
    }
    if (rel.find("..") != std::string_view::npos) {
        return false;
    }
    return true;
}

std::string xml_local(const char* name) {
    if (name == nullptr) {
        return {};
    }
    std::string s(name);
    const auto pos = s.find(':');
    if (pos != std::string::npos) {
        return s.substr(pos + 1);
    }
    return s;
}

std::string text_of(const pugi::xml_node& node) {
    std::string out;
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            out += child.value();
        } else {
            out += text_of(child);
        }
    }
    return out;
}


bool is_block_anchor_name(std::string_view name) {
    return name == "p" || name == "h1" || name == "h2" || name == "h3" || name == "h4" ||
           name == "h5" || name == "h6" || name == "li" || name == "blockquote" ||
           name == "div" || name == "section" || name == "article" || name == "pre" ||
           name == "td" || name == "th" || name == "dt" || name == "dd" || name == "figcaption";
}

bool has_block_anchor_child(const pugi::xml_node& node) {
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) {
            continue;
        }
        if (is_block_anchor_name(xml_local(child.name()))) {
            return true;
        }
        // Nested lists/tables wrap li/td; walk through non-anchor containers too.
        if (has_block_anchor_child(child)) {
            return true;
        }
    }
    return false;
}

void collect_text_paras(const pugi::xml_node& node, std::vector<std::string>& paras) {
    const std::string name = xml_local(node.name());
    if (name == "script" || name == "style") {
        return;
    }
    // Leaf block only: skip containers that still wrap other anchorable blocks so
    // #paN indices stay 1:1 with unique text units (p inside li, etc.).
    if (is_block_anchor_name(name)) {
        if (!has_block_anchor_child(node)) {
            std::string t = trim_copy(text_of(node));
            if (!t.empty()) {
                paras.push_back(std::move(t));
            }
            return;
        }
        // Container with nested anchors: walk children only.
    }
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element) {
            collect_text_paras(child, paras);
        }
    }
}

std::string html_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string attr(const pugi::xml_node& node, const char* key) {
    return node.attribute(key).as_string();
}

bool transform_node(const pugi::xml_node& node, const std::string& doc_entry, ZipReader& zip,
                    const std::filesystem::path& cache, std::ostringstream& html,
                    std::vector<std::string>& paras);

bool is_inline_flow_name(std::string_view name) {
    return name == "em" || name == "strong" || name == "span" || name == "a" || name == "code" ||
           name == "i" || name == "b" || name == "u" || name == "small" || name == "sub" ||
           name == "sup" || name == "br" || name == "img" || name == "ruby" || name == "rt" ||
           name == "rb" || name == "wbr" || name == "abbr" || name == "cite" || name == "q" ||
           name == "kbd" || name == "samp" || name == "var" || name == "time" || name == "mark";
}

void flush_orphan_run(std::ostringstream& html, std::vector<std::string>& paras,
                      std::string& run_html, std::string& run_text) {
    const std::string trimmed = trim_copy(run_text);
    if (trimmed.empty()) {
        html << run_html;
        run_html.clear();
        run_text.clear();
        return;
    }
    html << "<p class=\"epub-orphan\" id=\"pa" << paras.size() << "\">" << run_html << "</p>";
    paras.push_back(trimmed);
    run_html.clear();
    run_text.clear();
}

void transform_children_anchoring_orphans(const pugi::xml_node& parent, const std::string& doc_entry,
                                         ZipReader& zip, const std::filesystem::path& cache,
                                         std::ostringstream& html, std::vector<std::string>& paras) {
    std::string run_html;
    std::string run_text;
    for (pugi::xml_node child = parent.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            run_html += html_escape(child.value());
            run_text += child.value();
            continue;
        }
        if (child.type() != pugi::node_element) {
            continue;
        }
        const std::string name = xml_local(child.name());
        if (name == "script" || name == "style") {
            continue;
        }
        if (is_inline_flow_name(name) && !is_block_anchor_name(name)) {
            std::ostringstream piece;
            transform_node(child, doc_entry, zip, cache, piece, paras);
            run_html += piece.str();
            run_text += text_of(child);
            continue;
        }
        flush_orphan_run(html, paras, run_html, run_text);
        transform_node(child, doc_entry, zip, cache, html, paras);
    }
    flush_orphan_run(html, paras, run_html, run_text);
}

bool transform_node(const pugi::xml_node& node, const std::string& doc_entry, ZipReader& zip,
                    const std::filesystem::path& cache, std::ostringstream& html,
                    std::vector<std::string>& paras) {
    if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
        html << html_escape(node.value());
        return true;
    }
    if (node.type() != pugi::node_element) {
        return true;
    }
    const std::string name = xml_local(node.name());
    if (name == "script" || name == "style" || name == "iframe" || name == "object" ||
        name == "embed" || name == "svg") {
        return true;
    }
    if (name == "br") {
        html << "<br/>";
        return true;
    }
    if (name == "img") {
        std::string src = attr(node, "src");
        if (src.empty()) {
            src = attr(node, "xlink:href");
        }
        if (src.empty() || src.find("://") != std::string::npos || starts_with(src, "data:")) {
            return true;
        }
        src = percent_decode(src);
        const auto hash = src.find('#');
        if (hash != std::string::npos) {
            src.resize(hash);
        }
        const std::string entry = zip_join(doc_entry, src);
        if (!safe_cache_rel(entry)) {
            return true;
        }
        const std::filesystem::path dest = cache / std::filesystem::u8path(entry);
        if (!std::filesystem::exists(dest)) {
            (void)zip.extract_to(entry, dest);
        }
        if (std::filesystem::exists(dest)) {
            std::string path = to_utf8(dest);
            for (char& c : path) {
                if (c == '\\') {
                    c = '/';
                }
            }
            html << "<img class=\"epub-img\" src=\"" << html_escape(path) << "\" alt=\"\"/>";
        }
        return true;
    }

    const bool block = name == "p" || name == "div" || name == "section" || name == "article" ||
                       name == "h1" || name == "h2" || name == "h3" || name == "h4" ||
                       name == "h5" || name == "h6" || name == "li" || name == "blockquote" ||
                       name == "ul" || name == "ol" || name == "pre" || name == "td" ||
                       name == "th" || name == "dt" || name == "dd" || name == "figcaption" ||
                       name == "figure" || name == "table" || name == "tr";
    std::string tag = name;
    if (name == "section" || name == "article" || name == "div") {
        tag = "div";
    }
    if (name == "i") {
        tag = "em";
    }
    if (name == "b") {
        tag = "strong";
    }
    const bool inline_ok = tag == "em" || tag == "strong" || tag == "span" || tag == "a" ||
                           tag == "code" || tag == "p" || tag == "div" || tag == "h1" ||
                           tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" ||
                           tag == "h6" || tag == "li" || tag == "ul" || tag == "ol" ||
                           tag == "blockquote" || tag == "pre" || tag == "td" || tag == "th" ||
                           tag == "dt" || tag == "dd" || tag == "figcaption" || tag == "figure" ||
                           tag == "table" || tag == "tr";
    if (!inline_ok && !block) {
        transform_children_anchoring_orphans(node, doc_entry, zip, cache, html, paras);
        return true;
    }

    // Leaf block anchors (p/h/li/blockquote/div/section/…): skip nodes that still wrap
    // other anchorable blocks so #paN stays 1:1 with unique text units.
    const bool anchor_candidate = is_block_anchor_name(name) && !has_block_anchor_child(node);
    std::string para_text;
    if (anchor_candidate) {
        para_text = trim_copy(text_of(node));
    }
    html << "<" << tag;
    if (anchor_candidate && !para_text.empty()) {
        html << " id=\"pa" << paras.size() << "\"";
    }
    if (tag == "p" || (tag.size() == 2 && tag[0] == 'h')) {
        html << " class=\"epub-" << tag << "\"";
    } else if (tag == "img") {
        html << " class=\"epub-img\"";
    } else {
        html << " class=\"epub-" << tag << "\"";
    }
    html << ">";
    // Leaf anchors already own their text; nested walk would double-count orphans inside.
    if (anchor_candidate && !para_text.empty()) {
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
            transform_node(child, doc_entry, zip, cache, html, paras);
        }
    } else {
        transform_children_anchoring_orphans(node, doc_entry, zip, cache, html, paras);
    }
    html << "</" << tag << ">";
    if (anchor_candidate && !para_text.empty()) {
        paras.push_back(std::move(para_text));
    }
    return true;
}

bool xhtml_to_chapter(const std::string& bytes, const std::string& doc_entry, ZipReader& zip,
                      const std::filesystem::path& cache, Chapter& chapter) {
    pugi::xml_document doc;
    // EPUB XHTML is often missing xmlns-aware parse; try XML then HTML-ish.
    pugi::xml_parse_result ok =
        doc.load_buffer(bytes.data(), bytes.size(), pugi::parse_default | pugi::parse_declaration);
    if (!ok) {
        ok = doc.load_buffer(bytes.data(), bytes.size(),
                             pugi::parse_default | pugi::parse_declaration | pugi::parse_fragment);
    }
    if (!ok) {
        // Fall back: treat as plain text paragraphs.
        for (const std::string& line : split_lines(bytes)) {
            const std::string cleaned = clean_line(line);
            if (!cleaned.empty()) {
                chapter.paragraphs.push_back(cleaned);
            }
        }
        if (chapter.paragraphs.empty()) {
            chapter.paragraphs.push_back(kEmpty);
        }
        std::ostringstream html;
        html << "<div class=\"epub-body\">";
        for (size_t i = 0; i < chapter.paragraphs.size(); ++i) {
            html << "<p class=\"epub-p\" id=\"pa" << i << "\">"
                 << html_escape(chapter.paragraphs[i]) << "</p>";
        }
        html << "</div>";
        chapter.html = html.str();
        chapter.parsed = true;
        return true;
    }

    pugi::xml_node root = doc.child("html");
    if (!root) {
        root = doc.document_element();
    }
    pugi::xml_node body = root.child("body");
    if (!body) {
        for (pugi::xml_node n = root.first_child(); n; n = n.next_sibling()) {
            if (xml_local(n.name()) == "body") {
                body = n;
                break;
            }
        }
    }
    if (!body) {
        body = root;
    }

    std::ostringstream html;
    html << "<div class=\"epub-body\">";
    std::vector<std::string> paras;
    transform_children_anchoring_orphans(body, doc_entry, zip, cache, html, paras);
    html << "</div>";
    chapter.html = html.str();
    chapter.paragraphs = std::move(paras);
    if (chapter.paragraphs.empty()) {
        const std::string all = trim_copy(text_of(body));
        if (!all.empty()) {
            chapter.paragraphs.push_back(all);
            std::ostringstream wrap;
            wrap << "<div class=\"epub-body\"><p class=\"epub-orphan\" id=\"pa0\">"
                 << html_escape(all) << "</p></div>";
            chapter.html = wrap.str();
        } else {
            chapter.paragraphs.push_back(kEmpty);
        }
    }
    chapter.parsed = true;
    return true;
}

struct EpubPackage {
    std::string opf_path;
    std::string title;
    std::string author;
    struct Item {
        std::string id;
        std::string href;
        std::string media;
    };
    std::map<std::string, Item> manifest;
    std::vector<std::string> spine;  // ids
    std::map<std::string, std::string> toc_title;  // href(no hash) -> title
};

std::string strip_hash(std::string href) {
    const auto hash = href.find('#');
    if (hash != std::string::npos) {
        href.resize(hash);
    }
    return href;
}

bool load_container(ZipReader& zip, std::string& opf_path) {
    std::string xml;
    if (!zip.read("META-INF/container.xml", xml)) {
        return false;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return false;
    }
    for (pugi::xml_node rootfile : doc.child("container").child("rootfiles").children("rootfile")) {
        const char* full = rootfile.attribute("full-path").as_string();
        if (full && *full) {
            opf_path = zip_normalize(full);
            return true;
        }
    }
    std::function<bool(pugi::xml_node)> walk;
    walk = [&](pugi::xml_node node) -> bool {
        for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
            if (c.type() != pugi::node_element) {
                continue;
            }
            if (xml_local(c.name()) == "rootfile") {
                const char* full = c.attribute("full-path").as_string();
                if (full && *full) {
                    opf_path = zip_normalize(full);
                    return true;
                }
            }
            if (walk(c)) {
                return true;
            }
        }
        return false;
    };
    return walk(doc.document_element());
}

bool load_opf(ZipReader& zip, EpubPackage& pkg) {
    std::string xml;
    if (!zip.read(pkg.opf_path, xml)) {
        return false;
    }
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) {
        return false;
    }
    pugi::xml_node package = doc.document_element();
    pugi::xml_node metadata = package.child("metadata");
    if (!metadata) {
        for (pugi::xml_node n = package.first_child(); n; n = n.next_sibling()) {
            if (xml_local(n.name()) == "metadata") {
                metadata = n;
                break;
            }
        }
    }
    if (metadata) {
        for (pugi::xml_node n = metadata.first_child(); n; n = n.next_sibling()) {
            const std::string ln = xml_local(n.name());
            if (ln == "title" && pkg.title.empty()) {
                pkg.title = trim_copy(text_of(n));
            }
            if ((ln == "creator" || ln == "author") && pkg.author.empty()) {
                pkg.author = trim_copy(text_of(n));
            }
        }
    }

    pugi::xml_node manifest = package.child("manifest");
    if (!manifest) {
        for (pugi::xml_node n = package.first_child(); n; n = n.next_sibling()) {
            if (xml_local(n.name()) == "manifest") {
                manifest = n;
                break;
            }
        }
    }
    const std::string opf_dir = zip_dirname(pkg.opf_path);
    std::string ncx_href;
    std::string nav_href;
    if (manifest) {
        for (pugi::xml_node item = manifest.first_child(); item; item = item.next_sibling()) {
            if (xml_local(item.name()) != "item") {
                continue;
            }
            EpubPackage::Item it;
            it.id = item.attribute("id").as_string();
            it.href = zip_join(pkg.opf_path, percent_decode(item.attribute("href").as_string()));
            it.media = item.attribute("media-type").as_string();
            const std::string props = item.attribute("properties").as_string();
            if (props.find("nav") != std::string::npos) {
                nav_href = it.href;
            }
            if (it.media == "application/x-dtbncx+xml") {
                ncx_href = it.href;
            }
            if (!it.id.empty()) {
                pkg.manifest[it.id] = it;
            }
        }
    }

    pugi::xml_node spine = package.child("spine");
    if (!spine) {
        for (pugi::xml_node n = package.first_child(); n; n = n.next_sibling()) {
            if (xml_local(n.name()) == "spine") {
                spine = n;
                break;
            }
        }
    }
    if (spine) {
        if (ncx_href.empty()) {
            const char* toc = spine.attribute("toc").as_string();
            if (toc && *toc) {
                const auto found = pkg.manifest.find(toc);
                if (found != pkg.manifest.end()) {
                    ncx_href = found->second.href;
                }
            }
        }
        for (pugi::xml_node itemref = spine.first_child(); itemref; itemref = itemref.next_sibling()) {
            if (xml_local(itemref.name()) != "itemref") {
                continue;
            }
            const std::string idref = itemref.attribute("idref").as_string();
            if (!idref.empty()) {
                pkg.spine.push_back(idref);
            }
        }
    }

    auto ingest_toc_href = [&](const std::string& href, const std::string& title) {
        if (title.empty()) {
            return;
        }
        const std::string key = strip_hash(href);
        if (key.empty()) {
            return;
        }
        if (pkg.toc_title.find(key) == pkg.toc_title.end()) {
            pkg.toc_title[key] = title;
        }
    };

    if (!nav_href.empty()) {
        std::string nav_xml;
        if (zip.read(nav_href, nav_xml)) {
            pugi::xml_document nav;
            if (nav.load_buffer(nav_xml.data(), nav_xml.size())) {
                std::function<void(pugi::xml_node)> walk_nav;
                walk_nav = [&](pugi::xml_node node) {
                    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
                        if (c.type() != pugi::node_element) {
                            continue;
                        }
                        if (xml_local(c.name()) == "a") {
                            const std::string href =
                                zip_join(nav_href, c.attribute("href").as_string());
                            ingest_toc_href(href, trim_copy(text_of(c)));
                        }
                        walk_nav(c);
                    }
                };
                walk_nav(nav.document_element());
            }
        }
    }
    if (!ncx_href.empty()) {
        std::string ncx_xml;
        if (zip.read(ncx_href, ncx_xml)) {
            pugi::xml_document ncx;
            if (ncx.load_buffer(ncx_xml.data(), ncx_xml.size())) {
                std::function<void(pugi::xml_node)> walk_ncx;
                walk_ncx = [&](pugi::xml_node node) {
                    for (pugi::xml_node point = node.first_child(); point; point = point.next_sibling()) {
                        if (point.type() != pugi::node_element) {
                            continue;
                        }
                        if (xml_local(point.name()) == "navPoint") {
                            std::string title;
                            std::string src;
                            for (pugi::xml_node c = point.first_child(); c; c = c.next_sibling()) {
                                const std::string ln = xml_local(c.name());
                                if (ln == "navLabel") {
                                    title = trim_copy(text_of(c));
                                }
                                if (ln == "content") {
                                    src = c.attribute("src").as_string();
                                }
                            }
                            if (!src.empty()) {
                                ingest_toc_href(zip_join(ncx_href, src), title);
                            }
                        }
                        walk_ncx(point);
                    }
                };
                walk_ncx(ncx.document_element());
            }
        }
    }
    (void)opf_dir;
    return !pkg.spine.empty();
}

bool open_package(const std::filesystem::path& path, ZipReader& zip, EpubPackage& pkg) {
    if (!zip.open(path)) {
        return false;
    }
    if (!load_container(zip, pkg.opf_path)) {
        return false;
    }
    return load_opf(zip, pkg);
}

void apply_meta(Book& book, const EpubPackage& pkg) {
    book.format = BookFormat::Epub;
    if (!pkg.title.empty()) {
        book.title = pkg.title;
    }
    if (!pkg.author.empty()) {
        book.author = pkg.author;
    }
    book.chapter_count = static_cast<int>(pkg.spine.size());
    book.cache_dir = epub_cache_root(book);
}

void ensure_spine_titles(Book& book, const EpubPackage& pkg) {
    book.chapters.resize(pkg.spine.size());
    for (size_t i = 0; i < pkg.spine.size(); ++i) {
        Chapter& ch = book.chapters[i];
        const auto man = pkg.manifest.find(pkg.spine[i]);
        std::string href;
        if (man != pkg.manifest.end()) {
            href = man->second.href;
        }
        auto toc = pkg.toc_title.find(href);
        if (toc != pkg.toc_title.end()) {
            ch.title = toc->second;
        } else if (ch.title.empty()) {
            ch.title = "Chapter " + std::to_string(static_cast<int>(i) + 1);
        }
        ch.file_offset = static_cast<std::uint64_t>(i);
    }
    book.chapter_count = static_cast<int>(book.chapters.size());
    book.chapters_indexed = true;
}

bool fill_chapter_body(Book& book, ZipReader& zip, const EpubPackage& pkg, int index) {
    if (index < 0 || index >= static_cast<int>(pkg.spine.size())) {
        return false;
    }
    if (static_cast<int>(book.chapters.size()) <= index) {
        book.chapters.resize(static_cast<size_t>(index) + 1);
    }
    Chapter& ch = book.chapters[static_cast<size_t>(index)];
    if (ch.parsed && !ch.html.empty()) {
        return true;
    }
    const auto man = pkg.manifest.find(pkg.spine[static_cast<size_t>(index)]);
    if (man == pkg.manifest.end()) {
        return false;
    }
    std::string bytes;
    if (!zip.read(man->second.href, bytes)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(book.cache_dir, ec);
    ch.paragraphs.clear();
    ch.html.clear();
    if (ch.title.empty()) {
        auto toc = pkg.toc_title.find(man->second.href);
        if (toc != pkg.toc_title.end()) {
            ch.title = toc->second;
        } else {
            ch.title = "Chapter " + std::to_string(index + 1);
        }
    }
    return xhtml_to_chapter(bytes, man->second.href, zip, book.cache_dir, ch);
}

}  // namespace

bool epub_summary(const std::filesystem::path& path, Book& book) {
    ZipReader zip;
    EpubPackage pkg;
    book.path = path;
    book.key = to_utf8(path.filename());
    book.title = to_utf8(path.stem());
    book.format = BookFormat::Epub;
    book.chapters.clear();
    book.chapter_count = 0;
    book.chars = 0;
    if (!open_package(path, zip, pkg)) {
        return false;
    }
    apply_meta(book, pkg);
    ensure_spine_titles(book, pkg);
    // Summary keeps titles only; drop bodies.
    for (Chapter& ch : book.chapters) {
        ch.paragraphs.clear();
        ch.html.clear();
        ch.parsed = false;
    }
    book.index_loaded = true;
    return book.chapter_count > 0;
}

bool load_epub_window(Book& book, int center, int radius) {
    if (book.path.empty()) {
        return false;
    }
    ZipReader zip;
    EpubPackage pkg;
    if (!open_package(book.path, zip, pkg)) {
        return false;
    }
    apply_meta(book, pkg);
    ensure_spine_titles(book, pkg);
    if (book.chapter_count <= 0) {
        return false;
    }
    if (center < 0) {
        center = 0;
    }
    if (center >= book.chapter_count) {
        center = book.chapter_count - 1;
    }
    const int lo = (std::max)(0, center - radius);
    const int hi = (std::min)(book.chapter_count - 1, center + radius);
    for (int i = lo; i <= hi; ++i) {
        if (!fill_chapter_body(book, zip, pkg, i)) {
            if (i == center) {
                return false;
            }
        }
    }
    book.index_loaded = true;
    return book.chapters[static_cast<size_t>(center)].parsed;
}

void ensure_epub_index(Book& book) {
    if (book.chapters_indexed && !book.chapters.empty()) {
        return;
    }
    ZipReader zip;
    EpubPackage pkg;
    if (!open_package(book.path, zip, pkg)) {
        return;
    }
    apply_meta(book, pkg);
    ensure_spine_titles(book, pkg);
    book.index_loaded = true;
}

}  // namespace reader
