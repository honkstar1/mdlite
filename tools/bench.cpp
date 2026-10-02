// Headless timing of the pipeline: read -> md4c -> litehtml parse -> layout.
// usage: mdlite_bench file.md [runs]
#include "core.h"
#include <cstdio>
#include <chrono>

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        puts("usage: mdlite_bench file.md [runs]");
        return 1;
    }
    int          runs = argc > 2 ? atoi(argv[2]) : 5;
    wchar_t      path[MAX_PATH * 2];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, (int) std::size(path));

    using clk = std::chrono::steady_clock;
    auto t0   = clk::now();
    auto& c   = mdlite::shared_container(); // font enumeration + GDI+ startup: paid once per process
    double container_ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    printf("container creation (once per process): %.1f ms\n", container_ms);

    for(int i = 0; i < runs; i++)
    {
        std::string md;
        auto        r0 = clk::now();
        if(!mdlite::read_file(path, md))
        {
            puts("cannot read file");
            return 1;
        }
        double read_ms = std::chrono::duration<double, std::milli>(clk::now() - r0).count();

        mdlite::timings t;
        c.viewport_w = 900;
        c.viewport_h = 700;
        auto doc     = mdlite::build_document(c, md, 900, false, &t);
        if(!doc)
        {
            puts("document build failed");
            return 1;
        }
        printf("run %d: read %.2f  md4c %.2f  parse %.2f  layout %.2f  total %.2f ms  (doc height %d px)\n", i + 1,
               read_ms, t.md_ms, t.parse_ms, t.layout_ms, read_ms + t.md_ms + t.parse_ms + t.layout_ms,
               (int) doc->height());
    }
    return 0;
}
