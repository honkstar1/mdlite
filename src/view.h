#pragma once
#include <windows.h>

// The markdown view window, shared by the Total Commander plugin and mdlite.exe.
namespace mdlite
{
    // Creates the view as a child filling parent's client area and loads file. Returns nullptr on failure.
    // Keys the view does not handle are posted to the parent (Lister uses them for Esc, n/p, ...).
    HWND create_view(HWND parent, const wchar_t* file);

    // Replaces the shown file. Zoom and an open find bar carry over.
    bool view_load(HWND view, const wchar_t* file);

    // Opens the find bar. With find_next and a search term already entered, jumps to the next match instead.
    void view_find(HWND view, bool find_next);
} // namespace mdlite
