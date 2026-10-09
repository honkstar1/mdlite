// Standalone viewer: mdlite.exe <file.md>. Hosts the same view as the Total Commander plugin in a top-level window.
#include "view.h"
#include <commdlg.h>
#include <shellapi.h>
#include <string>

namespace
{
    constexpr wchar_t kFrameClass[] = L"MDLiteFrame";
    HWND              g_view        = nullptr;

    void set_title(HWND frame, const std::wstring& path)
    {
        auto name = path.substr(path.find_last_of(L"\\/") + 1);
        SetWindowTextW(frame, (name + L" - mdlite").c_str());
    }

    std::wstring full_path(const wchar_t* path)
    {
        wchar_t buf[MAX_PATH * 4];
        DWORD   n = GetFullPathNameW(path, (DWORD) std::size(buf), buf, nullptr);
        return n > 0 && n < std::size(buf) ? std::wstring(buf) : std::wstring(path);
    }

    bool ask_for_file(std::wstring& out)
    {
        wchar_t       buf[MAX_PATH * 4] = L"";
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.lpstrFilter = L"Markdown\0*.md;*.markdown;*.mkd;*.mkdn\0All files\0*.*\0";
        ofn.lpstrFile   = buf;
        ofn.nMaxFile    = (DWORD) std::size(buf);
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if(!GetOpenFileNameW(&ofn))
            return false;
        out = buf;
        return true;
    }

    LRESULT CALLBACK frame_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch(msg)
        {
        case WM_SIZE:
            if(g_view)
                MoveWindow(g_view, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_SETFOCUS:
            if(g_view)
                SetFocus(g_view);
            return 0;
        case WM_KEYDOWN: // keys the view did not handle
            if(wp == VK_ESCAPE)
                DestroyWindow(hwnd);
            return 0;
        case WM_DROPFILES:
            {
                HDROP   drop = (HDROP) wp;
                wchar_t path[MAX_PATH * 4];
                if(DragQueryFileW(drop, 0, path, (UINT) std::size(path)) && mdlite::view_load(g_view, path))
                    set_title(hwnd, path);
                DragFinish(drop);
                return 0;
            }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show)
{
    std::wstring path;
    int          argc = 0;
    wchar_t**    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(argc > 1)
        path = full_path(argv[1]);
    LocalFree(argv);
    if(path.empty() && !ask_for_file(path))
        return 0;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = frame_proc;
    wc.hInstance     = instance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kFrameClass;
    RegisterClassExW(&wc);

    HDC   screen = GetDC(nullptr);
    float scale  = GetDeviceCaps(screen, LOGPIXELSY) / 96.0f;
    ReleaseDC(nullptr, screen);
    HWND frame = CreateWindowExW(WS_EX_ACCEPTFILES, kFrameClass, L"mdlite", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, (int) (960 * scale), (int) (900 * scale), nullptr, nullptr, instance,
                                 nullptr);
    if(!frame)
        return 1;

    g_view = mdlite::create_view(frame, path.c_str());
    if(!g_view)
    {
        MessageBoxW(frame, (L"Cannot open " + path).c_str(), L"mdlite", MB_ICONERROR);
        DestroyWindow(frame);
        return 1;
    }
    set_title(frame, path);
    ShowWindow(frame, show);
    SetFocus(g_view);

    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int) msg.wParam;
}
