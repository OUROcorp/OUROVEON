--   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
--  |       |   |   |   __ \       |   |   |    ___|       |    |  |
--  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
--  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|

-- ------------------------------------------------------------------------------
function SetDefaultBuildConfiguration()
    
    conformancemode "on"
    cppdialect "C++20"

    filter "system:Windows"
        flags {
            "MultiProcessorCompile"         -- /MP on MSVC to locally distribute compile
        }
        externalwarnings "Off"
        buildoptions {
            "/Zc:__cplusplus",          -- enable __cplusplus compiler macro
            "/Zc:__STDC__",             -- as above so below
            "/external:anglebrackets",  -- treat all < > includes as 'external'; to turn their warnings off, per externalwarnings "Off"
        }
        -- silence all non-portable MSVC security whining
        defines {
            "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1",
            "_CRT_NONSTDC_NO_WARNINGS",
            "_CRT_SECURE_NO_WARNINGS",
        }
        -- https://learn.microsoft.com/en-us/cpp/c-runtime-library/compatibility?view=msvc-170
        defines {
            "_CRT_DECLARE_NONSTDC_NAMES=1",
        }
        vectorextensions ("AVX")
    filter {}

    filter "configurations:Debug"
        defines   { "DEBUG", "OURO_DEBUG=1", "OURO_RELEASE=0" }
        symbols   "FastLink"
        editandcontinue "On"
        ispcVars {
            GenerateDebugInformation = true,
            Opt         = "disabled",
            CPU         = "core2",
            TargetISA   = "sse2-i32x8",
        }
    filter {}

    filter "configurations:Release"
        defines   { "NDEBUG", "OURO_DEBUG=0", "OURO_RELEASE=1" }
        flags     { "LinkTimeOptimization" }
        optimize  "Full"
        editandcontinue "Off"
        ispcVars { 
            Opt         = "maximum",
            CPU         = "core2",
            TargetISA   = "sse2-i32x8",
        }
    filter {}

    filter { "configurations:Release", "options:pgo=instrument" }
        pgo "Instrument"
        defines   { "OURO_PGO_INSTRUMENT" }
    filter {}
    filter { "configurations:Release", "options:pgo=optimise" }
        pgo "Optimize"
        defines   { "OURO_PGO_OPTIMIZE" }
    filter {}

end

-- ------------------------------------------------------------------------------
function SetDefaultOutputDirectories(subgrp)

    targetdir   ( GetBuildRootToken() .. "_artefact/bin_" .. subgrp .. "/%{cfg.shortname}/%{prj.name}/" )
    objdir      ( "!" .. GetBuildRootToken() .. "_artefact/obj_" .. subgrp .. "/%{cfg.shortname}/%{prj.name}/" )
    debugdir    ( "$(OutDir)" )

end

-- ------------------------------------------------------------------------------
function AddPCH( sourceFile, headerFilePath, headerFile )
    pchsource ( sourceFile )

    filter "action:vs*"  -- for Visual Studio actions
        pchheader ( headerFile )
    filter{}

    filter "action:not vs*"  -- for everything else
        pchheader ( path.join( headerFilePath, headerFile ) )
    filter{}
end