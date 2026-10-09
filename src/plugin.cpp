// Total Commander lister plugin (WLX): markdown -> md4c -> litehtml -> GDI+.
// No browser engine, so opening a file costs a parse + layout instead of a Chromium start-up.
#include "view.h"
#include <string>

namespace
{
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
        DisableThreadLibraryCalls(module);
    return TRUE;
}

extern "C"
{
    HWND __stdcall ListLoadW(HWND parent, wchar_t* file, int /*flags*/)
    {
        HWND hwnd = mdlite::create_view(parent, file);
        if(hwnd)
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
        return mdlite::view_load(plugin, file) ? 0 : 1; // LISTPLUGIN_OK / LISTPLUGIN_ERROR
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

    // Lister's own Find (menu, F7) shows the plugin's find bar instead of the stock dialog.
    int __stdcall ListSearchDialog(HWND plugin, int find_next)
    {
        mdlite::view_find(plugin, find_next != 0);
        return 0; // LISTPLUGIN_OK
    }

    void __stdcall ListGetDetectString(char* out, int maxlen)
    {
        strncpy_s(out, maxlen, "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\"", _TRUNCATE);
    }
}
