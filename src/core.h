#pragma once
#include <string>
#include <windows.h>
#include <litehtml.h>
#include "gdiplus_container.h"

namespace mdlite
{
    // Reads a file into memory, strips a UTF-8 BOM. Returns false on failure.
    bool read_file(const wchar_t* path, std::string& out);

    // CommonMark + GitHub extensions (tables, strikethrough, task lists, autolinks) -> HTML fragment.
    std::string md_to_html(const std::string& markdown);

    // Stylesheet applied on top of litehtml's master CSS.
    std::string stylesheet(bool dark);

    bool system_uses_dark_theme();

    // Document container: litehtml's GDI+ container plus local image loading and link capture.
    // The base class enumerates all installed fonts and starts GDI+ in its constructor, so keep one
    // instance per process (see shared_container()).
    class container : public gdiplus_container
    {
      public:
        std::wstring base_dir;       // directory of the file being shown, for relative image paths
        std::string  clicked_link;   // set by on_anchor_click, consumed by the window
        std::string  cursor;         // last cursor requested by litehtml ("pointer", "auto", ...)
        int          viewport_w = 0; // client size, reported through get_viewport
        int          viewport_h = 0;

        void set_caption(const char*) override {}
        void set_base_url(const char*) override {}
        void on_anchor_click(const char* url, const litehtml::element::ptr&) override
        {
            clicked_link = url ? url : "";
        }
        void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
        void set_cursor(const char* c) override
        {
            cursor = c ? c : "";
        }
        void import_css(std::string&, const std::string&, std::string&) override {}
        void get_viewport(litehtml::position& vp) const override
        {
            vp = litehtml::position(0, 0, viewport_w, viewport_h);
        }

      protected:
        void      make_url(LPCWSTR url, LPCWSTR basepath, std::wstring& out) override;
        uint_ptr  get_image(LPCWSTR url_or_path, bool redraw_on_ready) override;
    };

    container& shared_container();

    // Timings in milliseconds, filled by build_document().
    struct timings
    {
        double read_ms = 0, md_ms = 0, parse_ms = 0, layout_ms = 0;
    };

    litehtml::document::ptr build_document(container& c, const std::string& markdown, int width, bool dark,
                                           timings* t = nullptr);
} // namespace mdlite
