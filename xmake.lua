add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate", "mode.asan")
--set_policy("build.c++.mode", "default")
set_toolset("ld", "clang")
-- set_policy("build.sanitizer.address", true)
-- add_cflags("-O0", "-g")




set_toolchains("clang")
set_languages("c11")

add_requires("sdl3",       {system = true})

target("example-miniaudio")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_links("m")
    add_files(
        "kasaria_lib/**.c",
        "example_miniaudio.c"
    )

target("example-sdl3")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_packages("sdl3")
    add_links("m")
    add_files(
        "kasaria_lib/**.c",
        "example_sdl3.c"
    )

target("example-simple")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    add_links("m")
    add_files(
        "kasaria_lib/**.c",
        "simple-example.c"
    )

target("example-conmidi")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("binary")
    --set_languages("c11")
    add_links("m", "pthread")
    add_files(
        "kasaria_lib/**.c",
        "conmidi_example/**.c"
    )