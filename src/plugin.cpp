// Total Commander lister plugin (WLX): markdown -> md4c -> litehtml -> GDI+.
// No browser engine, so opening a file costs a parse + layout instead of a Chromium start-up.
#include "core.h"
#include <shellapi.h>
#include <windowsx.h>
#include <string>

namespace
{
    constexpr wchar_t kClassName[] = L"MDLiteView";
    HINSTANCE         g_instance   = nullptr;

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

    // Per-document state that lives on the shared container.
    void activate(View* v, int w, int h)
    {
        auto& c      = mdlite::shared_container();
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
        int page = client_height(v->hwnd);
        int max  = v->doc_height > page ? v->doc_height - page : 0;
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
        InvalidateRect(v->hwnd, nullptr, FALSE);
    }

    bool load_file(View* v, const wchar_t* path)
    {
        v->path = path;
        if(!mdlite::read_file(path, v->markdown))
            return false;
        v->dark   = mdlite::system_uses_dark_theme();
        v->scroll = 0;
        activate(v, client_width(v->hwnd), client_height(v->hwnd));
        v->doc = mdlite::build_document(mdlite::shared_container(), v->markdown, client_width(v->hwnd), v->dark);
        if(!v->doc)
            return false;
        relayout(v);
        return true;
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

    void open_link(const std::string& url)
    {
        if(url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || url.rfind("mailto:", 0) == 0)
        {
            int          n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            std::wstring w(n > 0 ? n - 1 : 0, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, w.data(), n);
            ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        // Relative links and #anchors are not handled in this prototype (md4c emits no heading ids).
    }

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

    void paint(View* v)
    {
        PAINTSTRUCT ps;
        HDC         hdc = BeginPaint(v->hwnd, &ps);
        RECT        rc;
        GetClientRect(v->hwnd, &rc);
        if(rc.right > 0 && rc.bottom > 0)
        {
            ensure_backbuffer(v, hdc, rc.right, rc.bottom);
            HBRUSH bg = CreateSolidBrush(v->dark ? RGB(0x1e, 0x1e, 0x1e) : RGB(0xff, 0xff, 0xff));
            FillRect(v->memdc, &rc, bg);
            DeleteObject(bg);
            if(v->doc)
            {
                activate(v, rc.right, rc.bottom);
                litehtml::position clip(0, 0, rc.right, rc.bottom);
                v->doc->draw((litehtml::uint_ptr) v->memdc, 0, -v->scroll, &clip);
            }
            BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, v->memdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        }
        EndPaint(v->hwnd, &ps);
    }

    auto redraw_all(HWND h)
    {
        return [h](const litehtml::position&) { InvalidateRect(h, nullptr, FALSE); };
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        View* v = view_of(hwnd);
        switch(msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if(v)
            {
                paint(v);
                return 0;
            }
            break;
        case WM_SIZE:
            if(v && wp != SIZE_MINIMIZED)
                relayout(v);
            return 0;
        case WM_MOUSEWHEEL:
            if(v)
            {
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                scroll_to(v, v->scroll - GET_WHEEL_DELTA_WPARAM(wp) * (int) lines * 20 / WHEEL_DELTA);
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
            if(v)
            {
                int page = client_height(hwnd);
                switch(wp)
                {
                case VK_UP: scroll_to(v, v->scroll - 40); return 0;
                case VK_DOWN: scroll_to(v, v->scroll + 40); return 0;
                case VK_PRIOR: scroll_to(v, v->scroll - page + 40); return 0;
                case VK_NEXT:
                case VK_SPACE: scroll_to(v, v->scroll + page - 40); return 0;
                case VK_HOME: scroll_to(v, 0); return 0;
                case VK_END: scroll_to(v, v->doc_height); return 0;
                }
            }
            // Everything else (Esc, n/p for next/previous file, ...) belongs to Lister.
            PostMessageW(GetParent(hwnd), msg, wp, lp);
            return 0;
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
            }
            return 0;
        case WM_LBUTTONUP:
            if(v && v->doc)
            {
                auto& c = mdlite::shared_container();
                c.clicked_link.clear();
                int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
                if(v->doc->on_lbutton_up(x, y + v->scroll, x, y, redraw_all(hwnd)))
                    InvalidateRect(hwnd, nullptr, FALSE);
                if(!c.clicked_link.empty())
                    open_link(c.clicked_link);
            }
            return 0;
        case WM_SETCURSOR:
            if(LOWORD(lp) == HTCLIENT)
            {
                bool hand = mdlite::shared_container().cursor == "pointer";
                SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
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
        wc.hInstance     = g_instance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        done = true;
    }

    std::wstring ansi_to_wide(const char* s)
    {
        int          n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if(n > 0)
            MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
        g_instance = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C"
{
    HWND __stdcall ListLoadW(HWND parent, wchar_t* file, int /*flags*/)
    {
        register_class();
        RECT rc;
        GetClientRect(parent, &rc);
        HWND hwnd = CreateWindowExW(0, kClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0,
                                    rc.right, rc.bottom, parent, nullptr, g_instance, nullptr);
        if(!hwnd)
            return nullptr;
        auto* v = new View;
        v->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) v);
        if(!load_file(v, file))
        {
            DestroyWindow(hwnd);
            return nullptr;
        }
        SetFocus(hwnd);
        return hwnd;
    }

    HWND __stdcall ListLoad(HWND parent, char* file, int flags)
    {
        std::wstring w = ansi_to_wide(file);
        return ListLoadW(parent, w.data(), flags);
    }

    int __stdcall ListLoadNextW(HWND /*parent*/, HWND plugin, wchar_t* file, int /*flags*/)
    {
        View* v = view_of(plugin);
        return v && load_file(v, file) ? 0 : 1; // LISTPLUGIN_OK / LISTPLUGIN_ERROR
    }

    int __stdcall ListLoadNext(HWND parent, HWND plugin, char* file, int flags)
    {
        std::wstring w = ansi_to_wide(file);
        return ListLoadNextW(parent, plugin, w.data(), flags);
    }

    void __stdcall ListCloseWindow(HWND plugin)
    {
        DestroyWindow(plugin);
    }

    void __stdcall ListGetDetectString(char* out, int maxlen)
    {
        strncpy_s(out, maxlen, "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\"", _TRUNCATE);
    }
}
