// The editor side of zmd: the markdown rules and the document state they move.
// Both classes are free of engine types, so these are plain unit tests.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../example/applications/zmd/cpp/document.h"
#include "../../example/applications/zmd/cpp/markdown.h"

namespace {

const char* kSample =
    "# \xe6\xa0\x87\xe9\xa2\x98\n"
    "\n"
    "\xe6\xad\xa3\xe6\x96\x87 **\xe7\xb2\x97** \xe5\x92\x8c `\xe7\xa0\x81`"
    "\xef\xbc\x8c\xe5\xbc\x95\xe7\x94\xa8\xe4\xb8\x80\xe8\xa1\x8c\xe3\x80\x82\n"
    "\n"
    "> \xe5\xbc\x95\xe7\x94\xa8\n"
    "\n"
    "- \xe4\xb8\x80\n"
    "- \xe4\xba\x8c\n"
    "\n"
    "- [x] \xe5\xae\x8c\xe6\x88\x90\n"
    "\n"
    "| a | b |\n"
    "| --- | --- |\n"
    "| 1 | 2 |\n";

TEST(AppInteract, ZmdMarkdownKeepsPipesInsideInlineCode) {
    zmd::Markdown markdown;
    const std::string html = markdown.render(
        "| a | b |\n| --- | --- |\n| x | `|` |\n");
    EXPECT_NE(html.find("<code>|</code>"), std::string::npos) << html;
    EXPECT_EQ(html.find("<td></td>"), std::string::npos) << html;
}

TEST(AppInteract, ZmdMarkdownRendersWelcomeTablePipes) {
    zmd::Markdown markdown;
    const std::string html = markdown.render(
        "| \xe5\x85\x83\xe7\xb4\xa0 | \xe8\xaf\xad\xe6\xb3\x95 | \xe5\xbf\xab\xe6\x8d\xb7\xe9\x94\xae |\n"
        "| --- | --- | --- |\n"
        "| \xe8\xa1\xa8\xe6\xa0\xbc | `|` | Ctrl+K \xe5\x91\xbd\xe4\xbb\xa4\xe9\x9d\xa2\xe6\x9d\xbf |\n");
    EXPECT_NE(html.find("<code>|</code>"), std::string::npos) << html;
    EXPECT_EQ(html.find("<td></td>"), std::string::npos) << html;
    const std::size_t row = html.find("<tr>");
    ASSERT_NE(row, std::string::npos);
    const std::size_t body = html.find("<tr>", row + 1);
    ASSERT_NE(body, std::string::npos);
    const std::size_t row_end = html.find("</tr>", body);
    ASSERT_NE(row_end, std::string::npos);
    const std::string cells = html.substr(body, row_end - body);
    int td = 0;
    for (std::size_t at = 0; (at = cells.find("<td>", at)) != std::string::npos; at += 4) {
        ++td;
    }
    EXPECT_EQ(td, 3) << cells;
}

TEST(AppInteract, ZmdMarkdownKeepsSpacedInlineCode) {
    zmd::Markdown markdown;
    const std::string html = markdown.render(
        "- \xe8\xa1\x8c\xe9\xa6\x96\xe6\x89\x93 `# ` \xe6\xa0\x87\xe9\xa2\x98\n");
    EXPECT_NE(html.find("<code>#&#160;</code>"), std::string::npos) << html;
    EXPECT_NE(html.find("\xe6\xa0\x87\xe9\xa2\x98"), std::string::npos) << html;
}

TEST(AppInteract, ZmdMarkdownHighlightsTheDoubleEqualsRun) {
    zmd::Markdown markdown;
    const std::string html = markdown.render("a ==marked== b\n");
    EXPECT_NE(html.find("<mark>marked</mark>"), std::string::npos) << html;
    // A padded pair is not a mark: `a == b` is prose, and stays prose.
    const std::string plain = markdown.render("a == b\n");
    EXPECT_EQ(plain.find("<mark>"), std::string::npos) << plain;
}

TEST(AppInteract, ZmdMarkdownRendersBlocksAndInline) {
    zmd::Markdown markdown;
    const std::string html = markdown.render(kSample);
    EXPECT_NE(html.find("<h1 "), std::string::npos);
    EXPECT_NE(html.find("<strong>"), std::string::npos);
    EXPECT_NE(html.find("<code>"), std::string::npos);
    EXPECT_NE(html.find("<blockquote>"), std::string::npos);
    EXPECT_NE(html.find("<ul>"), std::string::npos);
    EXPECT_NE(html.find("class=\"mdw-task\""), std::string::npos);
    EXPECT_NE(html.find("<input type=\"checkbox\" checked>"), std::string::npos);
    EXPECT_NE(html.find("<table "), std::string::npos);
    EXPECT_NE(html.find("<td>1</td>"), std::string::npos);
}

// An ordered list keeps the ordinal its first marker spells: `3. ` renders `<ol start="3">`, which
// is where the sheet's counters begin, instead of renumbering the list from 1.
TEST(AppInteract, ZmdMarkdownKeepsAnOrderedListStart) {
    zmd::Markdown markdown;
    const std::string html = markdown.render("3. \xe4\xb8\x89\n4. \xe5\x9b\x9b\n");
    EXPECT_NE(html.find("<ol start=\"3\">"), std::string::npos) << html;
    EXPECT_EQ(html.find("<ol>"), std::string::npos) << html;
    // A list that starts at 1 stays a plain `<ol>`.
    const std::string plain = markdown.render("1. \xe4\xb8\x80\n2. \xe4\xba\x8c\n");
    EXPECT_NE(plain.find("<ol>"), std::string::npos) << plain;
    EXPECT_EQ(plain.find("start="), std::string::npos) << plain;
}

TEST(AppInteract, ZmdMarkdownEscapesMarkupAndKeepsCodeLiteral) {
    zmd::Markdown markdown;
    const std::string html = markdown.render("<script>x</script>\n\n`<b>`\n");
    EXPECT_EQ(html.find("<script>"), std::string::npos);
    EXPECT_NE(html.find("&lt;script&gt;"), std::string::npos);
    EXPECT_NE(html.find("<code>&lt;b&gt;</code>"), std::string::npos);
}

TEST(AppInteract, ZmdMarkdownCountsWordsAndLines) {
    zmd::Markdown markdown;
    // CJK is spelled from code points: a \u escape in the source is re-encoded by the
    // compiler into the execution character set, so the same escape yields different
    // bytes under a different code page.
    auto utf8 = [](unsigned cp) {
        std::string out;
        if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        }
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        return out;
    };
    const auto counts = markdown.counts("ab cd\n\n" + utf8(0x6807) + utf8(0x9898) + "\n");
    EXPECT_EQ(counts.second, 2);  // two non-blank lines
    EXPECT_EQ(counts.first, 4)    // "ab", "cd" and two CJK code points
        << "input bytes=" << ("ab cd\n\n" + utf8(0x6807) + utf8(0x9898) + "\n").size()
        << " cjk bytes=" << (utf8(0x6807) + utf8(0x9898)).size();
}

TEST(AppInteract, ZmdMarkdownOutlinesHeadingsWithDepth) {
    zmd::Markdown markdown;
    const std::vector<zmd::Heading> rows = markdown.headings(kSample);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].depth, 1);
    EXPECT_EQ(rows[0].text, "\xe6\xa0\x87\xe9\xa2\x98");
}

// The vis map is one source byte per emitted innerText byte. A find mark must not split a
// code point; a caret or a selection must not appear in the fragment at all.
TEST(AppInteract, ZmdMarkdownQuoteVisMapMatchesTheBody) {
    zmd::Markdown markdown;
    const std::string source =
        "# h\n\n"
        "p\n\n"
        "> \xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8\xe5\xba\x94\xe8\xaf\xa5"
        "\xe5\x9c\xa8\xe4\xbd\xa0\xe5\x86\x99\xe4\xbd\x9c\xe6\x97\xb6\xe6\xb6\x88\xe5\xa4\xb1"
        "\xef\xbc\x8c\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0\xe9\x9c\x80\xe8\xa6\x81\xe6\x97\xb6"
        "\xe5\x87\xba\xe7\x8e\xb0\xe3\x80\x82\n";
    const std::string quote =
        "\xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8\xe5\xba\x94\xe8\xaf\xa5"
        "\xe5\x9c\xa8\xe4\xbd\xa0\xe5\x86\x99\xe4\xbd\x9c\xe6\x97\xb6\xe6\xb6\x88\xe5\xa4\xb1"
        "\xef\xbc\x8c\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0\xe9\x9c\x80\xe8\xa6\x81\xe6\x97\xb6"
        "\xe5\x87\xba\xe7\x8e\xb0\xe3\x80\x82";
    const std::size_t body = source.find(quote);
    ASSERT_NE(body, std::string::npos);
    zmd::VisibleSources vis;
    zmd::Markdown::RenderCounts counts;
    markdown.render(source, static_cast<std::size_t>(-1), {}, nullptr, &counts, &vis);
    const auto it = vis.find("mdb1");
    ASSERT_TRUE(it != vis.end()) << "blocks=" << counts.blocks;
    ASSERT_EQ(it->second.at.size(), quote.size()) << "vis=" << it->second.at.size();
    ASSERT_FALSE(it->second.at.empty());
    EXPECT_EQ(it->second.at.front(), body) << "vis[0] should be 好, not the `>` marker";
    for (std::size_t i = 0; i < quote.size(); ++i) {
        EXPECT_EQ(it->second.at[i], body + i) << "i=" << i;
        if (it->second.at[i] != body + i) {
            break;
        }
    }
}

TEST(AppInteract, ZmdMarkdownTasksUseTheSheetCheckbox) {
    zmd::Markdown markdown;
    const std::string html = markdown.render("- [x] done\n- [ ] open\n");
    EXPECT_NE(html.find("class=\"mdw-tasks\""), std::string::npos) << html;
    EXPECT_NE(html.find("class=\"mdw-task\""), std::string::npos) << html;
    EXPECT_NE(html.find("<input type=\"checkbox\" checked>"), std::string::npos) << html;
    EXPECT_NE(html.find("<input type=\"checkbox\">"), std::string::npos) << html;
    EXPECT_EQ(html.find("md-check"), std::string::npos) << html;
    EXPECT_EQ(html.find("task-list"), std::string::npos) << html;
    EXPECT_NE(html.find("done"), std::string::npos) << html;
    EXPECT_NE(html.find("open"), std::string::npos) << html;
}

TEST(AppInteract, ZmdMarkdownSelectionDoesNotSplitUtf8) {
    zmd::Markdown markdown;
    const std::string source =
        "> \xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8\xe5\xba\x94\xe8\xaf\xa5"
        "\xe5\x9c\xa8\xe4\xbd\xa0\xe5\x86\x99\xe4\xbd\x9c\xe6\x97\xb6\xe6\xb6\x88\xe5\xa4\xb1"
        "\xef\xbc\x8c\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0\xe9\x9c\x80\xe8\xa6\x81\xe6\x97\xb6"
        "\xe5\x87\xba\xe7\x8e\xb0\xe3\x80\x82\n";
    const std::size_t comma = source.find("\xef\xbc\x8c");
    ASSERT_NE(comma, std::string::npos);
    // A caret on the first byte of `，` used to be a reason to split that code point. The
    // fragment for that caret and the fragment with no caret carry the same quote text.
    const std::string plain = markdown.render(source);
    const std::string caret = markdown.render(source, comma + 1);
    EXPECT_EQ(plain.find("mdw-sel"), std::string::npos) << plain;
    EXPECT_EQ(plain.find("mdw-selhost"), std::string::npos) << plain;
    EXPECT_EQ(plain.find("mdw-bubble"), std::string::npos) << plain;
    EXPECT_EQ(caret.find("mdw-sel"), std::string::npos) << caret;
    EXPECT_EQ(caret.find("mdw-bubble"), std::string::npos) << caret;
    EXPECT_NE(plain.find("\xe6\xb6\x88\xe5\xa4\xb1"), std::string::npos) << plain;  // 消失
    EXPECT_NE(plain.find("\xef\xbc\x8c"), std::string::npos) << plain;              // ，
    EXPECT_NE(plain.find("\xe7\x8e\xb0"), std::string::npos) << plain;              // 现
    EXPECT_NE(caret.find("\xef\xbc\x8c"), std::string::npos) << caret;
    EXPECT_NE(caret.find("\xe7\x8e\xb0"), std::string::npos) << caret;
    // The only difference a caret may make is the `mdw-cur` class on the block.
    auto strip_cur = [](std::string html) {
        const std::string token = " class=\"mdw-cur\"";
        for (std::size_t at = html.find(token); at != std::string::npos; at = html.find(token)) {
            html.erase(at, token.size());
        }
        return html;
    };
    EXPECT_EQ(strip_cur(plain), strip_cur(caret));
}

TEST(AppInteract, ZmdMarkdownSelectionEmitsTheFormatToolbarOnce) {
    zmd::Markdown markdown;
    const std::string source =
        "> \xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8\xe5\xba\x94\xe8\xaf\xa5"
        "\xe5\x9c\xa8\xe4\xbd\xa0\xe5\x86\x99\xe4\xbd\x9c\xe6\x97\xb6\xe6\xb6\x88\xe5\xa4\xb1"
        "\xef\xbc\x8c\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0\xe9\x9c\x80\xe8\xa6\x81\xe6\x97\xb6"
        "\xe5\x87\xba\xe7\x8e\xb0\xe3\x80\x82\n";
    const std::string quote =
        "\xe5\xa5\xbd\xe7\x9a\x84\xe7\xbc\x96\xe8\xbe\x91\xe5\x99\xa8\xe5\xba\x94\xe8\xaf\xa5"
        "\xe5\x9c\xa8\xe4\xbd\xa0\xe5\x86\x99\xe4\xbd\x9c\xe6\x97\xb6\xe6\xb6\x88\xe5\xa4\xb1"
        "\xef\xbc\x8c\xe5\x8f\xaa\xe5\x9c\xa8\xe4\xbd\xa0\xe9\x9c\x80\xe8\xa6\x81\xe6\x97\xb6"
        "\xe5\x87\xba\xe7\x8e\xb0\xe3\x80\x82";
    const std::size_t body = source.find(quote);
    ASSERT_NE(body, std::string::npos);
    zmd::VisibleSources vis;
    zmd::Markdown::RenderCounts counts;
    const std::string html = markdown.render(source, body, {}, nullptr, &counts, &vis);
    EXPECT_EQ(html.find("mdw-bubble"), std::string::npos) << html;
    EXPECT_EQ(html.find("mdw-sel"), std::string::npos) << html;
    EXPECT_EQ(html.find("zmd-sel-bold"), std::string::npos) << html;
    const auto it = vis.find("mdb0");
    ASSERT_TRUE(it != vis.end()) << "blocks=" << counts.blocks;
    ASSERT_EQ(it->second.at.size(), quote.size());
    EXPECT_EQ(it->second.at.front(), body);
}

TEST(AppInteract, ZmdDocumentTracksDirtyTypingAndUndo) {
    zmd::Document document;
    document.reset("doc.md", "one", {});
    EXPECT_FALSE(document.dirty());
    document.set_source("two", true);
    EXPECT_TRUE(document.dirty());
    EXPECT_EQ(document.save_state(), zmd::Document::Save::Typing);
    std::string restored;
    ASSERT_TRUE(document.undo(restored));
    EXPECT_EQ(restored, "one");
    document.mark_saved();
    EXPECT_FALSE(document.dirty());
    EXPECT_EQ(document.save_state(), zmd::Document::Save::Saved);
    document.mark_error();
    EXPECT_EQ(document.save_state(), zmd::Document::Save::Error);
}

}  // namespace
