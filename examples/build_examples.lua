

function copy_soundfonts(target)
    local destdir = target:targetdir()
    for _, file in ipairs(os.files("../assets/soundfonts/*.sf2")) do
        local dest = path.join(destdir, path.filename(file))
        if not os.isfile(dest) then
            os.cp(file, dest)
        end
    end
end

target("midi-player-miniaudio")
    set_kind("binary")
    add_links("m")
    add_files("builtin_player/custom_audio_backend/midi_player_miniaudio.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("midi-player-sokol")
    set_kind("binary")
    
    if is_plat("linux") then
        add_links("asound")
    elseif is_plat("mingw") then
         add_links("ole32")
    end
    add_links("m")
    add_files("builtin_player/custom_audio_backend/midi_player_sokol.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("midi-player-sdl3")

    if is_plat("mingw") then
        set_default(false)
    end
    
    set_kind("binary")
    add_packages("sdl3")
    add_links("m")
    add_files("builtin_player/custom_audio_backend/midi_player_sdl3.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("external-midi-player")

    if is_plat("mingw") then
        set_default(false)
    end
    
    set_kind("binary")
    add_packages("sdl3")
    add_links("m", "pthread", "asound")
    add_files("external_midi_player/**.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("simple-midi-player")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("builtin_player/simple_midi_player.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)

target("midi-converter")
    set_kind("binary")
    add_links("m", "pthread")
    add_files("builtin_player/midi_converter.c")
    add_deps("kasaria")
    after_build(copy_soundfonts)