add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate")


set_toolchains("clang")

add_requires("sdl3",       {system = true})

target("example-miniaudio")
    set_kind("binary")
    add_files(
        "kasaria_lib/*.c",
        "example_miniaudio.c"
    )

target("example-sdl3")
    set_kind("binary")
    add_packages("sdl3")
    add_files(
        "kasaria_lib/*.c",
        "example_sdl3.c"
    )


target("example2")
    set_default(false)
    set_kind("binary")
    add_files(
        "kasaria_lib/*.c",
        "diag.c"
    )