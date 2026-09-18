add_rules("mode.debug", "mode.release", "plugin.compile_commands.autoupdate", "mode.asan")
--set_policy("build.c++.mode", "default")
--set_toolset("ld", "clang")
--set_policy("build.sanitizer.address", true)
-- add_cflags("-O0", "-g")






set_languages("c11")

if is_plat("linux") then
    add_requires("sdl3",       {system = true})
    
    set_toolchains("clang")

    -- Required to prevent any C++ library from being linked to kasaria library
    set_toolset("cc", "clang")
    set_toolset("cxx", "clang++")
    set_toolset("ld", "clang")
    set_toolset("sh", "clang")
        
    add_cflags("-march=native", "-O3", "-ffast-math", "-fomit-frame-pointer")
    add_ldflags("-flto")
elseif is_plat("mingw") then
set_toolset("cc", "clang")
    set_toolset("sh", "clang")

    add_cflags(
        "--target=x86_64-w64-windows-gnu",
        "--gcc-toolchain=/usr",
        "-O3",
        "-ffast-math",
        "-fomit-frame-pointer"
    )

    add_shflags(
        "--target=x86_64-w64-windows-gnu",
        "--gcc-toolchain=/usr"
    )
end



option("build-examples")
    set_default(false)
    set_showmenu(true)
    set_description("Build examples")


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
    add_defines("LOGC__USER_SETTINGS")
    set_kind("shared")
   -- set_toolset("sh", "clang")
    
    if is_plat("linux") then
        add_ldflags("-Wl,--as-needed")
        add_links("m")
    elseif is_plat("mingw") then
        --set_prefixname("")
        add_links("kernel32")
    end
    
    add_links("m")
    add_files("src/**.c")


if has_config("build-examples") then
    includes("examples/build_examples.lua")
end