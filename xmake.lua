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
    set_default(false)
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_links("m")
    add_files(
        "src/**.c",
        "example_miniaudio.c"
    )

target("example-sokol")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_links("m", "asound")
    add_files(
        "src/**.c",
        "example_sokol.c"
    )

target("example-sdl3")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_packages("sdl3")
    add_links("m")
    add_files(
        "src/**.c",
        "example_sdl3.c"
    )

target("example-simple")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_links("m")
    add_files(
        "src/**.c",
        "simple-example.c"
    )

target("example-conmidi")
    set_default(false)
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    --set_languages("c11")
    add_links("m", "pthread")
    add_files(
        "src/**.c",
        "conmidi_example/**.c"
    )

target("example-async")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    --set_languages("c11")
    add_links("m", "pthread")
    add_files(
        "src/**.c",
        "example_async.c"
    )