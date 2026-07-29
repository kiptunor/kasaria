add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate", "mode.asan")
--set_policy("build.c++.mode", "default")
set_toolset("ld", "clang")
-- set_policy("build.sanitizer.address", true)
-- add_cflags("-O0", "-g")
add_cflags("-march=native", "-O3", "-ffast-math", "-fomit-frame-pointer")
add_ldflags("-flto")




set_toolchains("clang")
set_languages("c11")

add_requires("sdl3",       {system = true})


target("kasaria")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("shared")
    add_links("m")
    add_files("src/**.c")

target("example-miniaudio")
    set_kind("binary")
    add_links("m")
    add_files("example_miniaudio.c")
    add_deps("kasaria")

target("example-sokol")
    set_kind("binary")
    add_links("m", "asound")
    add_files("example_sokol.c")
    add_deps("kasaria")

target("example-sdl3")
    set_kind("binary")
    add_packages("sdl3")
    add_links("m")
    add_files("example_sdl3.c")
    add_deps("kasaria")

target("example-simple")
    set_kind("binary")
    add_links("m")
    add_files("simple-example.c")
    add_deps("kasaria")

target("example-conmidi")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("conmidi_example/**.c")
    add_deps("kasaria")

target("example-async")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("example_async.c")
    add_deps("kasaria")