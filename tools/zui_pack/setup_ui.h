#pragma once

// Painting and layout of the installer window. The module knows nothing about
// the window: the same code draws the live window and, offscreen, the PNGs
// `zui_setup_preview` writes, so the look of the installer is reviewable.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <gdiplus.h>

#include <string>
#include <vector>

namespace zui_tool::setup_ui {

enum class Screen { Install, Progress, Done, Error, Uninstall };

// Everything the pointer or the keyboard can reach. Edit is the path field the
// window hosts as an EDIT control.
enum class Item { Path, Edit, Browse, Primary, Secondary, RunAfter };

enum class Lang { En, Zh };

struct Labels {
    const wchar_t* install_to;
    const wchar_t* browse;
    const wchar_t* install;
    const wchar_t* cancel;
    const wchar_t* installing;
    const wchar_t* done_title;
    const wchar_t* installed_to;
    const wchar_t* run_after;
    const wchar_t* finish;
    const wchar_t* close;
    const wchar_t* failed_title;
    const wchar_t* uninstall_title;
    const wchar_t* uninstall_question;  // "%s"
    const wchar_t* uninstall_note;
    const wchar_t* uninstall;
    const wchar_t* keep;
    const wchar_t* font_family;
};

const Labels& labels(Lang lang);
Lang system_lang();

struct Model {
    Screen screen = Screen::Install;
    Lang lang = Lang::En;
    std::wstring product;
    std::wstring version;
    std::wstring destination;
    std::wstring status;
    std::wstring message;
    int step = 0;
    int total = 0;
    bool run_after = true;
    int hot = -1;    // Item under the pointer, -1 when none
    int focus = -1;  // Item with the keyboard focus, -1 when none
    bool pressed = false;
};

constexpr int kClientWidth = 520;
constexpr int kClientHeight = 320;

Gdiplus::SizeF client_size(float scale);
Gdiplus::RectF item_rect(Item item, float scale);

// The items the pointer and the keyboard reach on one screen, in paint order.
std::vector<Item> screen_items(Screen screen);

// Paints the whole client area. `icon` may be null; the brand tile stands in.
void paint(Gdiplus::Graphics& graphics, const Model& model, float scale,
           Gdiplus::Image* icon);

}  // namespace zui_tool::setup_ui
