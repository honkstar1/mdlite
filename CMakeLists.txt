cmake_minimum_required(VERSION 3.15)
project(mdlite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- third party -----------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/md4c EXCLUDE_FROM_ALL)

set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LITEHTML_ENABLE_LINT OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/litehtml EXCLUDE_FROM_ALL)

set(LH ${CMAKE_SOURCE_DIR}/third_party/litehtml)

# --- core: markdown -> html -> litehtml document, GDI+ container ------------
add_library(mdlite_core STATIC
    src/core.cpp
    src/container/win32_container.cpp
    src/container/gdiplus_container.cpp)
target_include_directories(mdlite_core PUBLIC
    src
    src/container
    ${LH}/include
    ${CMAKE_SOURCE_DIR}/third_party/md4c/src)
target_compile_definitions(mdlite_core PUBLIC UNICODE _UNICODE NOMINMAX)
target_link_libraries(mdlite_core PUBLIC litehtml md4c-html md4c gdiplus)

# --- Total Commander lister plugin (.wlx64) --------------------------------
add_library(mdlite_wlx SHARED src/plugin.cpp src/plugin.def)
target_link_libraries(mdlite_wlx PRIVATE mdlite_core user32 gdi32 shell32 advapi32)
set_target_properties(mdlite_wlx PROPERTIES OUTPUT_NAME mdlite SUFFIX ".wlx64")

# --- headless benchmark ----------------------------------------------------
add_executable(mdlite_bench tools/bench.cpp)
target_link_libraries(mdlite_bench PRIVATE mdlite_core)
