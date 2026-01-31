//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  Frontend controls initialistion of the rendering canvas and IMGUI 
//  instance, along with the update and display hooks
//

#include "pch.h"
#include "app/core.h"
#include "app/module.frontend.h"
#include "app/module.frontend.fonts.h"
#include "colour/preset.h"

#include "gfx/gl/enumstring.h"

#include "xp/open.url.h"

#include "config/frontend.h"
#include "config/display.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_freetype.h"

// bits from imgui internals
#include "imgui_internal.h"

namespace app {
namespace module {


// ---------------------------------------------------------------------------------------------------------------------
void ApplyOuroveonImGuiStyle()
{
    ImGuiStyle& style                     = ImGui::GetStyle();
    style.WindowRounding                  = 1.0f;
    style.ChildRounding                   = 1.0f;
    style.FrameRounding                   = 3.0f;
    style.GrabRounding                    = 2.0f;
    style.PopupRounding                   = 1.0f;
    style.ScrollbarRounding               = 10.0f;
    style.TabRounding                     = 2.0f;

    ImVec4* colors                        = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4( 0.97f, 0.96f, 0.93f, 0.99f );
    colors[ImGuiCol_TextDisabled]          = ImVec4( 0.54f, 0.53f, 0.53f, 1.00f );
    colors[ImGuiCol_WindowBg]              = ImVec4( 0.08f, 0.09f, 0.10f, 1.00f );
    colors[ImGuiCol_ChildBg]               = ImVec4( 0.08f, 0.09f, 0.10f, 1.00f );
    colors[ImGuiCol_PopupBg]               = ImVec4( 0.02f, 0.02f, 0.02f, 1.00f );
    colors[ImGuiCol_Border]                = ImVec4( 0.08f, 0.09f, 0.10f, 1.00f );
    colors[ImGuiCol_BorderShadow]          = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
    colors[ImGuiCol_FrameBg]               = ImVec4( 0.12f, 0.14f, 0.18f, 1.00f );
    colors[ImGuiCol_FrameBgHovered]        = ImVec4( 0.14f, 0.17f, 0.27f, 1.00f );
    colors[ImGuiCol_FrameBgActive]         = ImVec4( 0.16f, 0.21f, 0.32f, 1.00f );
    colors[ImGuiCol_TitleBg]               = ImVec4( 0.03f, 0.09f, 0.23f, 1.00f );
    colors[ImGuiCol_TitleBgActive]         = ImVec4( 0.04f, 0.10f, 0.26f, 1.00f );
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4( 0.01f, 0.06f, 0.15f, 1.00f );
    colors[ImGuiCol_MenuBarBg]             = ImVec4( 0.05f, 0.13f, 0.34f, 1.00f );
    colors[ImGuiCol_ScrollbarBg]           = ImVec4( 0.11f, 0.13f, 0.18f, 0.71f );
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4( 0.40f, 0.46f, 0.53f, 0.39f );
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4( 0.40f, 0.46f, 0.53f, 0.63f );
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4( 0.97f, 0.48f, 0.02f, 1.00f );
    colors[ImGuiCol_CheckMark]             = ImVec4( 0.97f, 0.48f, 0.02f, 1.00f );
    colors[ImGuiCol_SliderGrab]            = ImVec4( 0.40f, 0.46f, 0.53f, 1.00f );
    colors[ImGuiCol_SliderGrabActive]      = ImVec4( 0.97f, 0.48f, 0.02f, 1.00f );
    colors[ImGuiCol_Button]                = ImVec4( 0.35f, 0.44f, 0.55f, 0.31f );
    colors[ImGuiCol_ButtonHovered]         = ImVec4( 0.47f, 0.56f, 0.66f, 0.45f );
    colors[ImGuiCol_ButtonActive]          = ImVec4( 0.97f, 0.48f, 0.02f, 0.86f );
    colors[ImGuiCol_Header]                = ImVec4( 0.49f, 0.06f, 0.02f, 0.94f );
    colors[ImGuiCol_HeaderHovered]         = ImVec4( 0.50f, 0.07f, 0.03f, 1.00f );
    colors[ImGuiCol_HeaderActive]          = ImVec4( 0.53f, 0.07f, 0.03f, 1.00f );
    colors[ImGuiCol_Separator]             = ImVec4( 0.04f, 0.04f, 0.07f, 0.93f );
    colors[ImGuiCol_SeparatorHovered]      = ImVec4( 0.39f, 0.59f, 0.85f, 0.58f );
    colors[ImGuiCol_SeparatorActive]       = ImVec4( 0.39f, 0.59f, 0.85f, 1.00f );
    colors[ImGuiCol_ResizeGrip]            = ImVec4( 0.25f, 0.44f, 0.82f, 1.00f );
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4( 0.33f, 0.51f, 0.88f, 1.00f );
    colors[ImGuiCol_ResizeGripActive]      = ImVec4( 0.23f, 0.60f, 0.88f, 1.00f );
    colors[ImGuiCol_Tab]                   = ImVec4( 0.08f, 0.19f, 0.42f, 1.00f );
    colors[ImGuiCol_TabHovered]            = ImVec4( 0.09f, 0.29f, 0.72f, 1.00f );
    colors[ImGuiCol_TabActive]             = ImVec4( 0.09f, 0.33f, 0.82f, 1.00f );
    colors[ImGuiCol_TabUnfocused]          = ImVec4( 0.08f, 0.19f, 0.42f, 0.78f );
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4( 0.09f, 0.33f, 0.82f, 0.78f );
    colors[ImGuiCol_DockingPreview]        = ImVec4( 0.22f, 0.58f, 0.94f, 1.00f );
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4( 0.04f, 0.04f, 0.05f, 1.00f );
    colors[ImGuiCol_PlotLines]             = ImVec4( 0.61f, 0.61f, 0.61f, 1.00f );
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4( 0.56f, 0.16f, 0.04f, 1.00f );
    colors[ImGuiCol_PlotHistogram]         = ImVec4( 0.00f, 0.66f, 0.18f, 1.00f );
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4( 0.63f, 0.73f, 0.01f, 1.00f );
    colors[ImGuiCol_TableHeaderBg]         = ImVec4( 0.21f, 0.40f, 0.84f, 0.10f );
    colors[ImGuiCol_TableBorderStrong]     = ImVec4( 0.00f, 0.00f, 0.00f, 0.82f );
    colors[ImGuiCol_TableBorderLight]      = ImVec4( 0.00f, 0.00f, 0.00f, 0.50f );
    colors[ImGuiCol_TableRowBg]            = ImVec4( 0.61f, 0.74f, 1.00f, 0.04f );
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4( 0.61f, 0.74f, 1.00f, 0.07f );
    colors[ImGuiCol_TextSelectedBg]        = ImVec4( 0.21f, 0.31f, 0.48f, 0.70f );
    colors[ImGuiCol_DragDropTarget]        = ImVec4( 1.00f, 1.00f, 0.00f, 0.90f );
    colors[ImGuiCol_NavHighlight]          = ImVec4( 0.91f, 0.44f, 0.04f, 1.00f );
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4( 0.80f, 0.80f, 0.80f, 0.20f );
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );
}


// ---------------------------------------------------------------------------------------------------------------------
Frontend::Frontend( const config::Frontend& feConfig, const char* name )
    : m_feConfigCopy( feConfig )
    , m_appName( name )
    , m_imguiLayoutIni( nullptr )
    , m_glfwWindow( nullptr )
    , m_fontFixed( nullptr )
    , m_fontMedium( nullptr )
    , m_fontLogo( nullptr )
    , m_quitRequested( false )
{
}

// ---------------------------------------------------------------------------------------------------------------------
Frontend::~Frontend()
{
    delete[]m_imguiLayoutIni;

    if ( m_glfwWindow != nullptr )
        destroy();
}

static void glfwErrorCallback( int error, const char* description )
{
    blog::error::core( FMTX( "[glfw] error {} : {}" ), error, description );
}

// ---------------------------------------------------------------------------------------------------------------------
enum FontSlots
{
    FsFixed_EN,
    FsFixed_JP,
    FsFixed_KR,
    FsFontAwesome,
    FsFontAwesomeBrands,
    FsFontEmoji,
    FsTitle,
    FsMedium,
    FsCount
};

// ---------------------------------------------------------------------------------------------------------------------
absl::Status Frontend::create( app::Core* appCore )
{
    const auto baseStatus = Module::create( appCore );
    if ( !baseStatus.ok() )
        return baseStatus;

    const auto fontLoadpath = appCore->getSharedDataPath() / "fonts";

    // set the expected font locations
    std::array< fs::path, FsCount > fontFilesSlots;
    {
        fontFilesSlots[ FsFixed_EN ]            = fontLoadpath / "FiraCode-Regular.ttf";
        fontFilesSlots[ FsFixed_JP ]            = fontLoadpath / "jp" / "migu-1m-regular.ttf";
        fontFilesSlots[ FsFixed_KR ]            = fontLoadpath / "kr" / "D2Coding-subset.ttf";
        fontFilesSlots[ FsFontAwesome ]         = fontLoadpath / "icons" / FONT_ICON_FILE_NAME_FAS;
        fontFilesSlots[ FsFontAwesomeBrands ]   = fontLoadpath / "icons" / FONT_ICON_FILE_NAME_FAB;
        fontFilesSlots[ FsFontEmoji ]           = fontLoadpath / "emoji" / "NotoEmoji-B&W.ttf";
        fontFilesSlots[ FsTitle ]               = fontLoadpath / "Warheed.otf";
        fontFilesSlots[ FsMedium ]              = fontLoadpath / "Oswald-Light.ttf";
    }

    #define FONT_AT( _idx ) fontFilesSlots[_idx].string().c_str()

    // preflight checks on the font data, just check the files exist or abort boot if it fails
    for ( const auto& fontPath : fontFilesSlots )
    {
        if ( !fs::exists( fontPath ) )
        {
            return absl::NotFoundError( fmt::format( FMTX( "Unable to find required font file [{}]" ), fontPath.string() ) );
        }
    }

    glfwSetErrorCallback( glfwErrorCallback );

    blog::core( "loading GLFW {}.{}.{} ...", GLFW_VERSION_MAJOR, GLFW_VERSION_MINOR, GLFW_VERSION_REVISION );
    if ( !glfwInit() )
    {
        return absl::UnavailableError( "GLFW failed to initialise" );
    }

    // NB https://stackoverflow.com/questions/67345946/problem-with-imgui-when-using-glfw-opengl
    // GL 3.2 + GLSL 150
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR,     3 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR,     2 );
    glfwWindowHint( GLFW_OPENGL_PROFILE,            GLFW_OPENGL_CORE_PROFILE );
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT,     GL_TRUE );
    glfwWindowHint( GLFW_SAMPLES,                   2 );
    glfwWindowHint( GLFW_DOUBLEBUFFER,              1 );


    blog::core( "creating main window" );
    m_glfwWindow = glfwCreateWindow(
        m_feConfigCopy.appWidth,
        m_feConfigCopy.appHeight,
        fmt::format( FMTX( "OUROVEON // {} // " OURO_FRAMEWORK_CREDIT ), m_appName ).c_str(),
        nullptr,
        nullptr
    );
    if ( !m_glfwWindow )
    {
        glfwTerminate();
        return absl::UnavailableError( "GLFW unable to create main window" );
    }


    blog::core( "creating main GL context" );
    glfwMakeContextCurrent( m_glfwWindow );

    // set a saved position if we have one (and if it fits on screen); otherwise just centre on an appropriate monitor
    if ( m_feConfigCopy.appPositionValid && 
         glfwIsWindowPositionValid( m_glfwWindow, m_feConfigCopy.appPositionX, m_feConfigCopy.appPositionY ) )
    {
        glfwSetWindowPos( m_glfwWindow, m_feConfigCopy.appPositionX, m_feConfigCopy.appPositionY );
    }
    else
    {
        glfwSetWindowCenter( m_glfwWindow );
    }
    {
        m_isBorderless = m_feConfigCopy.isBorderless;
        applyBorderless();
    }
    // save the initial window pos/size, update (or create) the serialised version
    m_currentWindowGeometry = getWindowGeometry();
    m_windowGeometryChangedDelay = 0;
    updateAndSaveFrontendConfig();


    blog::core( "loading GLAD ..." );
    int32_t gladErr = gladLoadGLLoader( (GLADloadproc)glfwGetProcAddress );
    if ( gladErr == 0 )
    {
        return absl::UnavailableError( fmt::format( FMTX( "OpenGL loader failed, {}" ), gladErr ) );
    }

    glfwSwapInterval( 1 );

    // fetch desktop / display scaling factor and stash it for later use
    {
        // load configurable display scaling override
        config::Display displayConfig;
        const auto dcLoad = config::load( *appCore, displayConfig );
        if ( dcLoad != config::LoadResult::Success )
        {
            blog::core( FMTX( "display configuration did not load, using defaults" ) );
        }


        float displayScaleX = 1.0f, displayScaleY = 1.0f;
        glfwGetWindowContentScale( m_glfwWindow, &displayScaleX, &displayScaleY );

        // not sure what to do about this
        if ( displayScaleX != displayScaleY )
        {
            blog::error::app( "nonlinear display scaling found, X:{} Y:{}", displayScaleX, displayScaleY );
        }

        if ( displayConfig.useDisplayScale )
        {
            blog::app( "applying display scale override {}x", displayConfig.displayScale );
            displayScaleX = displayConfig.displayScale;
        }

        m_displayScale.set( displayScaleX );
        blog::app( "window display scale {}", m_displayScale.getDisplayScaleFactor() );
    }

    blog::core( "bound OpenGL {} | GLSL {}",
        (char*)glGetString( GL_VERSION ),                       // casts as glGetString returns uchar
        (char*)glGetString( GL_SHADING_LANGUAGE_VERSION ) );

    {
        GLint pformat, format, type;

        glGetInternalformativ( GL_TEXTURE_2D, GL_RGBA8, GL_INTERNALFORMAT_PREFERRED, 1, &pformat );
        glGetInternalformativ( GL_TEXTURE_2D, GL_RGBA8, GL_TEXTURE_IMAGE_FORMAT, 1, &format );
        glGetInternalformativ( GL_TEXTURE_2D, GL_RGBA8, GL_TEXTURE_IMAGE_TYPE, 1, &type );

        blog::core( "GL_INTERNALFORMAT_PREFERRED = ({:x}) {}", pformat, gl::enumToString(pformat) );
        blog::core( "    GL_TEXTURE_IMAGE_FORMAT = ({:x}) {}", format, gl::enumToString( format ) );
        blog::core( "      GL_TEXTURE_IMAGE_TYPE = ({:x}) {}", type, gl::enumToString( type ) );
    }

    {
        spacetime::ScopedTimer imguiTiming( "ImGui startup" );

        blog::core( "initialising ImGui {}", IMGUI_VERSION );

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        // install styles
        ImGui::StyleColorsDark();
        ApplyOuroveonImGuiStyle();


        // configure rendering with SDL/GL
        ImGui_ImplGlfw_InitForOpenGL( m_glfwWindow, true );
        ImGui_ImplOpenGL3_Init( nullptr );


        ImGuiIO& io = ImGui::GetIO();

        // configure where imgui will stash layout persistence
        {
            // default layouts are stashed in the shared/layouts path, eg. lore.default.ini
            m_imguiLayoutDefaultPath = appCore->getSharedDataPath() / "layouts" / fs::path( fmt::format( "{}.default.ini", appCore->GetAppCacheName() ) );
            if ( !fs::exists( m_imguiLayoutDefaultPath ) )
            {
                blog::error::core( "missing default layout .ini fallback [{}]", m_imguiLayoutDefaultPath.string() );
            }

            static constexpr size_t layoutPathMax = 256;

            m_imguiLayoutIni = new char[layoutPathMax];
            const auto layoutStore = ( appCore->getAppConfigPath() / "layout.current.ini" ).string();
            strncpy( m_imguiLayoutIni, layoutStore.c_str(), layoutPathMax - 1 );

            // if the current layout ini doesn't exist, manually pull the default one
            if ( !fs::exists( layoutStore ) )
            {
                reloadImguiLayoutFromDefault();
            }

            io.IniFilename                        = m_imguiLayoutIni;
        }
        // populate ImGui font selection
        {
            io.ConfigFlags                       |= ImGuiConfigFlags_DockingEnable;
            io.ConfigWindowsMoveFromTitleBarOnly  = true;
            io.ConfigDockingWithShift             = true;

            // setup default font with a good range of standard characters
            {
                ImFontConfig iconConfigEN;
                iconConfigEN.RasterizerMultiply = 1.2f;     // tighten contrast, I think it looks better

                static constexpr ImWchar fontRangeEN[] =
                {
                    // courtesy ChatGPT
                    0x0020, 0x007F, // Basic Latin
                    0x0080, 0x00FF, // Latin-1 Supplement
                    0x0100, 0x017F, // Latin Extended-A
                    0x0180, 0x024F, // Latin Extended-B
                    0x0250, 0x02AF, // IPA Extensions
                    0x02B0, 0x02FF, // Spacing Modifier Letters
                    0x0300, 0x036F, // Combining Diacritical Marks
                    0x0370, 0x03FF, // Greek and Coptic
                    0x0400, 0x052F, // Cyrillic
                    0xA640, 0xA69F, // Cyrillic Extended-B
                    0x1E00, 0x1EFF, // Latin Extended Additional
                    0x2000, 0x206F, // General Punctuation
                    0x2DE0, 0x2DFF, // Cyrillic Extended-A

                    // check Font.FiraCode.hpp
                    0x2580, 0x25F7, // Fira console shapes
                    0xEE00, 0xEE0B, // Fira progress bits
                    0,
                };

                m_fontFixed = io.Fonts->AddFontFromFileTTF(
                    FONT_AT(FsFixed_EN),
                    16.0f,
                    &iconConfigEN,
                    fontRangeEN );
            }

            // embed JP font choice
            {
                ImFontConfig iconConfigJP;
                iconConfigJP.RasterizerMultiply = 1.05f;
                iconConfigJP.MergeMode          = true;
                iconConfigJP.PixelSnapH         = true;

                static constexpr ImWchar fontRangeJP[] =
                {
                    // courtesy ChatGPT
                    0x3040, 0x309F, // Hiragana
                    0x30A0, 0x30FF, // Katakana
                    0x4E00, 0x9FFF, // Kanji
                    0xFF65, 0xFF9F, // Half-width Katakana
                    0x3000, 0x303F, // Japanese punctuation
                    0,
                };

                io.Fonts->AddFontFromFileTTF(
                    FONT_AT( FsFixed_JP ),
                    18.0f,
                    &iconConfigJP,
                    fontRangeJP );
            }

            // embed KR font choice
            {
                ImFontConfig iconConfigKR;
                iconConfigKR.RasterizerMultiply = 1.05f;
                iconConfigKR.MergeMode          = true;
                iconConfigKR.PixelSnapH         = true;

                static constexpr ImWchar fontRangeKR[] =
                {
                    // courtesy ChatGPT
                    0xAC00, 0xD7A3, // Hangul syllabary
                    0x1100, 0x1FF2, // Hangul Jamo
                    0,
                };

                io.Fonts->AddFontFromFileTTF(
                    FONT_AT( FsFixed_KR ),
                    18.0f,
                    &iconConfigKR,
                    fontRangeKR );
            }

            // embed emoji
            {
                ImFontConfig iconConfigEm;
                iconConfigEm.RasterizerMultiply = 1.1f;
                iconConfigEm.MergeMode          = true;
                iconConfigEm.PixelSnapH         = true;

                static constexpr ImWchar fontRangeEm[] =
                {
                    // a selection of common/popular emoji, not complete coverage
                    0x1F601, 0x1F64F,
                    0x1F300, 0x1F550,
                    0x1F910, 0x1F9FF,
                    0,
                };

                io.Fonts->AddFontFromFileTTF(
                    FONT_AT( FsFontEmoji ),
                    18.0f,
                    &iconConfigEm,
                    fontRangeEm );
            }

            // embed FontAwesome glyphs
            {
                ImFontConfig iconConfigFA;
                iconConfigFA.MergeMode          = true;
                iconConfigFA.PixelSnapH         = true;
                iconConfigFA.GlyphOffset        = ImVec2( 0.0f, 2.0f );
                iconConfigFA.GlyphExtraSpacing  = ImVec2( 1.0f, 0.0f );

                static constexpr ImWchar fontRangeFA[] =
                {
                    ICON_MIN_FA, ICON_MAX_FA,
                    0
                };
                io.Fonts->AddFontFromFileTTF( FONT_AT(FsFontAwesome), 15.0f, &iconConfigFA, fontRangeFA );

                static constexpr ImWchar fontRangeFAB[] =
                {
                    ICON_MIN_FAB, ICON_MAX_FAB,
                    0
                };
                io.Fonts->AddFontFromFileTTF( FONT_AT( FsFontAwesomeBrands ), 15.0f, &iconConfigFA, fontRangeFAB );
            }

            m_fixedSmaller = io.Fonts->AddFontDefault();
            m_fontLogo     = io.Fonts->AddFontFromFileTTF( FONT_AT(FsTitle),  56.0f );
            m_fontMedium   = io.Fonts->AddFontFromFileTTF( FONT_AT(FsMedium), 50.0f );

            ImGuiFreeType::BuildFontAtlas( io.Fonts, ImGuiFreeTypeBuilderFlags_LightHinting );
        }
        {
            m_markdownConfig.linkIcon = ICON_FA_LINK;
            m_markdownConfig.headingFormats[0].font = m_fontLogo;
            m_markdownConfig.headingFormats[0].separator = false;
            m_markdownConfig.headingFormats[1].font = m_fontMedium;
            m_markdownConfig.linkCallback = &Frontend::MarkdownLinkHandler;
            m_markdownConfig.userData = this;
            m_markdownConfig.formatCallback = &Frontend::MarkdownFormalCallback;
        }
    }

    #undef FONT_AT
    return absl::OkStatus();
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::destroy()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow( m_glfwWindow );
    glfwTerminate();

    m_glfwWindow = nullptr;

    Module::destroy();
}

// ---------------------------------------------------------------------------------------------------------------------
bool Frontend::appTick()
{
    bool shouldQuit = m_quitRequested;
         shouldQuit |= ( glfwWindowShouldClose( m_glfwWindow ) != 0 );

    // track changes to the window size/position and write out new configuration after a delay
    // (to absorb rapid changes, such as is likely when dragging around windows)
    const auto windowGeometry = getWindowGeometry();
    if ( m_currentWindowGeometry != windowGeometry )
    {
        m_currentWindowGeometry = windowGeometry;
        m_windowGeometryChangedDelay = 60;
    }
    if ( m_windowGeometryChangedDelay > 0 )
    {
        m_windowGeometryChangedDelay--;
        if ( m_windowGeometryChangedDelay == 0 )
            updateAndSaveFrontendConfig();
    }

    GLFWwindow* currentContext = glfwGetCurrentContext();
    if ( currentContext != m_glfwWindow )
        glfwMakeContextCurrent( m_glfwWindow );

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Scoped::TickPulses();

    return shouldQuit;
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::appRenderBegin()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::Render();

    // reset GL rendering state
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    glViewport( 0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y );
    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::appRenderImguiDispatch()
{
    ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::appRenderFinalise()
{
    glfwSwapBuffers( m_glfwWindow );
    glfwPollEvents();
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::toggleBorderless()
{
    m_isBorderless = !m_isBorderless;
    applyBorderless();
    updateAndSaveFrontendConfig();
}

// ---------------------------------------------------------------------------------------------------------------------
Frontend::WindowGeometry Frontend::getWindowGeometry() const
{
    WindowGeometry result;
    glfwGetWindowSize( m_glfwWindow, &result.m_width, &result.m_height );
    glfwGetWindowPos( m_glfwWindow, &result.m_positionX, &result.m_positionY );
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::updateAndSaveFrontendConfig()
{
    // 0 size usually indicates a minimization, ignore it
    if ( m_currentWindowGeometry.m_width == 0 ||
        m_currentWindowGeometry.m_height == 0 )
    {
        blog::core( FMTX( "window minimized, ignoring change ..." ) );
        return;
    }

    m_feConfigCopy.appWidth         = m_currentWindowGeometry.m_width;
    m_feConfigCopy.appHeight        = m_currentWindowGeometry.m_height;
    m_feConfigCopy.appPositionX     = m_currentWindowGeometry.m_positionX;
    m_feConfigCopy.appPositionY     = m_currentWindowGeometry.m_positionY;
    m_feConfigCopy.appPositionValid = true;
    m_feConfigCopy.isBorderless     = m_isBorderless;

    blog::core( FMTX( "window state change; [{}, {}] [{} x {}] [{}], saving changes ..." ),
        m_feConfigCopy.appPositionX,
        m_feConfigCopy.appPositionY,
        m_feConfigCopy.appWidth,
        m_feConfigCopy.appHeight,
        m_feConfigCopy.isBorderless ? "borderless" : "bordered"
    );

    ABSL_ASSERT( m_appCore.has_value() );
    const auto feSave = config::save( *m_appCore.value(), m_feConfigCopy );
    if ( feSave != config::SaveResult::Success )
    {
        blog::error::core( "unable to find or save [{}]", config::Frontend::StorageFilename );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::applyBorderless() const
{
    glfwSetWindowAttrib( m_glfwWindow, GLFW_DECORATED, m_isBorderless ? 0 : 1 );
    glfwSetWindowAttrib( m_glfwWindow, GLFW_RESIZABLE, m_isBorderless ? 0 : 1 );
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::titleText( const char* label ) const
{
    ImGui::PushFont( getFont( app::module::Frontend::FontChoice::MediumTitle ) );
    ImGui::TextUnformatted( label );
    ImGui::PopFont();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::reloadImguiLayoutFromDefault() const
{
    blog::core( FMTX("imgui loading layout [{}] ..."), m_imguiLayoutDefaultPath.string() );
    
    ImGui::LoadIniSettingsFromDisk( m_imguiLayoutDefaultPath.string().c_str() );
    ImGui::MarkIniSettingsDirty();
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::resetWindowPositionAndSizeToDefault()
{
    m_isBorderless = true;
    applyBorderless();

    glfwSetWindowSize( m_glfwWindow, config::Frontend::DefaultWidth, config::Frontend::DefaultHeight );
    glfwSetWindowCenter( m_glfwWindow );

    m_currentWindowGeometry = getWindowGeometry();
    updateAndSaveFrontendConfig();
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::MarkdownLinkHandler( const ImGui::MarkdownLinkCallbackData& data )
{
    std::string linkText( data.link, data.linkLength ); // copy out exact text into temporary buffer
    blog::core( "markdown link launching [{}]", linkText );
    xpOpenURL( linkText.c_str() );
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::MarkdownFormalCallback( const ImGui::MarkdownFormatInfo& markdownFormatInfo_, bool start_ )
{
    ImGui::defaultMarkdownFormatCallback( markdownFormatInfo_, start_ );

    switch ( markdownFormatInfo_.type )
    {
        case ImGui::MarkdownFormatType::HEADING:
        {
            if ( markdownFormatInfo_.level == 1 )
            {
                if ( start_ )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetColorU32( ImGuiCol_NavHighlight ) );
                }
                else
                {
                    ImGui::PopStyleColor();
                }
            }
            break;
        }

        case ImGui::MarkdownFormatType::LINK:
        {
            if ( start_ )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, colour::shades::pink.dark() );
            }
            else
            {
                ImGui::PopStyleColor();
                if ( markdownFormatInfo_.itemHovered )
                {
                    ImGui::UnderLine( colour::shades::pink.light() );
                }
                else
                {
                    ImGui::UnderLine( colour::shades::pink.dark() );
                }
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void Frontend::imguiRenderMarkdown( std::string_view markdownText ) const
{
    ImGui::Markdown( markdownText.data(), markdownText.size(), m_markdownConfig );
}

} // namespace module
} // namespace app
