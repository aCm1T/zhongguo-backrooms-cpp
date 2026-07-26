cmake_minimum_required(VERSION 3.20)
project(huaxia_backrooms_raytraced LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_file(SDL2_RUNTIME
    NAMES libSDL2-2.0.so.0
    PATHS /lib64 /usr/lib64 /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib /usr/lib
    REQUIRED)

foreach(LIB_SPEC
    "CAIRO_RUNTIME;libcairo.so.2"
    "PANGO_RUNTIME;libpango-1.0.so.0"
    "PANGOCAIRO_RUNTIME;libpangocairo-1.0.so.0"
    "GOBJECT_RUNTIME;libgobject-2.0.so.0"
    "GLIB_RUNTIME;libglib-2.0.so.0")
    list(GET LIB_SPEC 0 LIB_VAR)
    list(GET LIB_SPEC 1 LIB_NAME)
    find_file(${LIB_VAR} NAMES ${LIB_NAME}
        PATHS /lib64 /usr/lib64 /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib /usr/lib
        REQUIRED)
endforeach()

add_executable(huaxia_backrooms
    src/main.cpp
)

target_include_directories(huaxia_backrooms PRIVATE src)
target_compile_definitions(huaxia_backrooms PRIVATE
    BACKROOMS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
target_compile_options(huaxia_backrooms PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(huaxia_backrooms PRIVATE
    ${SDL2_RUNTIME} ${PANGOCAIRO_RUNTIME} ${PANGO_RUNTIME}
    ${CAIRO_RUNTIME} ${GOBJECT_RUNTIME} ${GLIB_RUNTIME} dl pthread)

install(TARGETS huaxia_backrooms RUNTIME DESTINATION bin)
