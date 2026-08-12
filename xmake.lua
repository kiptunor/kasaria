add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate", "mode.asan")
--set_policy("build.c++.mode", "default")
set_toolset("ld", "clang")
--set_policy("build.sanitizer.address", true)
-- add_cflags("-O0", "-g")
add_cflags("-march=native", "-O3", "-ffast-math", "-fomit-frame-pointer")
add_ldflags("-flto")




set_toolchains("clang")
set_languages("c11")

add_requires("sdl3",       {system = true})



function copy_soundfonts(target)
    local destdir = target:targetdir()
    for _, file in ipairs(os.files("assets/soundfonts/*.sf2")) do
        local dest = path.join(destdir, path.filename(file))
        if not os.isfile(dest) then
            os.cp(file, dest)
        end
    end
end

target("kasaria")
    add_defines("ULOG_BUILD_CONFIG_HEADER_ENABLED")
    set_kind("shared")
    set_toolset("sh", "clang")
    add_ldflags("-Wl,--as-needed")
    add_links("m")
    add_files("src/**.c")

target("example-miniaudio")
    set_kind("binary")
    add_links("m")
    add_files("example_miniaudio.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("example-sokol")
    set_kind("binary")
    add_links("m", "asound")
    add_files("example_sokol.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("example-sdl3")
    set_kind("binary")
    add_packages("sdl3")
    add_links("m")
    add_files("example_sdl3.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("example-simple")
    set_kind("binary")
    add_links("m")
    add_files("simple-example.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("example-conmidi")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("conmidi_example/**.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("example-async")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("example_async.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("example-converter")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("example_converter.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)