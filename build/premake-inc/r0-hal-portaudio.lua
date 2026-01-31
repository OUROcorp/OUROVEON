
function Root_PORTAUDIO()
    return SrcDir() .. "r0.hal/portaudio/"
end

function Root_SteinbergAsioSDK()
    return path.join( SrcDir(), "r0.hal", "asiosdk", asioSDKVersion() )
end

-- ==============================================================================
ModuleRefInclude["portaudio"] = function()

    includedirs
    {
        Root_PORTAUDIO() .. "include",
    }

    filter "configurations:Debug"
    defines 
    { 
        "PA_ENABLE_DEBUG_OUTPUT",
    }
    filter {}

    filter "system:Windows"
    defines 
    {
        "PA_USE_ASIO",
        "PA_USE_DS",
        "PA_USE_WASAPI",
        "PA_USE_WDMKS",
    }
    filter {}

    filter "system:linux"
    defines 
    {
        "PA_USE_ALSA",
        "PA_USE_JACK",
    }
    links
    {
        "jack",
        "asound",
    }
    filter {}

    filter "system:macosx"
    defines 
    {
        "PA_USE_COREAUDIO",
    }
    filter {}
end

-- ==============================================================================
project "r0-portaudio"
    kind "StaticLib"
    language "C"

    SetDefaultBuildConfiguration()
    SetDefaultOutputDirectories("r0")
    
    ModuleRefInclude["portaudio"]()

    includedirs
    {
        Root_PORTAUDIO() .. "src/common",
    }
    files 
    { 
        Root_PORTAUDIO() .. "src/common/**.*",
    }

    filter "system:Windows"
    includedirs
    {
        Root_PORTAUDIO() .. "src/os/win",

        path.join( Root_SteinbergAsioSDK(), "common" ),
        path.join( Root_SteinbergAsioSDK(), "host" ),
        path.join( Root_SteinbergAsioSDK(), "host", "pc" ),
    }
    files 
    { 
        Root_PORTAUDIO() .. "src/os/win/**.*",
        Root_PORTAUDIO() .. "src/hostapi/asio/*.cpp",
        Root_PORTAUDIO() .. "src/hostapi/asio/*.h",
        Root_PORTAUDIO() .. "src/hostapi/dsound/*.*",
        Root_PORTAUDIO() .. "src/hostapi/wasapi/*.c",
        Root_PORTAUDIO() .. "src/hostapi/wdmks/*.c",
        Root_PORTAUDIO() .. "src/hostapi/wmme/*.c",
        Root_PORTAUDIO() .. "**.h",

        path.join( Root_SteinbergAsioSDK(), "common", "asio.cpp" );
        path.join( Root_SteinbergAsioSDK(), "host", "asiodrivers.cpp" );
        path.join( Root_SteinbergAsioSDK(), "host", "pc", "asiolist.cpp" );
    }
    filter {}

    filter "system:linux"
    includedirs
    {
        Root_PORTAUDIO() .. "src/os/unix",
    }
    files 
    { 
        Root_PORTAUDIO() .. "src/os/unix/**.*",
        Root_PORTAUDIO() .. "src/hostapi/sndio/*.c",
        Root_PORTAUDIO() .. "src/hostapi/jack/*.c",
        Root_PORTAUDIO() .. "src/hostapi/alsa/*.c",
        Root_PORTAUDIO() .. "**.h",
    }
    filter {}

    filter "system:macosx"
    includedirs
    {
        Root_PORTAUDIO() .. "src/os/unix",
    }
    files 
    { 
        Root_PORTAUDIO() .. "src/os/unix/**.*",
        Root_PORTAUDIO() .. "src/hostapi/coreaudio/pa_mac_core.c",
        Root_PORTAUDIO() .. "src/hostapi/coreaudio/pa_mac_core_blocking.c",
        Root_PORTAUDIO() .. "src/hostapi/coreaudio/pa_mac_core_utilities.c",
        Root_PORTAUDIO() .. "src/hostapi/coreaudio/**.h",
    }
    filter {}


ModuleRefLink["portaudio"] = function()
    links { "r0-portaudio" }
end