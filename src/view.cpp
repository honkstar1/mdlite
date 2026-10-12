// Markdown view window: a litehtml document with scrolling, links, Ctrl+wheel zoom, a find bar and text selection.
// Shared by the Total Commander plugin and mdlite.exe.
#include "view.h"
#include "core.h"
#include <commctrl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mdlite
{
    namespace
    {
        constexpr wchar_t  kClassName[]   = L"MDLiteView";
        constexpr float    kMinZoom       = 0.5f;
        constexpr float    kMaxZoom       = 4.0f;
        constexpr float    kZoomStep      = 1.1f;
        constexpr UINT_PTR kZoomHintTimer = 1;
        constexpr UINT_PTR kAutoScrollTimer = 2;

        // Process-wide, so the zoom survives Lister switching files and, in Total Commander, reopening Lister.
        float g_zoom = 1.0f;

        // The module this code is linked into: the .wlx64 or mdlite.exe.
        HINSTANCE module()
        {
            return reinterpret_cast<HINSTANCE>(&__ImageBase);
        }

        struct Palette
        {
            COLORREF page, panel, fg, border;
        };

        Palette palette(bool dark)
        {
            return dark ? Palette{RGB(0x1e, 0x1e, 0x1e), RGB(0x2a, 0x2a, 0x2a), RGB(0xd4, 0xd4, 0xd4), RGB(0x3c, 0x3c, 0x3c)}
                        : Palette{RGB(0xff, 0xff, 0xff), RGB(0xf3, 0xf4, 0xf6), RGB(0x1f, 0x23, 0x28), RGB(0xd0, 0xd7, 0xde)};
        }

        // A text element of the document and where its text sits in View::text.
        struct Segment
        {
            litehtml::element::ptr el;
            size_t                 start = 0;
            size_t                 len   = 0;
            std::wstring           drawn; // what litehtml draws; differs from the text for whitespace
        };

        struct Match
        {
            size_t                          start = 0, end = 0;
            std::vector<litehtml::position> rects; // document coordinates
        };

        struct View
        {
            HWND                    hwnd = nullptr;
            litehtml::document::ptr doc;
            std::wstring            path;
            std::string             markdown;
            bool                    dark       = false;
            int                     scroll     = 0;
            int                     doc_height = 0;
            HDC                     memdc      = nullptr;
            HBITMAP                 bmp        = nullptr;
            HGDIOBJ                 old_bmp    = nullptr;
            int                     bw = 0, bh = 0;
            bool                    tracking = false;
            HHOOK                   key_hook = nullptr; // sees Esc before Lister does, see esc_hook

            float     dpi_scale       = 1.0f;
            HFONT     ui_font         = nullptr;
            int       ui_line_h       = 16;
            HBRUSH    panel_brush     = nullptr;
            HBRUSH    page_brush      = nullptr;
            ULONGLONG zoom_hint_until = 0;

            // Find bar and search state.
            HWND                 find_edit    = nullptr;
            HWND                 find_label   = nullptr;
            bool                 find_visible = false;
            RECT                 find_panel{};
            int                  find_anchor = 0; // document y the search starts from
            std::wstring         term;
            std::wstring         text;            // lowercased document text, '\n' between blocks
            std::wstring         text_raw;        // same text with its original case, for copying
            std::vector<Segment> segments;
            bool                 text_valid = false;
            std::vector<Match>   matches;
            bool                 rects_valid = false;
            int                  current     = -1;

            // Mouse selection, as a range of offsets into text.
            size_t                          sel_anchor = 0, sel_caret = 0;
            bool                            dragging   = false;
            bool                            drag_moved = false;
            POINT                           drag_origin{};
            POINT                           drag_last{}; // client coordinates, for auto-scroll
            std::vector<litehtml::position> sel_rects;
            bool                            sel_valid = false;
        };

        View* view_of(HWND h)
        {
            return reinterpret_cast<View*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }

        std::wstring dir_of(const std::wstring& path)
        {
            auto pos = path.find_last_of(L"\\/");
            return pos == std::wstring::npos ? L"." : path.substr(0, pos);
        }

        std::wstring widen(const std::string& s)
        {
            int          n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
            return w;
        }

        std::string narrow(const std::wstring& w)
        {
            int         n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), s.data(), n, nullptr, nullptr);
            return s;
        }

        int scaled(View* v, int px)
        {
            return (int) std::lround(px * v->dpi_scale);
        }

        // Per-document state that lives on the shared container.
        void activate(View* v, int w, int h)
        {
            auto& c      = shared_container();
            c.base_dir   = dir_of(v->path);
            c.viewport_w = w;
            c.viewport_h = h;
        }

        int client_width(HWND h)
        {
            RECT rc;
            GetClientRect(h, &rc);
            return rc.right;
        }

        int client_height(HWND h)
        {
            RECT rc;
            GetClientRect(h, &rc);
            return rc.bottom;
        }

        void update_scrollbar(View* v)
        {
            int page  = client_height(v->hwnd);
            int max   = v->doc_height > page ? v->doc_height - page : 0;
            v->scroll = std::max(0, std::min(v->scroll, max));
            SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, v->doc_height > 0 ? v->doc_height - 1 : 0,
                          (UINT) page, v->scroll, 0};
            SetScrollInfo(v->hwnd, SB_VERT, &si, TRUE);
        }

        // Lay the existing document out at the current width. The scrollbar appearing or disappearing changes
        // the client width, so settle in at most two passes.
        void relayout(View* v)
        {
            if(!v->doc)
                return;
            for(int pass = 0; pass < 2; pass++)
            {
                int w = client_width(v->hwnd);
                activate(v, w, client_height(v->hwnd));
                v->doc->render(w);
                v->doc_height = v->doc->height();
                update_scrollbar(v);
                if(client_width(v->hwnd) == w)
                    break;
            }
            v->rects_valid = false;
            v->sel_valid   = false;
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        void scroll_to(View* v, int pos)
        {
            int page = client_height(v->hwnd);
            int max  = v->doc_height > page ? v->doc_height - page : 0;
            pos      = std::max(0, std::min(pos, max));
            if(pos == v->scroll)
                return;
            v->scroll = pos;
            update_scrollbar(v);
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        // --- search -------------------------------------------------------------------------------------------

        void add_break(View* v)
        {
            // Cell separator at the end of a row; it belongs to no segment, unlike a tab in preformatted text.
            while(!v->text.empty() && v->text.back() == L'\t' &&
                  (v->segments.empty() || v->segments.back().start + v->segments.back().len < v->text.size()))
                v->text.pop_back();
            if(!v->text.empty() && v->text.back() != L'\n')
                v->text += L'\n';
        }

        // Flattens the document into View::text, one segment per word or space. Block boundaries become '\n' so a
        // match never spans two paragraphs; table cells are separated by a tab. Runs of whitespace collapse to one
        // space like they render, except in preformatted text, which is kept as is.
        void collect_text(View* v, const litehtml::element::ptr& el)
        {
            if(el->css().get_display() == litehtml::display_none)
                return;
            if(el->is_text())
            {
                std::string utf8;
                el->get_text(utf8);
                if(utf8.empty())
                    return;
                Segment s;
                s.el    = el;
                s.start = v->text.size();
                auto ws = el->css().get_white_space();
                bool is_space = el->is_white_space() || utf8.find_first_not_of(" \t\r\n") == std::string::npos;
                if(is_space && (ws == litehtml::white_space_pre || ws == litehtml::white_space_pre_wrap))
                {
                    utf8.erase(std::remove(utf8.begin(), utf8.end(), '\r'), utf8.end());
                    if(utf8.empty())
                        return;
                    s.len   = utf8.size();
                    s.drawn = utf8 == "\n" ? L"" : utf8 == "\t" ? L"    " : widen(utf8);
                    v->text += widen(utf8);
                } else if(is_space)
                {
                    if(v->text.empty() || v->text.back() == L' ' || v->text.back() == L'\n' || v->text.back() == L'\t')
                        return;
                    s.len   = 1;
                    s.drawn = utf8 == "\t" ? L"    " : (utf8 == "\n" || utf8 == "\r") ? L"" : L" ";
                    v->text += L' ';
                } else
                {
                    s.drawn  = widen(utf8);
                    s.len    = s.drawn.size();
                    v->text += s.drawn;
                }
                v->segments.push_back(std::move(s));
                return;
            }
            if(el->is_break())
            {
                add_break(v);
                return;
            }
            bool cell  = el->css().get_display() == litehtml::display_table_cell;
            bool block = !el->is_inline() && !cell;
            if(block)
                add_break(v);
            for(const auto& child : el->children())
                collect_text(v, child);
            if(block)
                add_break(v);
            else if(cell && !v->text.empty() && v->text.back() != L'\n' && v->text.back() != L'\t')
                v->text += L'\t';
        }

        void ensure_text(View* v)
        {
            if(v->text_valid || !v->doc)
                return;
            v->text.clear();
            v->segments.clear();
            collect_text(v, v->doc->root());
            v->text_raw = v->text;
            CharLowerBuffW(v->text.data(), (DWORD) v->text.size()); // keeps the length, so offsets match text_raw
            v->text_valid = true;
        }

        // Case-insensitive, non-overlapping.
        void compute_matches(View* v)
        {
            v->matches.clear();
            v->rects_valid = false;
            v->current     = -1;
            if(v->term.empty() || !v->doc)
                return;
            ensure_text(v);
            std::wstring needle = v->term;
            CharLowerBuffW(needle.data(), (DWORD) needle.size());
            for(size_t pos = v->text.find(needle); pos != std::wstring::npos;
                pos        = v->text.find(needle, pos + needle.size()))
                v->matches.push_back({pos, pos + needle.size(), {}});
        }

        float text_width(const std::wstring& s, litehtml::uint_ptr font)
        {
            return s.empty() || !font ? 0.0f : (float) shared_container().text_width(narrow(s).c_str(), font);
        }

        // Rectangles (document coordinates) covering text[start, end), one per word.
        void range_rects(View* v, size_t start, size_t end, std::vector<litehtml::position>& out)
        {
            out.clear();
            auto it = std::upper_bound(v->segments.begin(), v->segments.end(), start,
                                       [](size_t pos, const Segment& s) { return pos < s.start; });
            if(it != v->segments.begin())
                --it;
            for(; it != v->segments.end() && it->start < end; ++it)
            {
                if(it->start + it->len <= start)
                    continue;
                size_t      a     = std::max(start, it->start) - it->start;
                size_t      b     = std::min(end, it->start + it->len) - it->start;
                auto        place = it->el->get_placement();
                auto        font  = it->el->css().get_font();
                const auto& d     = it->drawn;
                float       x0    = a == 0 ? 0.0f : text_width(d.substr(0, std::min(a, d.size())), font);
                float       x1    = b == it->len ? (float) place.width : text_width(d.substr(0, std::min(b, d.size())), font);
                if(x1 > x0)
                    out.emplace_back(place.x + x0, place.y, x1 - x0, place.height);
            }
        }

        void ensure_rects(View* v)
        {
            if(v->rects_valid)
                return;
            v->rects_valid = true;
            for(auto& m : v->matches)
                range_rects(v, m.start, m.end, m.rects);
        }

        void update_label(View* v)
        {
            if(!v->find_label)
                return;
            wchar_t buf[64] = L"";
            if(!v->term.empty())
            {
                if(v->matches.empty())
                    wcscpy_s(buf, L"No results");
                else
                    swprintf_s(buf, L"%d of %d", v->current + 1, (int) v->matches.size());
            }
            SetWindowTextW(v->find_label, buf);
        }

        void reveal_current(View* v)
        {
            if(v->current < 0)
                return;
            ensure_rects(v);
            const auto& r = v->matches[v->current].rects;
            if(r.empty())
                return;
            int top     = (int) r.front().y;
            int bottom  = (int) r.back().bottom();
            int page    = client_height(v->hwnd);
            int covered = v->find_visible ? v->find_panel.bottom : 0;
            if(top < v->scroll + covered || bottom > v->scroll + page)
                scroll_to(v, top - page / 3);
        }

        // Moves to the next (or previous) match, wrapping around. Without a current match, starts from the anchor.
        void step(View* v, bool backwards)
        {
            int n = (int) v->matches.size();
            if(n > 0)
            {
                if(v->current < 0)
                {
                    ensure_rects(v);
                    auto top = [&](int i) {
                        return v->matches[i].rects.empty() ? 0.0f : (float) v->matches[i].rects.front().y;
                    };
                    v->current = backwards ? n - 1 : 0;
                    for(int i = 0; i < n; i++)
                    {
                        int k = backwards ? n - 1 - i : i;
                        if(backwards ? top(k) < v->find_anchor : top(k) >= v->find_anchor)
                        {
                            v->current = k;
                            break;
                        }
                    }
                } else
                    v->current = (v->current + (backwards ? n - 1 : 1)) % n;
                reveal_current(v);
            }
            update_label(v);
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        void on_term_changed(View* v)
        {
            int          len = GetWindowTextLengthW(v->find_edit);
            std::wstring term(len, L'\0');
            GetWindowTextW(v->find_edit, term.data(), len + 1);
            v->term = term;
            compute_matches(v);
            step(v, false);
        }

        void layout_find(View* v)
        {
            if(!v->find_edit)
                return;
            int pad     = scaled(v, 6);
            int margin  = scaled(v, 8);
            int h       = v->ui_line_h + scaled(v, 8);
            int edit_w  = scaled(v, 240);
            int label_w = scaled(v, 90);
            int right   = client_width(v->hwnd) - margin;
            int edit_x  = std::max(label_w + 2 * pad, right - pad - edit_w);
            int label_x = edit_x - pad - label_w;
            MoveWindow(v->find_edit, edit_x, margin + pad, std::min(edit_w, right - pad - edit_x), h, TRUE);
            MoveWindow(v->find_label, label_x, margin + pad, label_w, h, TRUE);
            v->find_panel = {label_x - pad, margin, right, margin + h + 2 * pad};
        }

        void hide_find(View* v);

        LRESULT CALLBACK edit_proc(HWND e, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto  old  = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(e, GWLP_USERDATA));
            HWND  view = GetParent(e);
            View* v    = view_of(view);
            if(v)
            {
                bool ctrl = GetKeyState(VK_CONTROL) < 0;
                switch(msg)
                {
                case WM_KEYDOWN:
                    switch(wp)
                    {
                    case VK_RETURN:
                    case VK_F3: step(v, GetKeyState(VK_SHIFT) < 0); return 0;
                    case VK_ESCAPE: hide_find(v); return 0;
                    case VK_UP:
                    case VK_DOWN:
                    case VK_PRIOR:
                    case VK_NEXT: return SendMessageW(view, msg, wp, lp);
                    case 'A':
                    case 'F':
                        if(ctrl)
                        {
                            SendMessageW(e, EM_SETSEL, 0, -1);
                            return 0;
                        }
                        break;
                    case VK_OEM_PLUS:
                    case VK_ADD:
                    case VK_OEM_MINUS:
                    case VK_SUBTRACT:
                    case '0':
                    case VK_NUMPAD0:
                        if(ctrl)
                            return SendMessageW(view, msg, wp, lp);
                        break;
                    }
                    break;
                case WM_CHAR:
                    // Enter, Esc, Ctrl+A, Ctrl+F: handled on key down, swallow the beep.
                    if(wp == L'\r' || wp == 27 || wp == 1 || wp == 6)
                        return 0;
                    break;
                case WM_MOUSEWHEEL: return SendMessageW(view, msg, wp, lp);
                }
            }
            return CallWindowProcW(old, e, msg, wp, lp);
        }

        void create_find_controls(View* v)
        {
            v->find_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX, 0, 0,
                                            0, 0, v->hwnd, nullptr, module(), nullptr);
            v->find_edit  = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, v->hwnd,
                                            nullptr, module(), nullptr);
            SendMessageW(v->find_label, WM_SETFONT, (WPARAM) v->ui_font, FALSE);
            SendMessageW(v->find_edit, WM_SETFONT, (WPARAM) v->ui_font, FALSE);
            SendMessageW(v->find_edit, EM_SETCUEBANNER, TRUE, (LPARAM) L"Find");
            SetWindowLongPtrW(v->find_edit, GWLP_USERDATA, GetWindowLongPtrW(v->find_edit, GWLP_WNDPROC));
            SetWindowLongPtrW(v->find_edit, GWLP_WNDPROC, (LONG_PTR) edit_proc);
        }

        void show_find(View* v)
        {
            if(!v->find_edit)
                create_find_controls(v);
            if(!v->find_visible)
            {
                v->find_visible = true;
                v->find_anchor  = v->scroll;
                layout_find(v);
                ShowWindow(v->find_label, SW_SHOWNA);
                ShowWindow(v->find_edit, SW_SHOWNA);
                on_term_changed(v); // the edit keeps the previous term
            }
            SetFocus(v->find_edit);
            SendMessageW(v->find_edit, EM_SETSEL, 0, -1);
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        void hide_find(View* v)
        {
            if(!v->find_visible)
                return;
            v->find_visible = false;
            ShowWindow(v->find_edit, SW_HIDE);
            ShowWindow(v->find_label, SW_HIDE);
            v->matches.clear();
            v->current = -1;
            SetFocus(v->hwnd);
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        void find_next(View* v, bool backwards)
        {
            if(!v->find_edit || GetWindowTextLengthW(v->find_edit) == 0)
                show_find(v);
            else if(!v->find_visible)
                show_find(v); // recomputes and selects the first match below the current position
            else
                step(v, backwards);
        }

        // --- selection ----------------------------------------------------------------------------------------

        bool has_selection(const View* v)
        {
            return v->sel_anchor != v->sel_caret;
        }

        void set_selection(View* v, size_t anchor, size_t caret)
        {
            if(anchor == v->sel_anchor && caret == v->sel_caret)
                return;
            v->sel_anchor = anchor;
            v->sel_caret  = caret;
            v->sel_valid  = false;
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        // The text offset nearest to a document point: the closest word, then the closest character boundary in it.
        size_t offset_at(View* v, int x, int y)
        {
            ensure_text(v);
            const Segment* best = nullptr;
            float          best_x = 0, best_w = 0, best_dy = 0, best_dx = 0;
            for(const auto& s : v->segments)
            {
                if(s.drawn.empty())
                    continue;
                auto p = s.el->get_placement();
                float px = (float) p.x, py = (float) p.y, pw = (float) p.width, ph = (float) p.height;
                if(pw <= 0 || ph <= 0)
                    continue;
                float dy = y < py ? py - y : y > py + ph ? y - (py + ph) : 0.0f;
                float dx = x < px ? px - x : x > px + pw ? x - (px + pw) : 0.0f;
                if(!best || dy < best_dy || (dy == best_dy && dx < best_dx))
                {
                    best   = &s;
                    best_x = px;
                    best_w = pw;
                    best_dy = dy;
                    best_dx = dx;
                }
            }
            if(!best)
                return 0;
            float rx = x - best_x;
            if(rx <= 0)
                return best->start;
            if(rx >= best_w)
                return best->start + best->len;
            const auto& d = best->drawn;
            if(d.size() != best->len)
                return rx < best_w / 2 ? best->start : best->start + best->len;
            auto  font = best->el->css().get_font();
            float prev = 0.0f;
            for(size_t i = 1; i <= d.size(); i++)
            {
                float w = text_width(d.substr(0, i), font);
                if(rx < (prev + w) / 2)
                    return best->start + i - 1;
                prev = w;
            }
            return best->start + d.size();
        }

        bool over_text(View* v, int x, int y)
        {
            ensure_text(v);
            for(const auto& s : v->segments)
            {
                if(s.drawn.empty())
                    continue;
                auto p = s.el->get_placement();
                float px = (float) p.x, py = (float) p.y;
                if(x >= px && x < px + (float) p.width && y >= py && y < py + (float) p.height)
                    return true;
            }
            return false;
        }

        void select_word(View* v, size_t off)
        {
            ensure_text(v);
            const auto& t = v->text_raw;
            if(t.empty())
                return;
            off         = std::min(off, t.size() - 1);
            auto kind   = [&](size_t i) { return t[i] == L'\n' ? 0 : (iswalnum(t[i]) || t[i] == L'_') ? 1 : 2; };
            int  k      = kind(off);
            size_t a = off, b = off + 1;
            while(a > 0 && kind(a - 1) == k)
                a--;
            while(b < t.size() && kind(b) == k)
                b++;
            set_selection(v, a, b);
        }

        void select_all(View* v)
        {
            ensure_text(v);
            set_selection(v, 0, v->text_raw.size());
        }

        void clear_selection(View* v)
        {
            set_selection(v, 0, 0);
        }

        bool copy_selection(View* v)
        {
            if(!has_selection(v) || !v->text_valid)
                return false;
            size_t       lo = std::min(v->sel_anchor, v->sel_caret), hi = std::max(v->sel_anchor, v->sel_caret);
            hi = std::min(hi, v->text_raw.size());
            if(lo >= hi)
                return false;
            std::wstring out;
            for(size_t i = lo; i < hi; i++)
            {
                if(v->text_raw[i] == L'\n')
                    out += L'\r';
                out += v->text_raw[i];
            }
            if(!OpenClipboard(v->hwnd))
                return false;
            EmptyClipboard();
            bool    ok  = false;
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (out.size() + 1) * sizeof(wchar_t));
            if(mem)
            {
                memcpy(GlobalLock(mem), out.c_str(), (out.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(mem);
                ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
                if(!ok)
                    GlobalFree(mem);
            }
            CloseClipboard();
            return ok;
        }

        // Moves the selection end to the last known mouse position.
        void drag_selection(View* v)
        {
            size_t caret = offset_at(v, v->drag_last.x, v->drag_last.y + v->scroll);
            set_selection(v, v->sel_anchor, caret);
        }

        // Lister closes itself on Esc before the key reaches the plugin window, so a selection could never be
        // cleared with it. A thread keyboard hook runs first: with a selection in the focused view, Esc clears it
        // and is swallowed; otherwise it passes on and closes Lister as usual.
        LRESULT CALLBACK esc_hook(int code, WPARAM wp, LPARAM lp)
        {
            if(code == HC_ACTION && wp == VK_ESCAPE && !(lp & (1 << 31)))
            {
                HWND    focus = GetFocus();
                wchar_t cls[32];
                if(focus && GetClassNameW(focus, cls, (int) std::size(cls)) && wcscmp(cls, kClassName) == 0)
                {
                    View* v = view_of(focus);
                    if(v && has_selection(v))
                    {
                        clear_selection(v);
                        return 1;
                    }
                }
            }
            return CallNextHookEx(nullptr, code, wp, lp);
        }

        void stop_drag(HWND hwnd, View* v)
        {
            if(!v->dragging)
                return;
            v->dragging = false;
            KillTimer(hwnd, kAutoScrollTimer);
            if(GetCapture() == hwnd)
                ReleaseCapture();
        }

        // --- document -----------------------------------------------------------------------------------------

        void update_theme(View* v)
        {
            if(v->panel_brush)
                DeleteObject(v->panel_brush);
            if(v->page_brush)
                DeleteObject(v->page_brush);
            auto p         = palette(v->dark);
            v->panel_brush = CreateSolidBrush(p.panel);
            v->page_brush  = CreateSolidBrush(p.page);
        }

        bool rebuild(View* v)
        {
            v->segments.clear();
            v->text.clear();
            v->text_valid = false;
            v->matches.clear();
            activate(v, client_width(v->hwnd), client_height(v->hwnd));
            v->doc = build_document(shared_container(), v->markdown, client_width(v->hwnd), v->dark, g_zoom);
            if(!v->doc)
                return false;
            relayout(v);
            if(v->find_visible)
            {
                int keep = v->current;
                compute_matches(v);
                if(keep < (int) v->matches.size())
                    v->current = keep;
                update_label(v);
            }
            return true;
        }

        bool load_file(View* v, const wchar_t* path)
        {
            v->path = path;
            if(!read_file(path, v->markdown))
                return false;
            v->dark    = use_dark_theme();
            v->scroll  = 0;
            v->current = -1;
            stop_drag(v->hwnd, v);
            v->sel_anchor = v->sel_caret = 0;
            v->sel_valid                 = false;
            update_theme(v);
            if(!rebuild(v))
                return false;
            if(v->find_visible)
            {
                v->find_anchor = 0;
                v->current     = -1;
                step(v, false);
            }
            return true;
        }

        void set_zoom(View* v, float zoom, int anchor_y)
        {
            zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
            if(std::fabs(zoom - 1.0f) < 0.02f)
                zoom = 1.0f; // repeated steps drift; land on 100% exactly
            if(!v->doc || std::fabs(zoom - g_zoom) < 0.001f)
                return;
            // Keep the document point under anchor_y in place.
            double frac = v->doc_height > 0 ? (v->scroll + anchor_y) / (double) v->doc_height : 0.0;
            g_zoom      = zoom;
            rebuild(v);
            v->scroll = (int) std::lround(frac * v->doc_height) - anchor_y;
            update_scrollbar(v);
            v->zoom_hint_until = GetTickCount64() + 1200;
            SetTimer(v->hwnd, kZoomHintTimer, 1250, nullptr);
            InvalidateRect(v->hwnd, nullptr, FALSE);
        }

        void open_link(const std::string& url)
        {
            if(url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || url.rfind("mailto:", 0) == 0)
                ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            // Relative links and #anchors are not handled (md4c emits no heading ids).
        }

        // --- painting -----------------------------------------------------------------------------------------

        void ensure_backbuffer(View* v, HDC like, int w, int h)
        {
            if(v->memdc && v->bw == w && v->bh == h)
                return;
            if(v->memdc)
            {
                SelectObject(v->memdc, v->old_bmp);
                DeleteObject(v->bmp);
                DeleteDC(v->memdc);
            }
            v->memdc   = CreateCompatibleDC(like);
            v->bmp     = CreateCompatibleBitmap(like, w, h);
            v->old_bmp = SelectObject(v->memdc, v->bmp);
            v->bw      = w;
            v->bh      = h;
        }

        void paint_selection(View* v, int height)
        {
            if(!has_selection(v))
                return;
            if(!v->sel_valid)
            {
                ensure_text(v);
                size_t lo = std::min(v->sel_anchor, v->sel_caret), hi = std::max(v->sel_anchor, v->sel_caret);
                range_rects(v, lo, hi, v->sel_rects);
                v->sel_valid = true;
            }
            Gdiplus::Graphics   g(v->memdc);
            Gdiplus::SolidBrush brush(Gdiplus::Color(v->dark ? 120 : 90, 51, 144, 255));
            for(const auto& r : v->sel_rects)
            {
                float y = (float) r.y - v->scroll;
                if(y + (float) r.height < 0 || y > height)
                    continue;
                g.FillRectangle(&brush, (float) r.x, y, (float) r.width, (float) r.height);
            }
        }

        void paint_highlights(View* v, int height)
        {
            if(v->matches.empty())
                return;
            ensure_rects(v);
            Gdiplus::Graphics   g(v->memdc);
            Gdiplus::SolidBrush other(Gdiplus::Color(96, 255, 210, 0));
            Gdiplus::SolidBrush current(Gdiplus::Color(170, 255, 140, 0));
            for(int i = 0; i < (int) v->matches.size(); i++)
                for(const auto& r : v->matches[i].rects)
                {
                    float y = (float) r.y - v->scroll;
                    if(y + (float) r.height < 0 || y > height)
                        continue;
                    g.FillRectangle(i == v->current ? &current : &other, (float) r.x, y, (float) r.width,
                                    (float) r.height);
                }
        }

        // Panel behind the find controls, and the transient zoom percentage.
        void paint_overlays(View* v)
        {
            auto p = palette(v->dark);
            if(v->find_visible)
            {
                FillRect(v->memdc, &v->find_panel, v->panel_brush);
                HBRUSH border = CreateSolidBrush(p.border);
                FrameRect(v->memdc, &v->find_panel, border);
                DeleteObject(border);
            }
            if(GetTickCount64() < v->zoom_hint_until)
            {
                wchar_t buf[32];
                swprintf_s(buf, L"Zoom %d%%", (int) std::lround(g_zoom * 100));
                HGDIOBJ old = SelectObject(v->memdc, v->ui_font);
                RECT    tr{0, 0, 0, 0};
                DrawTextW(v->memdc, buf, -1, &tr, DT_CALCRECT | DT_SINGLELINE);
                int  pad = scaled(v, 8), m = scaled(v, 8);
                RECT box{m, m, m + tr.right + 2 * pad, m + tr.bottom + pad};
                FillRect(v->memdc, &box, v->panel_brush);
                SetBkMode(v->memdc, TRANSPARENT);
                SetTextColor(v->memdc, p.fg);
                DrawTextW(v->memdc, buf, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(v->memdc, old);
            }
        }

        void paint(View* v)
        {
            PAINTSTRUCT ps;
            HDC         hdc = BeginPaint(v->hwnd, &ps);
            RECT        rc;
            GetClientRect(v->hwnd, &rc);
            if(rc.right > 0 && rc.bottom > 0)
            {
                ensure_backbuffer(v, hdc, rc.right, rc.bottom);
                FillRect(v->memdc, &rc, v->page_brush);
                if(v->doc)
                {
                    activate(v, rc.right, rc.bottom);
                    litehtml::position clip(0, 0, rc.right, rc.bottom);
                    v->doc->draw((litehtml::uint_ptr) v->memdc, 0, -v->scroll, &clip);
                    paint_selection(v, rc.bottom);
                    paint_highlights(v, rc.bottom);
                }
                paint_overlays(v);
                BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                       ps.rcPaint.bottom - ps.rcPaint.top, v->memdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            }
            EndPaint(v->hwnd, &ps);
        }

        auto redraw_all(HWND h)
        {
            return [h](const litehtml::position&) { InvalidateRect(h, nullptr, FALSE); };
        }

        // Returns true if the key was a view command.
        bool handle_key(View* v, WPARAM key)
        {
            int  page  = client_height(v->hwnd);
            bool ctrl  = GetKeyState(VK_CONTROL) < 0;
            bool shift = GetKeyState(VK_SHIFT) < 0;
            if(ctrl)
            {
                switch(key)
                {
                case 'F': show_find(v); return true;
                case 'C':
                case VK_INSERT: copy_selection(v); return true;
                case 'A': select_all(v); return true;
                case VK_OEM_PLUS:
                case VK_ADD: set_zoom(v, g_zoom * kZoomStep, 0); return true;
                case VK_OEM_MINUS:
                case VK_SUBTRACT: set_zoom(v, g_zoom / kZoomStep, 0); return true;
                case '0':
                case VK_NUMPAD0: set_zoom(v, 1.0f, 0); return true;
                }
            }
            switch(key)
            {
            case VK_UP: scroll_to(v, v->scroll - 40); return true;
            case VK_DOWN: scroll_to(v, v->scroll + 40); return true;
            case VK_PRIOR: scroll_to(v, v->scroll - page + 40); return true;
            case VK_NEXT:
            case VK_SPACE: scroll_to(v, v->scroll + page - 40); return true;
            case VK_HOME: scroll_to(v, 0); return true;
            case VK_END: scroll_to(v, v->doc_height); return true;
            case VK_F3: find_next(v, shift); return true;
            case VK_ESCAPE:
                if(v->find_visible)
                {
                    hide_find(v);
                    return true;
                }
                if(has_selection(v))
                {
                    clear_selection(v);
                    return true;
                }
                return false;
            }
            return false;
        }

        LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            View* v = view_of(hwnd);
            switch(msg)
            {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT:
                if(v)
                {
                    paint(v);
                    return 0;
                }
                break;
            case WM_SIZE:
                if(v && wp != SIZE_MINIMIZED)
                {
                    relayout(v);
                    layout_find(v);
                }
                return 0;
            case WM_MOUSEWHEEL:
                if(v)
                {
                    int delta = GET_WHEEL_DELTA_WPARAM(wp);
                    if(GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)
                    {
                        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                        ScreenToClient(hwnd, &pt);
                        set_zoom(v, g_zoom * std::pow(kZoomStep, delta / (float) WHEEL_DELTA),
                                 std::clamp((int) pt.y, 0, client_height(hwnd)));
                        return 0;
                    }
                    UINT lines = 3;
                    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                    scroll_to(v, v->scroll - delta * (int) lines * 20 / WHEEL_DELTA);
                    return 0;
                }
                break;
            case WM_VSCROLL:
                if(v)
                {
                    SCROLLINFO si{sizeof(si), SIF_ALL};
                    GetScrollInfo(hwnd, SB_VERT, &si);
                    int page = client_height(hwnd);
                    switch(LOWORD(wp))
                    {
                    case SB_LINEUP: scroll_to(v, v->scroll - 40); break;
                    case SB_LINEDOWN: scroll_to(v, v->scroll + 40); break;
                    case SB_PAGEUP: scroll_to(v, v->scroll - page + 40); break;
                    case SB_PAGEDOWN: scroll_to(v, v->scroll + page - 40); break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: scroll_to(v, si.nTrackPos); break;
                    case SB_TOP: scroll_to(v, 0); break;
                    case SB_BOTTOM: scroll_to(v, v->doc_height); break;
                    }
                    return 0;
                }
                break;
            case WM_KEYDOWN:
                if(v && handle_key(v, wp))
                    return 0;
                // Everything else (Esc, n/p for next/previous file, ...) belongs to the parent.
                PostMessageW(GetParent(hwnd), msg, wp, lp);
                return 0;
            case WM_COMMAND:
                if(v && (HWND) lp == v->find_edit && HIWORD(wp) == EN_CHANGE)
                {
                    on_term_changed(v);
                    return 0;
                }
                break;
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC:
                if(v)
                {
                    auto p = palette(v->dark);
                    SetTextColor((HDC) wp, p.fg);
                    SetBkColor((HDC) wp, msg == WM_CTLCOLOREDIT ? p.page : p.panel);
                    return (LRESULT) (msg == WM_CTLCOLOREDIT ? v->page_brush : v->panel_brush);
                }
                break;
            case WM_TIMER:
                if(wp == kAutoScrollTimer)
                {
                    if(v && v->dragging && v->drag_moved)
                    {
                        int h     = client_height(hwnd);
                        int delta = v->drag_last.y < 0 ? v->drag_last.y : v->drag_last.y > h ? v->drag_last.y - h : 0;
                        if(delta != 0)
                        {
                            scroll_to(v, v->scroll + (delta < 0 ? -1 : 1) * std::max(10, std::abs(delta) / 2));
                            drag_selection(v);
                        }
                    }
                    return 0;
                }
                if(wp == kZoomHintTimer)
                {
                    KillTimer(hwnd, kZoomHintTimer);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                break;
            case WM_MOUSEMOVE:
                if(v && v->doc)
                {
                    if(!v->tracking)
                    {
                        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                        TrackMouseEvent(&tme);
                        v->tracking = true;
                    }
                    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                    if(v->doc->on_mouse_over(x, y + v->scroll, x, y, redraw_all(hwnd)))
                        InvalidateRect(hwnd, nullptr, FALSE);
                    if(v->dragging)
                    {
                        v->drag_last = {x, y};
                        if(std::abs(x - v->drag_origin.x) > GetSystemMetrics(SM_CXDRAG) ||
                           std::abs(y - v->drag_origin.y) > GetSystemMetrics(SM_CYDRAG))
                            v->drag_moved = true;
                        if(v->drag_moved)
                            drag_selection(v);
                    }
                }
                return 0;
            case WM_MOUSELEAVE:
                if(v && v->doc)
                {
                    v->tracking = false;
                    if(v->doc->on_mouse_leave(redraw_all(hwnd)))
                        InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONDOWN:
                if(v && v->doc)
                {
                    SetFocus(hwnd);
                    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                    if(v->doc->on_lbutton_down(x, y + v->scroll, x, y, redraw_all(hwnd)))
                        InvalidateRect(hwnd, nullptr, FALSE);
                    size_t off = offset_at(v, x, y + v->scroll);
                    if(wp & MK_SHIFT)
                        set_selection(v, has_selection(v) ? v->sel_anchor : v->sel_caret, off);
                    else
                        v->sel_anchor = v->sel_caret = off; // a plain click clears the selection on release
                    v->dragging    = true;
                    v->drag_moved  = (wp & MK_SHIFT) != 0;
                    v->drag_origin = v->drag_last = {x, y};
                    SetCapture(hwnd);
                    SetTimer(hwnd, kAutoScrollTimer, 30, nullptr);
                    if(!(wp & MK_SHIFT))
                    {
                        v->sel_valid = false;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            case WM_LBUTTONDBLCLK:
                if(v && v->doc)
                {
                    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                    select_word(v, offset_at(v, x, y + v->scroll));
                    v->dragging   = true; // the following release must not clear the word or open a link
                    v->drag_moved = true;
                    v->drag_last  = {x, y};
                    SetCapture(hwnd);
                }
                return 0;
            case WM_CAPTURECHANGED:
                if(v && v->dragging)
                {
                    v->dragging = false;
                    KillTimer(hwnd, kAutoScrollTimer);
                }
                break;
            case WM_LBUTTONUP:
                if(v && v->doc)
                {
                    auto& c = shared_container();
                    c.clicked_link.clear();
                    int  x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                    bool selected = v->dragging && v->drag_moved; // dragging over a link selects, it does not click
                    stop_drag(hwnd, v);
                    if(v->doc->on_lbutton_up(x, y + v->scroll, x, y, redraw_all(hwnd)))
                        InvalidateRect(hwnd, nullptr, FALSE);
                    if(!c.clicked_link.empty() && !selected)
                        open_link(c.clicked_link);
                }
                return 0;
            case WM_SETCURSOR:
                if((HWND) wp == hwnd && LOWORD(lp) == HTCLIENT)
                {
                    bool  hand = shared_container().cursor == "pointer";
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(hwnd, &pt);
                    bool ibeam = !hand && v && v->doc && !(v->find_visible && PtInRect(&v->find_panel, pt)) &&
                                 over_text(v, pt.x, pt.y + v->scroll);
                    SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : ibeam ? IDC_IBEAM : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_NCDESTROY:
                if(v)
                {
                    if(v->memdc)
                    {
                        SelectObject(v->memdc, v->old_bmp);
                        DeleteObject(v->bmp);
                        DeleteDC(v->memdc);
                    }
                    if(v->key_hook)
                        UnhookWindowsHookEx(v->key_hook);
                    DeleteObject(v->ui_font);
                    DeleteObject(v->panel_brush);
                    DeleteObject(v->page_brush);
                    delete v;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        void register_class()
        {
            static bool done = false;
            if(done)
                return;
            WNDCLASSEXW wc{sizeof(wc)};
            wc.style         = CS_DBLCLKS;
            wc.lpfnWndProc   = wnd_proc;
            wc.hInstance     = module();
            wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kClassName;
            RegisterClassExW(&wc);
            done = true;
        }

        void create_ui_font(View* v)
        {
            HDC screen = GetDC(nullptr);
            int dpi    = GetDeviceCaps(screen, LOGPIXELSY); // same source as the container's pt_to_px
            v->dpi_scale = dpi / 96.0f;
            v->ui_font   = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                       L"Segoe UI");
            HGDIOBJ    old = SelectObject(screen, v->ui_font);
            TEXTMETRICW tm{};
            GetTextMetricsW(screen, &tm);
            v->ui_line_h = tm.tmHeight;
            SelectObject(screen, old);
            ReleaseDC(nullptr, screen);
        }
    } // namespace

    HWND create_view(HWND parent, const wchar_t* file)
    {
        register_class();
        RECT rc;
        GetClientRect(parent, &rc);
        HWND hwnd = CreateWindowExW(0, kClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0,
                                    rc.right, rc.bottom, parent, nullptr, module(), nullptr);
        if(!hwnd)
            return nullptr;
        auto* v = new View;
        v->hwnd = hwnd;
        create_ui_font(v);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) v);
        v->key_hook = SetWindowsHookExW(WH_KEYBOARD, esc_hook, nullptr, GetCurrentThreadId());
        if(!load_file(v, file))
        {
            DestroyWindow(hwnd);
            return nullptr;
        }
        return hwnd;
    }

    bool view_load(HWND view, const wchar_t* file)
    {
        View* v = view_of(view);
        return v && load_file(v, file);
    }

    bool view_copy(HWND view)
    {
        View* v = view_of(view);
        return v && copy_selection(v);
    }

    void view_select_all(HWND view)
    {
        if(View* v = view_of(view))
            select_all(v);
    }

    void view_find(HWND view, bool next)
    {
        if(View* v = view_of(view))
        {
            if(next)
                find_next(v, false);
            else
                show_find(v);
        }
    }
} // namespace mdlite
