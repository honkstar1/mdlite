// Total Commander lister plugin (WLX): markdown -> md4c -> litehtml -> GDI+.
// No browser engine, so opening a file costs a parse + layout instead of a Chromium start-up.
#include "view.h"
#include "core.h"
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
    struct ListDefaultParamStruct
    {
        int   size;
        DWORD plugin_interface_version_low, plugin_interface_version_high;
        char  default_ini_name[MAX_PATH]; // wincmd.ini
    };

    // Called once by Total Commander before the first ListLoad. default_ini_name is a plugin-specific name in
    // Total Commander's ini directory, not wincmd.ini itself, so look at that and at the wincmd.ini beside it.
    void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps)
    {
        std::wstring ini = ansi_to_wide(dps->default_ini_name);
        auto         pos = ini.find_last_of(L"\\/");
        std::wstring dir = pos == std::wstring::npos ? L"" : ini.substr(0, pos + 1);
        mdlite::set_settings_inis({ini, dir + L"wincmd.ini"});
    }

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

    // Lister's Edit > Copy and Select all.
    int __stdcall ListSendCommand(HWND plugin, int command, int /*parameter*/)
    {
        constexpr int lc_copy = 1, lc_selectall = 3;
        if(command == lc_copy)
            mdlite::view_copy(plugin);
        else if(command == lc_selectall)
            mdlite::view_select_all(plugin);
        else
            return 1; // LISTPLUGIN_ERROR
        return 0;
    }

    void __stdcall ListGetDetectString(char* out, int maxlen)
    {
        strncpy_s(out, maxlen, "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\"", _TRUNCATE);
    }
}
