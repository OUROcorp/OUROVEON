--   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
--  |       |   |   |   __ \       |   |   |    ___|       |    |  |
--  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
--  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|

require('xcode')
local xcode = premake.modules.xcode 
xcode.cppLanguageStandards["C++20"] = "c++20"

newoption {
    trigger = "pgo",
    value = "mode",
    description = "enable PGO link phase",
    allowed = {
        { "none",       "disabled" },
        { "instrument", "instrumentation phase" },
        { "optimise",   "optimisation phase" }
     },
     default = "none"
}

newoption {
    trigger = "teamid",
    description = "Team ID for apple provisioning",
    default = ""
 }

include "premake-inc/premake-build-vstudio.lua"


-- ==============================================================================

-- stash the starting directory upfront to use as a reference root 
local initialDir = os.getcwd()
print( "Premake launch directory: " .. initialDir )

local rootBuildGenerationDir = "_gen"
local rootSourceDir = "src"

function GetSourceDir()
    return rootSourceDir
end
function SrcDir()
    return initialDir .. "/../" .. rootSourceDir .. "/"
end

-- currently we ship a set of prebuilt libs for certain libraries on Mac that have been 
-- merged into 'fat' x64/ARM versions
function GetPrebuiltLibs_MacUniversal_Headers()
    return initialDir .. "/../libs/macos/opt/"
end
function GetPrebuiltLibs_MacUniversal()
    return initialDir .. "/../libs/macos/universal-fat/"
end

-- .. and a smaller set of prebuilt libs on Windows that are too annoying to move into Premake
function GetPrebuiltLibs_Win64()
    return initialDir .. "/../libs/windows/win64/"
end
function GetPrebuiltLibs_Win64_VSMacro()
    return "$(SolutionDir)..\\..\\libs\\windows\\win64\\"
end


function GetBuildRootToken()
    if ( GeneratingForVisualStudio() ) then
        return "$(SolutionDir)"
    else
        return initialDir .. "/" .. rootBuildGenerationDir .. "/"
    end
end
print ( "Build root token : " .. GetBuildRootToken() )


-- ==============================================================================

include "premake-inc/asiosdk.lua"

-- ASIO sdk download and patching for Windows VS only
checkASIOSDKDownload( rootBuildGenerationDir )

-- ==============================================================================

LibRoot = {}

ModuleRefInclude = {}
ModuleRefLink = {}

ModuleHeaderOnlyFiles = {}

ModuleRefLinkWin    = {}
ModuleRefLinkLinux  = {}
ModuleRefLinkOSX    = {}


include "ispc-premake/premake.lua"

include "premake-inc/common.lua"
include "premake-inc/sys-freetype.lua"
include "premake-inc/sys-openssl.lua"
include "premake-inc/sys-superluminal.lua"


-- ==============================================================================
workspace ("ouroveon_" .. _ACTION)

    configurations  { "Debug", "Release" }

    location (rootBuildGenerationDir)

    filter "system:Windows"

        platforms       { "x86_64" }
        architecture      "x64"

        useISPC()

        defines 
        {
            "WIN32",
            "_WINDOWS",

            "OURO_PLATFORM_WIN=1",
            "OURO_PLATFORM_OSX=0",
            "OURO_PLATFORM_LINUX=0",

            "OURO_HAS_ISPC=1",
            "OURO_HAS_CLAP=0",          -- enable CLAP plugin loading
            "OURO_HAS_NDLS_ONLINE=1",   -- enable connection to the Endlesss live servers. or don't.
            "OURO_HAS_NDLS_SHARING=0",  -- enable sharing-to-Endlesss-feed

            -- for DPP; needs adjusting at the highest level before winsock is included
            "FD_SETSIZE=1024"
        }
        ispcVars { 
            OS              = "windows",
            Architecture    = "x64",
        }

    filter {}
    
    filter "system:linux"

        platforms       { "x86_64" }
        architecture      "x64"

        systemversion   "latest"
        pic             "On"
        staticruntime   "On"

        defines
        {
            "OURO_PLATFORM_WIN=0",
            "OURO_PLATFORM_OSX=0",
            "OURO_PLATFORM_LINUX=1",

            "OURO_HAS_ISPC=0",
            "OURO_HAS_CLAP=0",
            "OURO_HAS_NDLS_ONLINE=1",
            "OURO_HAS_NDLS_SHARING=0",
        }
        buildoptions
        {
            
        }
    filter {}

    filter "system:macosx"

        platforms       { "universal" }
        architecture      "universal"
        systemversion     "11.0"
        
        defines
        {
            "OURO_PLATFORM_WIN=0",
            "OURO_PLATFORM_OSX=1",
            "OURO_PLATFORM_LINUX=0",

            "OURO_HAS_ISPC=0",
            "OURO_HAS_CLAP=0",
            "OURO_HAS_NDLS_ONLINE=1",
            "OURO_HAS_NDLS_SHARING=0",
        }
        buildoptions
        {
            "-Wall"
        }
    filter {}

    -- fine-tune Xcode settings; these are all based on the recommended/upgraded settings in Xcode 15.3
    filter { "system:macosx" }
    xcodebuildsettings 
    {
        ENABLE_HARDENED_RUNTIME = "YES",
        LLVM_LTO = "YES_THIN",
        DEAD_CODE_STRIPPING = "YES",
        CLANG_ENABLE_OBJC_WEAK = "YES",
        STRING_CATALOG_GENERATE_SYMBOLS = "YES", -- as per Xcode 26.2 whining

        -- just to silence the upgrade-project notice
        ASSETCATALOG_COMPILER_GENERATE_SWIFT_ASSET_SYMBOL_EXTENSIONS = "YES",

        -- pass dev team ID in via command line
        CODE_SIGN_STYLE = "Manual",
        ["CODE_SIGN_IDENTITY[sdk=macosx*]"] = "Developer ID Application",
        ["DEVELOPMENT_TEAM[sdk=macosx*]"] = _OPTIONS["teamid"],
    }
    filter {}
    
    filter { "system:macosx", "configurations:Debug" }
    xcodebuildsettings 
    {
        ENABLE_TESTABILITY = "YES",
    }
    filter {}        
    filter { "system:macosx", "configurations:Release" }
    xcodebuildsettings 
    {
        CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = "YES",
        CLANG_WARN_BOOL_CONVERSION = "YES",
        CLANG_WARN_COMMA = "YES",
        CLANG_WARN_CONSTANT_CONVERSION = "YES",
        CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = "YES",
        CLANG_WARN_EMPTY_BODY = "YES",
        CLANG_WARN_ENUM_CONVERSION = "YES",
        CLANG_WARN_INFINITE_RECURSION = "YES",
        CLANG_WARN_INT_CONVERSION = "YES",
        CLANG_WARN_NON_LITERAL_NULL_CONVERSION = "YES",
        CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = "YES",
        CLANG_WARN_OBJC_LITERAL_CONVERSION = "YES",
        CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = "YES",
        CLANG_WARN_RANGE_LOOP_ANALYSIS = "YES",
        CLANG_WARN_STRICT_PROTOTYPES = "YES",
        CLANG_WARN_SUSPICIOUS_MOVE = "YES",
        CLANG_WARN_UNREACHABLE_CODE = "YES",
        CLANG_WARN__DUPLICATE_METHOD_MATCH = "YES",

        ENABLE_STRICT_OBJC_MSGSEND = "YES",
		ENABLE_USER_SCRIPT_SANDBOXING = "YES",
		GCC_NO_COMMON_BLOCKS = "YES",
        GCC_WARN_64_TO_32_BIT_CONVERSION ="YES",
        GCC_WARN_UNDECLARED_SELECTOR = "YES",
		GCC_WARN_UNINITIALIZED_AUTOS = "YES",
		GCC_WARN_UNUSED_FUNCTION = "YES",
    }
    filter {}    

    solutionitems {
        {
            ["natvis"] = 
            {
                path.join( SrcDir(), "r0.data",      "json",       "nlohmann",    "nlohmann_json.natvis" ),
                path.join( SrcDir(), "r0.scaffold",  "abseil_ext",                "abseil.natvis" ),
                path.join( SrcDir(), "r0.scripting", "sol-330",                   "sol2.natvis" ),
            }
        }
    }

-- ==============================================================================

include "premake-inc/r0-headeronly.lua"

group "r0-hal"

include "premake-inc/r0-hal-glfw.lua"
include "premake-inc/r0-hal-portaudio.lua"
include "premake-inc/r0-hal-rtmidi.lua"

group ""
group "r0-scaffold"

include "premake-inc/r0-scaffold-abseil.lua"
include "premake-inc/r0-scaffold-date.lua"
include "premake-inc/r0-scaffold-fmt.lua"
include "premake-inc/r0-scaffold-rpmalloc.lua"
include "premake-inc/r0-scaffold-stb.lua"
include "premake-inc/r0-scaffold-simplecpp.lua"

group ""
group "r0-codec"

include "premake-inc/r0-codec-brotli.lua"
include "premake-inc/r0-codec-flac.lua"
include "premake-inc/r0-codec-foxen.lua"
include "premake-inc/r0-codec-libaiff.lua"
include "premake-inc/r0-codec-opus.lua"
include "premake-inc/r0-codec-zlib.lua"
include "premake-inc/r0-codec-zstd.lua"

group ""
group "r0-data"

include "premake-inc/r0-data-sqlite3.lua"
include "premake-inc/r0-data-sodium.lua"
include "premake-inc/r0-data-rapidyaml.lua"

group ""
group "r0-dsp"

include "premake-inc/r0-dsp-pffft.lua"
include "premake-inc/r0-dsp-r8brain.lua"

group ""
group "r0-net"

-- include "premake-inc/r0-net-nng.lua"
include "premake-inc/r0-net-dpp.lua"
include "premake-inc/r0-net-uriparser.lua"

group ""
group "r0-platform"

include "premake-inc/r0-platform-pfold.lua"

group ""
group "r0-scripting"

include "premake-inc/r0-scripting-lua.lua"

group ""
group "r1-render"

include "premake-inc/r1-render-imgui.lua"

group ""
group "error"

-- ==============================================================================

group ""

function XcodeConfigureApp( layerName )
    if ( os.host() == "macosx" ) then
        xcodebuildsettings 
        {
            ["PRODUCT_BUNDLE_IDENTIFIER"] = "com.reasonandnightmare." .. layerName,
        }
    end    
end

-- ------------------------------------------------------------------------------
function SetupOuroveonLayer( isFinalRing, layerName )

    language "C++"
    cppdialect "C++20"

    SetDefaultBuildConfiguration()
    SetDefaultOutputDirectories(layerName)

    if isFinalRing == true then
       targetdir ( GetBuildRootToken() .. "../../bin/" .. layerName .. "/%{cfg.system}_%{cfg.shortname}" )
       XcodeConfigureApp( layerName )
    else
       targetdir ( GetBuildRootToken() .. "_artefact/ouro/_" .. layerName .. "/%{cfg.system}_%{cfg.shortname}" )
    end

    defines 
    {
        "VST_2_4_EXTENSIONS",
        "VST_64BIT_PLATFORM=1",

        "PCG_LITTLE_ENDIAN=1",
    }

    for libName, libFn in pairs(ModuleRefInclude) do
        libFn()
    end

    includedirs
    {
        SrcDir() .. "r0.closed/steinberg",

        SrcDir() .. "r0.platform",

        SrcDir() .. "r2.ouro",
        SrcDir() .. "r2.ouro.xp",
        SrcDir() .. "r3.endlesss",
        SrcDir() .. "r4.toolbox",
    }

    -- sdk layer compiles all library code; app layers then just link sdk
    if isFinalRing == false then
    files 
    { 
        SrcDir() .. "r2.ouro/**.cpp",
        SrcDir() .. "r2.ouro/**.ispc",
        SrcDir() .. "r2.ouro/**.isph",

        SrcDir() .. "r3.endlesss/**.cpp",

        SrcDir() .. "r4.toolbox/**.cpp",
    }
    
    filter "system:Windows"
    files 
    {
        SrcDir() .. "r2.ouro.xp/xp/windows/*.*",
        
        SrcDir() .. "r0.platform/win32/**.h",
        SrcDir() .. "r0.platform/win32/**.cpp",
    }
    filter {}
    filter "system:linux"
    files 
    {
        SrcDir() .. "r2.ouro.xp/xp/linux/*.*",
    }
    filter {}
    filter "system:macosx"
    files 
    {
        SrcDir() .. "r2.ouro.xp/xp/macosx/*.*",
    }
    filter {}

    end

    -- headers
    files 
    { 
        SrcDir() .. "r0.closed/steinberg/**.h",

        SrcDir() .. "r2.ouro/**.h",
        SrcDir() .. "r2.ouro/**.inl",
        SrcDir() .. "r2.ouro.xp/**.h",

        SrcDir() .. "r3.endlesss/**.h",

        SrcDir() .. "r4.toolbox/**.h",
    }

    for libName, libFn in pairs(ModuleHeaderOnlyFiles) do
        libFn()
    end

end

-- ------------------------------------------------------------------------------
function CommonAppLink()

    links
    {
        "sdk"
    }
    
    for libName, libFn in pairs(ModuleRefLink) do
        libFn()
    end
    if ( os.host() == "windows" ) then
        for libName, libFn in pairs(ModuleRefLinkWin) do
            libFn()
        end
    end
    if ( os.host() == "linux" ) then
        for libName, libFn in pairs(ModuleRefLinkLinux) do
            libFn()
        end
    end
    if ( os.host() == "macosx" ) then
        for libName, libFn in pairs(ModuleRefLinkOSX) do
            libFn()
        end     
    end    

    filter "system:Windows"
    links
    {
        "opengl32",
        "winmm",
        "dxguid",

        "ws2_32",
        "wsock32",
        "shlwapi",
        "version",
        "setupapi",
        "imm32",
    }
    filter {}

    filter "system:linux"
    links 
    {
        "m",
        "pthread",
        "dl",
        "atomic",

        "X11",
        "Xrandr",
        "Xinerama",
        "Xxf86vm",
        "Xcursor",
        "GL",
    }
    filter {}

    filter "system:macosx"
    links 
    {
        "pthread",

        "Cocoa.framework",
        "IOKit.framework",
        "OpenGL.framework",
        "CoreFoundation.framework",
        "CoreVideo.framework",
        "CoreAudio.framework",
        "CoreMIDI.framework",
        "AudioToolBox.framework",
        "AudioUnit.framework",
        "Security.framework"   -- httplib requires
    }
    filter {}

end


-- ==============================================================================


group "r4-ouroveon"

project "sdk"
    kind "StaticLib"
    SetupOuroveonLayer( false, "sdk" )

    AddPCH( 
        path.join( "..", "src", "r2.ouro", "pch.cpp" ),
        path.join( SrcDir(), "r2.ouro" ),
        "pch.h" )

group ""


-- ==============================================================================


group "r5-apps"

-- ------------------------------------------------------------------------------
project "BEAM"

    kind "ConsoleApp"
    SetupOuroveonLayer( true, "beam" )
    CommonAppLink()

    files
    {
        SrcDir() .. "r5.beam/pch.cpp",

        SrcDir() .. "r5.beam/**.cpp",
        SrcDir() .. "r5.beam/**.h",
        SrcDir() .. "r5.beam/**.inl",
    }
    filter "system:windows"
    files
    {
        SrcDir() .. "r5.beam/*.rc",
        SrcDir() .. "r5.beam/*.ico",
    }
    filter {}

    AddPCH( 
        "../src/r5.beam/pch.cpp",
        SrcDir() .. "r2.ouro/",
        "pch.h" )

-- ------------------------------------------------------------------------------
project "LORE"

    kind "ConsoleApp"
    SetupOuroveonLayer( true, "lore" )
    CommonAppLink()

    files 
    {
        SrcDir() .. "r5.lore/pch.cpp",

        SrcDir() .. "r5.lore/**.cpp",
        SrcDir() .. "r5.lore/**.h",
        SrcDir() .. "r5.lore/**.inl",
    }
    filter "system:windows"
    files
    {
        SrcDir() .. "r5.lore/*.rc",
        SrcDir() .. "r5.lore/*.ico",
    }
    filter {}

    AddPCH( 
        "../src/r5.lore/pch.cpp",
        SrcDir() .. "r2.ouro/",
        "pch.h" )


-- ------------------------------------------------------------------------------
project "PONY"

    kind "ConsoleApp"
    SetupOuroveonLayer( true, "pony" )
    CommonAppLink()

    files
    {
        SrcDir() .. "r5.pony/pch.cpp",

        SrcDir() .. "r5.pony/**.cpp",
        SrcDir() .. "r5.pony/**.h",
        SrcDir() .. "r5.pony/**.inl",
    }

    filter "system:windows"
    files
    {
        SrcDir() .. "r5.pony/*.rc",
        SrcDir() .. "r5.pony/*.ico",
    }
    filter {}

    AddPCH( 
        "../src/r5.pony/pch.cpp",
        SrcDir() .. "r2.ouro/",
        "pch.h" )


group ""
