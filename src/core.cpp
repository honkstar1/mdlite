#include "core.h"
#include <gdiplus.h>
#include <md4c-html.h>
#include <chrono>

namespace mdlite
{
    using clock_t_ = std::chrono::steady_clock;

    static double ms_since(clock_t_::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();
    }

    bool read_file(const wchar_t* path, std::string& out)
    {
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if(h == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(h, &size) || size.QuadPart > (256ll << 20))
        {
            CloseHandle(h);
            return false;
        }
        out.resize((size_t) size.QuadPart);
        DWORD got = 0;
        bool  ok  = size.QuadPart == 0 || ReadFile(h, out.data(), (DWORD) size.QuadPart, &got, nullptr);
        CloseHandle(h);
        if(!ok)
            return false;
        out.resize(got);
        if(out.size() >= 3 && (unsigned char) out[0] == 0xEF && (unsigned char) out[1] == 0xBB &&
           (unsigned char) out[2] == 0xBF)
            out.erase(0, 3);
        return true;
    }

    static void append_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
    {
        static_cast<std::string*>(userdata)->append(text, size);
    }

    std::string md_to_html(const std::string& markdown)
    {
        std::string html;
        html.reserve(markdown.size() * 2);
        md_html(markdown.data(), (MD_SIZE) markdown.size(), append_output, &html,
                MD_DIALECT_GITHUB | MD_FLAG_PERMISSIVEURLAUTOLINKS, 0);
        return html;
    }

    bool system_uses_dark_theme()
    {
        DWORD value = 1, size = sizeof(value);
        if(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
            return false;
        return value == 0;
    }

    std::string stylesheet(bool dark, float zoom)
    {
        // Font sizes in pt so litehtml's pt_to_px applies the screen DPI; everything else is in em.
        const char* fg      = dark ? "#d4d4d4" : "#1f2328";
        const char* bg      = dark ? "#1e1e1e" : "#ffffff";
        const char* border  = dark ? "#3c3c3c" : "#d0d7de";
        const char* code_bg = dark ? "#2a2a2a" : "#f3f4f6";
        const char* link    = dark ? "#4fa6ff" : "#0969da";
        const char* muted   = dark ? "#9da5ae" : "#59636e";

        char font_size[32];
        snprintf(font_size, sizeof(font_size), "%.2fpt", 11.0f * zoom);

        std::string css;
        css += std::string("html, body { background-color: ") + bg + "; }\n";
        css += std::string("body { margin: 0; padding: 1.4em 2em; color: ") + fg +
               "; font-family: 'Segoe UI', sans-serif; font-size: " + font_size + "; line-height: 1.55; }\n";
        css += "h1, h2, h3, h4, h5, h6 { font-weight: bold; line-height: 1.25; margin: 1.4em 0 0.6em 0; }\n";
        css += "h1 { font-size: 2em; } h2 { font-size: 1.5em; } h3 { font-size: 1.25em; }\n";
        css += "h4 { font-size: 1em; } h5 { font-size: 0.875em; } h6 { font-size: 0.85em; }\n";
        css += std::string("h1, h2 { padding-bottom: 0.3em; border-bottom: 1px solid ") + border + "; }\n";
        css += "h6 { color: " + std::string(muted) + "; }\n";
        css += "body > :first-child { margin-top: 0; }\n";
        css += "p, ul, ol, pre, table, blockquote { margin: 0 0 1em 0; }\n";
        css += "ul, ol { padding-left: 2em; }\n";
        css += "li { margin: 0.25em 0; }\n";
        css += std::string("a { color: ") + link + "; text-decoration: none; }\n";
        css += std::string("code { font-family: Consolas, monospace; font-size: 85%; background-color: ") + code_bg +
               "; padding: 0.15em 0.35em; }\n";
        css += std::string("pre { font-family: Consolas, monospace; background-color: ") + code_bg +
               "; padding: 0.9em 1em; line-height: 1.4; }\n";
        css += "pre code { font-size: 85%; background-color: transparent; padding: 0; white-space: pre; }\n";
        css += std::string("blockquote { padding: 0 1em; color: ") + muted + "; border-left: 4px solid " + border +
               "; }\n";
        css += "blockquote > :last-child { margin-bottom: 0; }\n";
        css += "table { border-collapse: collapse; }\n";
        css += std::string("th, td { border: 1px solid ") + border + "; padding: 0.4em 0.8em; }\n";
        css += std::string("th { font-weight: bold; background-color: ") + code_bg + "; }\n";
        css += std::string("hr { height: 1px; border: 0; background-color: ") + border + "; margin: 1.5em 0; }\n";
        css += "img { max-width: 100%; }\n";
        css += "del { text-decoration: line-through; }\n";
        return css;
    }

    static std::wstring utf8_to_wide(const char* s)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if(n <= 1)
            return {};
        std::wstring w(n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
        return w;
    }

    static std::wstring percent_decode(const std::wstring& in)
    {
        // Operate on UTF-8 bytes so %C3%A5 style escapes decode correctly.
        int         n = WideCharToMultiByte(CP_UTF8, 0, in.c_str(), (int) in.size(), nullptr, 0, nullptr, nullptr);
        std::string bytes(n, '\0'), out;
        WideCharToMultiByte(CP_UTF8, 0, in.c_str(), (int) in.size(), bytes.data(), n, nullptr, nullptr);
        for(size_t i = 0; i < bytes.size(); i++)
        {
            if(bytes[i] == '%' && i + 2 < bytes.size() + 0 && isxdigit((unsigned char) bytes[i + 1]) &&
               isxdigit((unsigned char) bytes[i + 2]))
            {
                out += (char) strtol(bytes.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            } else
                out += bytes[i];
        }
        return utf8_to_wide(out.c_str());
    }

    void container::make_url(LPCWSTR url, LPCWSTR /*basepath*/, std::wstring& out)
    {
        std::wstring u = url ? url : L"";
        if(u.rfind(L"file:///", 0) == 0)
            u.erase(0, 8);
        u = percent_decode(u);
        for(auto& ch : u)
            if(ch == L'/')
                ch = L'\\';
        bool absolute = (u.size() > 1 && u[1] == L':') || (u.size() > 1 && u[0] == L'\\' && u[1] == L'\\');
        out           = absolute ? u : base_dir + L"\\" + u;
    }

    container::uint_ptr container::get_image(LPCWSTR url_or_path, bool /*redraw_on_ready*/)
    {
        // Remote images are skipped on purpose: fetching them would make opening a file depend on the network.
        if(!url_or_path || wcsstr(url_or_path, L"http:\\") == url_or_path || wcsstr(url_or_path, L"https:\\") == url_or_path)
            return 0;
        auto* bmp = Gdiplus::Bitmap::FromFile(url_or_path);
        if(!bmp || bmp->GetLastStatus() != Gdiplus::Ok)
        {
            delete bmp;
            return 0;
        }
        return (uint_ptr) bmp;
    }

    container& shared_container()
    {
        // Deliberately leaked: the destructor calls GdiplusShutdown, which must not run under the loader lock
        // when Total Commander unloads the DLL.
        static container* c = new container;
        return *c;
    }

    litehtml::document::ptr build_document(container& c, const std::string& markdown, int width, bool dark,
                                           float zoom, timings* t)
    {
        auto t0   = clock_t_::now();
        auto body = md_to_html(markdown);
        if(t)
            t->md_ms = ms_since(t0);

        t0         = clock_t_::now();
        auto doc   = litehtml::document::createFromString(body, &c, litehtml::master_css, stylesheet(dark, zoom));
        if(t)
            t->parse_ms = ms_since(t0);

        if(doc)
        {
            t0 = clock_t_::now();
            doc->render(width);
            if(t)
                t->layout_ms = ms_since(t0);
        }
        return doc;
    }
} // namespace mdlite
