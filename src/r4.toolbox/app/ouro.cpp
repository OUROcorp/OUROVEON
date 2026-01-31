//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#include "pch.h"

#include "spacetime/chronicle.h"
#include "base/text.h"
#include "colour/preset.h"

#include "config/frontend.h"
#include "config/data.h"
#include "config/audio.h"
#include "config/spectrum.h"

#include "app/ouro.h"
#include "app/imgui.ext.h"
#include "app/module.audio.h"
#include "app/module.frontend.h"
#include "app/module.frontend.fonts.h"
#include "app/module.midi.h"

#include "mix/common.h"
#include "mix/stem.amalgam.h"

#include "endlesss/all.h"

#include "platform_folders.h"

#include "ux/riff.feedshare.h"
#include "ux/cache.migrate.h"
#include "ux/cache.trim.h"

#include "io/tarch.h"
#include "xp/open.url.h"

using namespace std::chrono_literals;

namespace app {

// double-clicking on the OUROVEON logo shows a few extra secret options 
struct AdvancedOptionsBlock
{
    bool    bShow = false;
#if OURO_HAS_NDLS_ONLINE
    bool    bEnableNetworkLogging = false;
    bool    bEnableLastMinuteQuirkFixes = false;
#endif // OURO_HAS_NDLS_ONLINE
};

// ---------------------------------------------------------------------------------------------------------------------
// endlesss::live::RiffFetchServices
int32_t OuroApp::getSampleRate() const
{
    ABSL_ASSERT( m_mdAudio != nullptr );    // can't be fetching riffs until all the core services are running
    return m_mdAudio->getSampleRate();
}

// ---------------------------------------------------------------------------------------------------------------------
// run a loop with a dedicated configuration page for choosing output device per session; once done, we jump to the app code
//
int OuroApp::EntrypointGUI()
{
    static constexpr std::array< const char*, 5 > cSampleRateLabels { "44100", "48000", "88200", "96000", "192000" };
    static constexpr std::array< uint32_t,    5 > cSampleRateValues {  44100 ,  48000 ,  88200 ,  96000 ,  192000  };
    static constexpr std::array< const char*, 8 > cBufferSizeLabels {  "Auto",    "32",    "64",   "256",   "512",  "1024",  "2048",  "4096" };
    static constexpr std::array< uint32_t,    8 > cBufferSizeValues {      0 ,     32 ,     64 ,    256 ,    512 ,   1024 ,   2048 ,   4096  };
    static constexpr std::array< const char*, 6 > cVibeRenderLabels {   "512",  "1024",  "2048",  "4096" };
    static constexpr std::array< uint32_t,    6 > cVibeRenderValues {    512 ,   1024 ,   2048 ,   4096  };

    AdvancedOptionsBlock advancedOptionsBlock;

    // fetch any customised stem export spec
    const auto exportLoadResult = config::load( *this, m_configExportOutput );

    // try and fetch last audio settings from the stash; doesn't really matter if we can't, it is just saved defaults
    // for the config screen
    config::Audio audioConfig;
    const auto audioLoadResult = config::load( *this, audioConfig );
    if ( audioLoadResult != config::LoadResult::Success )
    {
        blog::cfg( "No default audio settings found, using defaults" );
    }

    // spectrum/scope config block also passed to audio engine setup; defaults is fine
    config::Spectrum audioSpectrumConfig;
    const auto spectrumLoadResult = config::load( *this, audioSpectrumConfig );
    if ( spectrumLoadResult != config::LoadResult::Success &&
         spectrumLoadResult != config::LoadResult::CannotFindConfigFile )
    {
        blog::cfg( "Spectrum config file failed to load, using defaults" );
    }

    // get default value strings for saved config values to display in the UI; these are updated if selection changes
    std::string previewSampleRate = ImGui::ValueArrayPreviewString( cSampleRateLabels, cSampleRateValues, audioConfig.sampleRate );
    std::string previewBufferSize = ImGui::ValueArrayPreviewString( cBufferSizeLabels, cBufferSizeValues, audioConfig.bufferSize );

    // add UTC/Server time on left of status bar
    const auto sbbTimeStatusLeftID = registerStatusBarBlock( app::CoreGUI::StatusBarAlignment::Left, 375.0f, []()
    {
        const auto timezoneUTC = date::locate_zone( "Etc/UTC" );
        auto t  = date::make_zoned( timezoneUTC, std::chrono::system_clock::now() );
        auto tf = date::format( spacetime::defaultDisplayTimeFormatTZ, t );
        const auto serverTime = fmt::format( FMTX( " {} | {}" ), timezoneUTC->name(), tf );

        ImGui::PushStyleColor( ImGuiCol_Text, IM_COL32_WHITE );
        ImGui::TextUnformatted( serverTime );
        ImGui::PopStyleColor();
    });
    // add local timezone time on right 
    const auto sbbTimeStatusRightID = registerStatusBarBlock( app::CoreGUI::StatusBarAlignment::Left, 375.0f, []()
    {
        const auto timezoneLocal = date::current_zone();
        auto t  = date::make_zoned( timezoneLocal, std::chrono::system_clock::now() );
        auto tf = date::format( spacetime::defaultDisplayTimeFormatTZ, t );
        const auto localTime = fmt::format( FMTX( " {} | {}" ), timezoneLocal->name(), tf );

        ImGui::PushStyleColor( ImGuiCol_Text, IM_COL32_WHITE );
        ImGui::TextUnformatted( localTime );
        ImGui::PopStyleColor();
    });

    // background task activity display
    const auto sbbAsyncTaskActivity = registerStatusBarBlock( app::CoreGUI::StatusBarAlignment::Right, 100.0f, [this]()
    {
        if ( m_asyncTaskActivityIntensity > 0 )
        {
            const std::string pulseOverview = pulseSlotsToString( "BUSY ", m_asyncTaskPulseSlots );

            ImGui::TextUnformatted( pulseOverview.c_str() );
        }
    });


    // network activity display
    const auto sbbNetworkActivity = registerStatusBarBlock( app::CoreGUI::StatusBarAlignment::Right, 300.0f, [this]()
    {
        const std::string pulseOverview = pulseSlotsToString( " ", m_avgNetPulseHistory);

        const auto kbAvgPayload = m_avgNetPayloadPerSec.get() / 1024.0;
        const auto networkState = fmt::format( FMTX( "{}  {:>6.1f} Kb/s " ), pulseOverview, kbAvgPayload );

        ImGui::TextUnformatted( networkState );
    });


    // load any saved configs
    config::endlesss::Auth endlesssAuth;
    /* const auto authLoadResult    = */ config::load( *this, endlesssAuth );

    // #HDD check and do something with config load results?

#if !OURO_HAS_NDLS_ONLINE
    // "no-network" config bits, doesn't matter if it doesn't exist
    if ( config::load( *this, m_configNoNet ) == config::LoadResult::Success )
        m_noNetImpersonationUser.setUsername( m_configNoNet.impersonationUsername );
#endif // !OURO_HAS_NDLS_ONLINE



    app::AudioDeviceQuery adq;
    int32_t chosenDeviceIndex;

    // fn to take the current settings and run a query
    const auto fnUpdateDeviceQuery = [&]
    {
        chosenDeviceIndex = -1;

        adq.findSuitable( audioConfig );

        // see if the last device we had selected still exists
        if ( !audioConfig.lastDevice.empty() )
        {
            for ( int32_t dI = 0; dI < (int32_t)adq.m_deviceNames.size(); dI++ )
            {
                if ( adq.m_deviceNames[dI] == audioConfig.lastDevice )
                {
                    chosenDeviceIndex = dI;
                    break;
                }
            }
        }
    };
    // prefill a device query; this will be re-run if the settings change
    fnUpdateDeviceQuery();

    // task / state for fetching metadata 
    auto asyncFetchState = endlesss::cache::Jams::AsyncFetchState::None;
    std::string asyncState;
    tf::Taskflow taskFlow;

    // buffer for changing data storage path; #HDD TODO per platform limits for dataStoragePathBufferSize
    constexpr size_t dataStoragePathBufferSize = 255;
    char dataStoragePathBuffer[dataStoragePathBufferSize];
    memset( dataStoragePathBuffer, 0, dataStoragePathBufferSize );
    if ( m_configData == std::nullopt )
    {
        // if we are starting with no config data, put a suggestion in
        const fs::path initalPathSuggestion = fs::path( sago::getDocumentsFolder() ) / "OUROVEON";
        strcpy( dataStoragePathBuffer, initalPathSuggestion.string().c_str() );
    }
    else
    {
        // already got one!
        strcpy( dataStoragePathBuffer, m_configData->storageRoot.c_str() );
    }

    // ::filesystem variant of the buffer and computed free-space data, updated via a lambda that can be called
    // if the buffer entry changes during imgui update
    fs::path        dataStoragePath;
    std::error_code dataStorageSpaceInfoError;
    fs::space_info  dataStorageSpaceInfo;
    std::string     dataStorageSpaceText;

    const auto UpdateLocalFsRecordsForDataStoragePath = [&]()
    {
        dataStoragePath          = fs::path( dataStoragePathBuffer );
        dataStorageSpaceInfo     = std::filesystem::space( dataStoragePath, dataStorageSpaceInfoError );

        if ( dataStorageSpaceInfoError )
            dataStorageSpaceText = dataStorageSpaceInfoError.message();
        else
            dataStorageSpaceText = base::humaniseByteSize( "Free space : ", dataStorageSpaceInfo.available );

        // in the case we have an existing config and someone just changed the data path in the UI, 
        // nix the data instance so we have to go through the [Accept & Configure] flow
        if ( m_configData != std::nullopt &&
             m_configData->storageRoot != dataStoragePath.string() )
        {
            m_configData   = std::nullopt;
            m_storagePaths = std::nullopt;
        }
    };
    UpdateLocalFsRecordsForDataStoragePath();

    const auto GetNetworkQuality = [this]() {
        return m_configPerf.enableUnstableNetworkCompensation ?
            endlesss::api::NetConfiguration::NetworkQuality::Unstable :
            endlesss::api::NetConfiguration::NetworkQuality::Stable;
        };


    // used if we need to pop a modal showing some error feedback
    static constexpr auto popupErrorModalName = "Error";
    std::string popupErrorMessage;

    bool bEnableCacheManagementMenu = false;

    bool successfulBreakFromLoop = false;
#if OURO_HAS_NDLS_ONLINE
    bool endlesssAuthExpired = false;
#endif // OURO_HAS_NDLS_ONLINE

    // configuration preflight
    while ( beginInterfaceLayout( CoreGUI::VF_WithStatusBar ) )
    {
        std::string popupErrorMessageToDisplay;

        ImGui::SetNextWindowContentSize( ImVec2( 700.0f, 100.0f ) );
        if ( ImGui::BeginPopupModal( popupErrorModalName, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize ) )
        {
            ImGui::TextUnformatted( popupErrorMessage.c_str() );

            ImGui::Spacing();
            ImGui::Spacing();

            if ( ImGui::Button( " Close " ) )
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }


        std::string progressionInhibitionReason;

        // hold on preflight setup if any module reports they are busy
        if ( m_mdAudio->isPreflightSetupComplete() == false )
            progressionInhibitionReason = "Waiting on audio module ...";
        if ( m_mdFrontEnd->isPreflightSetupComplete() == false )
            progressionInhibitionReason = "Waiting on front-end module ...";
        if ( m_mdMidi->isPreflightSetupComplete() == false )
            progressionInhibitionReason = "Waiting on MIDI module ...";


        const float  configWindowColumn1 = 500.0f;
        const float  configWindowColumn2 = 500.0f;
        const ImVec2 configWindowSize = ImVec2( configWindowColumn1 + configWindowColumn2, 660.0f );

        if ( ImGui::BeginFixedCenteredWindow( "Framework Preflight | Version " OURO_FRAMEWORK_VERSION, configWindowSize, ImVec2( 0, 50.0f ) ) )
        {
            constexpr float perColumnIndent = 5.0f;
            constexpr float perBlockIndent = 10.0f;

            // ImGuiTableFlags_SizingFixedFit #IMGUIUPGRADE
            if ( ImGui::BeginTable( "##databusknobs", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings ) )
            {
                ImGui::TableSetupColumn( "X", ImGuiTableColumnFlags_WidthFixed, configWindowColumn1 );
                ImGui::TableSetupColumn( "Y", ImGuiTableColumnFlags_WidthFixed, configWindowColumn2 );

                ImGui::TableNextColumn();
                ImGui::Indent( perColumnIndent );


                // ---------------------------------------------------------------------------------------------------------
                // hi everybody 

                {
                    ImGui::Spacing();
                    ImGui::Spacing();

                    if ( advancedOptionsBlock.bShow )
                    {
                        ImGui::TextUnformatted( "Advanced Toggles" );
#if OURO_HAS_NDLS_ONLINE
                        ImGui::Checkbox( " Enable Network Diagnostics", &advancedOptionsBlock.bEnableNetworkLogging );
                        ImGui::Checkbox( " Enable Last Minute Fixes", &advancedOptionsBlock.bEnableLastMinuteQuirkFixes );
#endif // OURO_HAS_NDLS_ONLINE
                        ImGui::Checkbox( " Cache Management Tools", &bEnableCacheManagementMenu );
                    }
                    else
                    {
                        ImGui::PushFont( m_mdFrontEnd->getFont( app::module::Frontend::FontChoice::LargeLogo ) );
                        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetColorU32( ImGuiCol_NavHighlight ) );
                        ImGui::TextUnformatted( "OUROVEON " );
                        if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                            advancedOptionsBlock.bShow = true;

                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    }

                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::ColumnSeparatorBreak();
                }


                // ---------------------------------------------------------------------------------------------------------
                // where do we store our data
                // a complicated setup system for choosing a path to use for data storage and all the edge cases that includes
                {
                    std::error_code filesysError;

                    m_mdFrontEnd->titleText( "Installation" );
                    ImGui::Indent( perBlockIndent );

                    const ImVec2 pathButtonSize( configWindowColumn1 - 35.0f, 32.0f );
                    ImGui::PushItemWidth( pathButtonSize.x );

                    ImGui::TextUnformatted( "Set Data Storage Root Path :" );
                    const bool textAccept = ImGui::InputText( "##DataPath", dataStoragePathBuffer, dataStoragePathBufferSize, ImGuiInputTextFlags_EnterReturnsTrue );
                    if ( textAccept || ImGui::IsItemDeactivatedAfterEdit() )
                    {
                        UpdateLocalFsRecordsForDataStoragePath();
                    }
                    const bool textFocus = ImGui::IsItemActive();

                    ImGui::Spacing();

                    // please type something
                    if ( dataStoragePath.empty() )
                    {
                        ImGui::BeginDisabledControls( true );
                        ImGui::Button( "No path entered", pathButtonSize );
                        ImGui::EndDisabledControls( true );
                    }
                    // offer to create the directory if it isn't present; requiring it to exist lets us do a better job upfront of 
                    // validating a potential storage site and showing some stats too
                    else if ( !fs::exists( dataStoragePath, filesysError ) )
                    {
                        ImGui::TextUnformatted( "This path does not exist. Would you like to create it?" );
                        ImGui::BeginDisabledControls( textFocus );
                        if ( ImGui::Button( "Create Directory", pathButtonSize ) )
                        {
                            fs::create_directories( dataStoragePath, filesysError );

                            if ( filesysError )
                            {
                                popupErrorMessageToDisplay = fmt::format( "Error occured while creating directory :\n{}", filesysError.message() );
                            }
                            else
                            {
                                // recheck the storage space, update state variables
                                UpdateLocalFsRecordsForDataStoragePath();
                            }
                        }
                        ImGui::EndDisabledControls( textFocus );
                    }
                    else
                    {
                        // show the drive space for the current path, just in case you realise you're about to obliterate
                        // your last good gigabyte with stems
                        ImGui::TextUnformatted( dataStorageSpaceText.c_str() );

                        if ( !fs::is_directory( dataStoragePath, filesysError ) )
                        {
                            ImGui::BeginDisabledControls( true );
                            ImGui::Button( "Path is not a directory", pathButtonSize );
                            ImGui::EndDisabledControls( true );
                        }
                        if ( dataStoragePath == dataStoragePath.root_path() )
                        {
                            ImGui::BeginDisabledControls( true );
                            ImGui::Button( "Cannot use a root drive path", pathButtonSize );
                            ImGui::EndDisabledControls( true );
                        }
                        else
                        {
                            const bool alreadyConfigured = ( m_configData.has_value() && m_storagePaths.has_value() );

                            ImGui::BeginDisabledControls( textFocus || alreadyConfigured );
                            if ( ImGui::Button( textFocus ? "[ Path Being Edited ]" : "Validate & Save", pathButtonSize ) )
                            {
                                config::Data newDataConfig;
                                newDataConfig.storageRoot = dataStoragePath.string();

                                m_configData = newDataConfig;

                                // save new state
                                const auto authSaveResult = config::save( *this, newDataConfig );
                                if ( authSaveResult != config::SaveResult::Success )
                                {
                                    popupErrorMessageToDisplay = fmt::format( "Unable to save storage configuration to [{}]", config::getFullPath< config::Data >( *this ).string() );
                                }

                                // temporarily create a StoragePaths instance so that we can then create the subdirectories / validate their state
                                m_storagePaths = StoragePaths( m_configData.value(), GetAppCacheName() );
                                if ( !m_storagePaths->tryToCreateAndValidate() )
                                {
                                    popupErrorMessageToDisplay = fmt::format( "Unable to create or validate storage directories (check log)" );
                                }
                                m_storagePaths = std::nullopt;
                            }
                            ImGui::EndDisabledControls( textFocus || alreadyConfigured );
                        }
                    }
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    ImGui::Spacing();

                    if ( m_configData != std::nullopt )
                    {
                        m_storagePaths = StoragePaths( m_configData.value(), GetAppCacheName());

                        bool cacheAppValid      = fs::exists( m_storagePaths->cacheApp );
                        bool cacheCommonValid   = fs::exists( m_storagePaths->cacheCommon );
                        bool outputAppValid     = fs::exists( m_storagePaths->outputApp );

                        const auto showValidatedStoragePath = []( bool isValid, const fs::path& path, const char* context )
                        {
                            if ( isValid )
                                ImGui::TextColored( ImGui::GetStyleColorVec4( ImGuiCol_NavHighlight ), ICON_FA_CIRCLE_CHECK " %s", context );
                            else
                                ImGui::TextDisabled( ICON_FA_CIRCLE_STOP " %s [Not Found]", context );

                            ImGui::CompactTooltip( path.string().c_str() );
                        };

                        showValidatedStoragePath( cacheCommonValid, m_storagePaths->cacheCommon, "Shared Cache" );
                        showValidatedStoragePath( cacheAppValid,    m_storagePaths->cacheApp,    "Application Cache" );
                        showValidatedStoragePath( outputAppValid,   m_storagePaths->outputApp,   "Application Output" );

                        // hold up progress until we have storage paths validated
                        if ( !cacheAppValid     ||
                             !cacheCommonValid  ||
                             !outputAppValid )
                        {
                            m_storagePaths = std::nullopt;
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled( ICON_FA_CIRCLE " ..." );
                        ImGui::TextDisabled( ICON_FA_CIRCLE " ..." );
                        ImGui::TextDisabled( ICON_FA_CIRCLE " ..." );
                    }

                    ImGui::Unindent( perBlockIndent );
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::ColumnSeparatorBreak();
                }

                // storage not yet configured, don't continue with the rest until this is done
                if ( !m_storagePaths.has_value() )
                {
                    ImGui::Unindent( perColumnIndent );
                    ImGui::TableNextColumn();
                    ImGui::EndTable();
                }
                else
                {
                    static bool endlesssWorkUnauthorised = false;
                    {
                        m_mdFrontEnd->titleText( "Endlesss Accesss" );
                        ImGui::Indent( perBlockIndent );

#if !OURO_HAS_NDLS_ONLINE

                        endlesssWorkUnauthorised = true;

                        ImGui::TextColored(
                            colour::shades::callout.light(),
                            "Endlesss has ceased operations, servers are unavailable." );
                        ImGui::TextColored(
                            colour::shades::callout.neutral(),
                            "All network access will be disabled." );

                        ImGui::Spacing();
                        ImGui::Spacing();

                        {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled( "[?]" );
                            ImGui::CompactTooltip( "Enter a username to identify as for this session, for any views that work best with a known/default user" );
                            ImGui::SameLine();
                            ImGui::TextUnformatted( "Identify As" );
                            ImGui::SameLine();

                            if ( m_noNetImpersonationUser.imgui( "##impersonateusername", getEndlesssPopulation(), ImGui::ux::UserSelector::cDefaultWidthForUserSize ) )
                            {
                                m_configNoNet.impersonationUsername = m_noNetImpersonationUser.getUsername();

                                const auto noNetSaveResult = config::save( *this, m_configNoNet );
                                if ( noNetSaveResult != config::SaveResult::Success )
                                {
                                    blog::error::cfg( "Unable to save no-net configuration" );
                                }
                            }
                        }

                        if ( m_networkConfiguration->hasNoAccessSet() )
                            m_networkConfiguration->initWithoutAuthentication( m_appEventBus, m_configEndlesssAPI );
                    
#else // !OURO_HAS_NDLS_ONLINE

                    // ---------------------------------------------------------------------------------------------------------
                    // check in on our configured access to Endlesss' services

                        {
                            ImGui::Checkbox( " Unstable Network Compensation", &m_configPerf.enableUnstableNetworkCompensation );
                            ImGui::SameLine();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled( "[?]" );
                            ImGui::CompactTooltip( "Enable if connecting over less reliable networks or via 4G/mobile\nto have increased API retries on failures\nplus longer time-out allowance on all calls" );
                        }

                        // used to enable/disable buttons if the jam cache update thread is working
                        const bool jamsAreUpdating = (asyncFetchState == endlesss::cache::Jams::AsyncFetchState::Working);

                        // endlesss unix times are in nano precision
                        const auto authExpiryUnixTime = endlesssAuth.expires / 1000;

                        uint32_t expireDays, expireHours, expireMins, expireSecs;
                        endlesssAuthExpired = !spacetime::datestampUnixExpiryFromNow( authExpiryUnixTime, expireDays, expireHours, expireMins, expireSecs );

                        {
                            const bool offerNoAuthMode = ( supportsUnauthorisedEndlesssMode() && endlesssAuthExpired );
                            {
                                ImGui::Scoped::Enabled se( offerNoAuthMode );
                                ImGui::Checkbox( " Continue without Authentication", &endlesssWorkUnauthorised );
                            }
                            ImGui::SameLine();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled( "[?]" );

                            // give context-aware tooltip help
                            if ( !supportsUnauthorisedEndlesssMode() )
                                ImGui::CompactTooltip( "This app does not support 'No Authentication' Mode" );
                            else if ( endlesssAuthExpired )
                                ImGui::CompactTooltip( "Enable to allow booting without valid Endlesss\nauthentication, limiting some features" );
                            else
                                ImGui::CompactTooltip( "'No Authentication' mode only available when signed-out of Endlesss" );

                            ImGui::Spacing();
                        }

                        if ( endlesssAuthExpired )
                        {
                            ImGui::TextColored(
                                colour::shades::errors.neutral(),
                                "Authentication expired, please sign in to refresh" );
                        }
                        else
                        {
                            {
                                ImGui::Scoped::Disabled sd( jamsAreUpdating );
                                if ( ImGui::IconButton( ICON_FA_RIGHT_FROM_BRACKET ) )
                                {
                                    // zero out the disk copy and our local cache
                                    config::endlesss::Auth emptyAuth;
                                    config::save( *this, emptyAuth );
                                    config::load( *this, endlesssAuth );

                                    // wipe out any existing auth details in the network config
                                    if ( !m_networkConfiguration->hasNoAccessSet() )
                                        m_networkConfiguration->initWithoutAuthentication( m_appEventBus, m_configEndlesssAPI );
                                }
                            }
                            ImGui::CompactTooltip( "Log out" );

                            ImGui::SameLine( 0, 12.0f );
                            
                            ImGui::TextColored(
                                colour::shades::tag_lvl_1.neutral(),
                                "%s", endlesssAuth.user_id.c_str() );
                            ImGui::SameLine();
                            ImGui::TextUnformatted( "access expires in" );
                            ImGui::SameLine();
                            ImGui::TextColored(
                                colour::shades::callout.neutral(),
                                "%u day(s), %u hours", expireDays, expireHours );
                        }

                        if ( endlesssAuthExpired )
                        {
                            if ( !endlesssWorkUnauthorised )
                                progressionInhibitionReason = "Endlesss log-in has expired";
                        }

                        ImGui::Spacing();

                        // allow direct login to endlesss services; this is the same path as the website and app takes
                        if ( endlesssAuthExpired )
                        {
                            static char localLoginName[64];
                            static char localLoginPass[64];

                            ImGui::TextUnformatted( "User/Pass :" );
                            ImGui::SameLine();
                            ImGui::PushItemWidth( 140.0f );
                            ImGui::InputText( "##username", localLoginName, 60 );
                            ImGui::SameLine();
                            ImGui::InputText( "##password", localLoginPass, 60, ImGuiInputTextFlags_Password );
                            ImGui::PopItemWidth();
                            ImGui::SameLine();
                            if ( ImGui::Button( "Login" ) )
                            {
                                auto dataClient = std::make_unique< httplib::SSLClient >( "api.endlesss.fm" );

                                dataClient->set_ca_cert_path( m_configEndlesssAPI.certBundleRelative.c_str() );
                                dataClient->enable_server_certificate_verification( true );

                                // post user/pass to /auth/login as a json object
                                auto authBody = fmt::format( R"({{ "username" : "{}", "password" : "{}" }})", localLoginName, localLoginPass );
                                auto res = dataClient->Post(
                                    "/auth/login",
                                    authBody,
                                    "application/json" );

                                // the expected result is a json document that config::endlesss::Auth is a subset of
                                // so we deserialize direct into that
                                const auto memoryLoadResult = config::loadFromMemory( res->body, endlesssAuth );
                                if ( memoryLoadResult != config::LoadResult::Success )
                                {
                                    blog::error::cfg( "Unable to parse Endlesss credentials from json" );

                                    // try and present the user some info about why we failed to login
                                    config::endlesss::AuthFailure authFailureDetails;
                                    try
                                    {
                                        std::istringstream is( res->body );
                                        cereal::JSONInputArchive archive( is );

                                        authFailureDetails.serialize( archive );
                                    }
                                    catch ( cereal::Exception& cEx )
                                    {
                                        authFailureDetails.message = fmt::format( "Authentication failed with unknown error\n{}", cEx.what() );
                                    }
                                    if ( !authFailureDetails.message.empty() )
                                        popupErrorMessageToDisplay = authFailureDetails.message;
                                }
                                else
                                {
                                    // .. if parsing was successful, save the key bits we need back to disk for future use
                                    const auto authSaveResult = config::save( *this, endlesssAuth );
                                    if ( authSaveResult != config::SaveResult::Success )
                                    {
                                        blog::error::cfg( "Unable to re-save Endlesss authentication data" );
                                    }
                                }
                            }
                        }
                        else
                        {
                            // if not yet setup, configure the network for full authenticated access
                            if ( !m_networkConfiguration->hasAccess( endlesss::api::NetConfiguration::Access::Authenticated ) &&
                                 !endlesssAuth.password.empty() )
                            {
                                m_networkConfiguration->initWithAuthentication( m_appEventBus, m_configEndlesssAPI, endlesssAuth );
                            }

                            // stop closing the boot window if we're running background threads or we have no jam data
                            if ( !m_jamLibrary.hasJamData() )
                                progressionInhibitionReason = "No jam metadata found";

                            ImGui::BeginDisabledControls( jamsAreUpdating );
                            {
                                if ( ImGui::IconButton( ICON_FA_ARROWS_ROTATE ) )
                                {
                                    m_jamLibrary.asyncCacheRebuild(
                                        *m_networkConfiguration,
                                        endlesssAuth.sync_options,
                                        taskFlow,
                                        [&]( const endlesss::cache::Jams::AsyncFetchState state, const std::string& status )
                                        {
                                            asyncFetchState = state;
                                            asyncState      = status;

                                            if ( state == endlesss::cache::Jams::AsyncFetchState::Success )
                                                m_jamLibrary.save( *this );
                                        });
                                    m_taskExecutor.run( taskFlow );
                                }
                                ImGui::CompactTooltip( "Sync and update your list of jams\nand current join-ins" );
                            }
                            ImGui::EndDisabledControls( jamsAreUpdating );
                            ImGui::SameLine( 0, 12.0f );
                            {
                                ImGui::TextUnformatted( "Jam Cache :" );
                                ImGui::SameLine();
                                {
                                    ImGui::PushStyleColor( ImGuiCol_Text, colour::shades::callout.neutral() );

                                    switch ( asyncFetchState )
                                    {
                                    case endlesss::cache::Jams::AsyncFetchState::Success:
                                    case endlesss::cache::Jams::AsyncFetchState::None:
                                        ImGui::TextUnformatted( m_jamLibrary.getCacheFileState().c_str() );
                                        break;

                                    case endlesss::cache::Jams::AsyncFetchState::Working:
                                        ImGui::Spinner( "##syncing", true, ImGui::GetTextLineHeight() * 0.3f, 3.0f, 0.0f, ImGui::GetColorU32( ImGuiCol_Text ) );
                                        ImGui::SameLine( 0, 14.0f );
                                        ImGui::SetCursorPosY( ImGui::GetCursorPosY() - 3.0f );
                                        ImGui::TextUnformatted( asyncState.c_str() );
                                        progressionInhibitionReason = "Busy fetching metadata";
                                        break;

                                    case endlesss::cache::Jams::AsyncFetchState::Failed:
                                        ImGui::TextUnformatted( asyncState.c_str() );
                                        break;
                                    }

                                    ImGui::PopStyleColor();
                                }
                            }
                            // configuration of jam cache sync
                            {
                                bool syncOptionsChanged = false;

                                ImGui::Spacing();
                                ImGui::Indent( perBlockIndent * 3 );
                                ImGui::BeginDisabledControls( jamsAreUpdating );

                                {
                                    ImGui::AlignTextToFramePadding();
                                    ImGui::TextDisabled( "[?]" );
                                    ImGui::SameLine();
                                    ImGui::CompactTooltip( "For every jam we know about, query basic data like riff counts\nThis can take a little while but provides the most complete\nview of the jam metadata" );
                                    syncOptionsChanged |= ImGui::Checkbox( " Fetch Jam State", &endlesssAuth.sync_options.sync_state );
                                }

                                ImGui::EndDisabledControls( jamsAreUpdating );
                                ImGui::Unindent( perBlockIndent * 3 );

                                if ( syncOptionsChanged )
                                    config::save( *this, endlesssAuth );
                            }
                        }

#endif // OURO_HAS_NDLS_ONLINE

                        ImGui::Unindent( perBlockIndent );
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::ColumnSeparatorBreak();
                    }


                    // ---------------------------------------------------------------------------------------------------------
                    {
                        ImGui::Unindent( perColumnIndent );
                        ImGui::TableNextColumn();
                        ImGui::Indent( perColumnIndent );
                    }


                    // ---------------------------------------------------------------------------------------------------------
                    // configure how we'll be making noise

                    {
                        m_mdFrontEnd->titleText( "Audio Engine" );
                        ImGui::Indent( perBlockIndent );

                        // clamp size of drop-downs
                        ImGui::PushItemWidth( 80.0f );
                        {
                            // choose a sample rate; changing causes device options to be reconsidered
                            if ( ImGui::ValueArrayComboBox( "Sample Rate :", "##smpr", cSampleRateLabels, cSampleRateValues, audioConfig.sampleRate, previewSampleRate, 16.0f ) )
                            {
                                fnUpdateDeviceQuery();
                            }

                            // note incompatibility with Opus streaming direct-to-discord; this has a fixed constraint of running at 48k
                            if ( audioConfig.sampleRate != 48000 )
                                ImGui::TextColored( colour::shades::callout.neutral(), ICON_FAB_DISCORD " Streaming requires 48kHz" );
                            else
                                ImGui::TextUnformatted( ICON_FAB_DISCORD " Streaming compatible" );

                            ImGui::Spacing();
                            ImGui::Spacing();

                            ImGui::ValueArrayComboBox( "Buffer Size :", "##buff", cBufferSizeLabels, cBufferSizeValues, audioConfig.bufferSize, previewBufferSize, 16.0f );
                        }
                        ImGui::PopItemWidth();

                        if ( ImGui::Checkbox( "Prefer Low Latency", &audioConfig.lowLatency ) )
                            fnUpdateDeviceQuery();

                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::Spacing();

                        ImGui::TextUnformatted( "Output Device :" );
                        ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x - 20.0f );

                        std::string deviceNameLabel;
                        if ( ImGui::BeginCombo( "##audio_devices", (chosenDeviceIndex >= 0) ? adq.m_deviceNamesUnpacked[chosenDeviceIndex] : "-select audio device-", 0 ) )
                        {
                            for ( int32_t n = 0; n < (int32_t)adq.m_deviceNamesUnpacked.size(); n++ )
                            {
                                const bool is_selected = (chosenDeviceIndex == n);

                                deviceNameLabel = fmt::format( "[L={:.2f}] {}", adq.m_deviceLatencies[n], adq.m_deviceNamesUnpacked[n] );

                                if ( ImGui::Selectable( deviceNameLabel.c_str(), is_selected ) )
                                {
                                    chosenDeviceIndex = n;
                                    audioConfig.lastDevice = adq.m_deviceNames[chosenDeviceIndex];
                                }

                                if ( is_selected )
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();

                        ImGui::Unindent( perBlockIndent );
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::ColumnSeparatorBreak();

                        if ( chosenDeviceIndex < 0 )
                            progressionInhibitionReason = "No audio device selected";
                    }

                    // ---------------------------------------------------------------------------------------------------------
                    // gotta go fast
                    {
                        m_mdFrontEnd->titleText( "Performance" );
                        ImGui::Indent( perBlockIndent );

                        const auto NicerIntEditPreamble = []( const char* title, const char* tooltip )
                            {
                                ImGui::TextDisabled( "[?]" );
                                ImGui::CompactTooltip( tooltip );
                                ImGui::SameLine();
                                ImGui::TextUnformatted( title );
                                ImGui::SameLine();
                                const auto panelRegionAvailable = ImGui::GetContentRegionAvail();
                                ImGui::Dummy( ImVec2( panelRegionAvailable.x - 200.0f, 0.0f ) );
                                ImGui::SameLine();
                            };

                        ImGui::PushItemWidth( 150.0f );
                        {
                            NicerIntEditPreamble(
                                "Stem Cache Memory Target",
                                "Stems are loaded and stored in memory for re-use between riffs.\nWhen the amount of memory in use hits this value, we run a pruning process to unload the oldest ones in the cache.\nIncrease this if you have lots of RAM and want to avoid re-loading\nstems off disk during longer sessions"
                            );
                            if ( ImGui::InputInt( " Mb##stem_cache_mem", &m_configPerf.stemCacheAutoPruneAtMemoryUsageMb, 256, 512 ) )
                            {
                                m_configPerf.clampLimits();
                            }

                            NicerIntEditPreamble(
                                "Riff Live Instance Pool Size",
                                "If possible, some riffs are kept alive in memory to speed-up transitions / avoid re-loading from disk.\nThis value controls how many we aim to limit that to.\nIncrease if you got RAM to burn."
                            );
                            ImGui::InputInt( "##riff_live_inst", &m_configPerf.liveRiffInstancePoolSize, 8, 16 );
                        }
                        ImGui::PopItemWidth();


                        ImGui::Unindent( perBlockIndent );
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::ColumnSeparatorBreak();
                    }

                    // ---------------------------------------------------------------------------------------------------------
                    // 
                    {
                        m_mdFrontEnd->titleText( "Vibes" );
                        ImGui::Indent( perBlockIndent );

                        // fundamental enable/disable of all Vibes systems to avoid booting it or compiling any shaders (JIC)
                        {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextDisabled( "[?]" );
                            ImGui::CompactTooltip( "Vibes is the audio-reactive visual effects system\nbuilt into Ouroveon. Disabling it can\nsave some GPU cost and VRAM, or if\nyou experience issues with it during\napplication boot-up" );
                            ImGui::SameLine();

                            ImGui::Checkbox( " Enable Vibes Rendering", &m_configPerf.enableVibesRenderer );
                        }

                        ImGui::Unindent( perBlockIndent );
                        ImGui::Spacing();
                        ImGui::Spacing();
                        ImGui::ColumnSeparatorBreak();
                    }


                    ImGui::Unindent( perColumnIndent );
                    ImGui::EndTable();

                } // phase-2 configuration blocks
            } // table

            // quit | continue button block at the base of the screen
            {
                const ImVec2 buttonSize( 260.0f, 36.0f );

                // align to the bottom of the dialogue
                const auto panelRegionAvailable = ImGui::GetContentRegionAvail();
                ImGui::Dummy( ImVec2( 0.0f, panelRegionAvailable.y - 64.0f ) );

                // centered
                ImGui::Dummy( ImVec2( (panelRegionAvailable.x * 0.5f) - buttonSize.x, 0.0f ) );


                // Quit is denied only when some async work is happening so resulting data saving isn't hurt by quitting half-way through
                const bool asyncWorkIsHappening = (asyncFetchState == endlesss::cache::Jams::AsyncFetchState::Working);
                ImGui::BeginDisabledControls( asyncWorkIsHappening );
                {
                    ImGui::SameLine();
                    if ( ImGui::Button( ICON_FA_CIRCLE_XMARK " Quit", buttonSize ) )
                    {
                        m_mdFrontEnd->requestQuit();
                    }
                }
                ImGui::EndDisabledControls( asyncWorkIsHappening );

                // Continue is available if something else isn't broken or working; tell the user why we can't continue if that's the case
                const bool bootProcessUnfinished = !progressionInhibitionReason.empty();
                ImGui::BeginDisabledControls( bootProcessUnfinished );
                {
                    ImGui::SameLine();
                    if ( ImGui::Button( bootProcessUnfinished ? progressionInhibitionReason.c_str() : ICON_FA_CIRCLE_CHECK " Accept & Continue", buttonSize ) )
                    {
                        successfulBreakFromLoop = true;
                    }
                }
                ImGui::EndDisabledControls( bootProcessUnfinished );
            }

        } // config window block

        ImGui::End();

        if ( m_networkConfiguration )
        {
            m_networkConfiguration->setQuality( GetNetworkQuality() );
        }

        // pending error message? transfer and pop the modal open on the next cycle round
        // must be done here, after ImGui::End for annoying Imgui ordering / hierarchy reasons
        if ( !popupErrorMessageToDisplay.empty() )
        {
            popupErrorMessage = std::move(popupErrorMessageToDisplay);
            ImGui::OpenPopup( popupErrorModalName );
            popupErrorMessageToDisplay.clear();
        }

        // dispatch the UI for rendering
        finishInterfaceLayoutAndRender();

        if ( successfulBreakFromLoop )
            break;
    }

#if OURO_HAS_NDLS_ONLINE
    // if we passed over Endlesss authentication, create a net config structure that only knows how to talk to public
    // stuff (so we can still pull stems from the CDN on demand, for example)
    if ( m_networkConfiguration->hasNoAccessSet() )
    {
        blog::app( FMTX( "configuring network before launch ..." ) );
        if ( !endlesssAuthExpired )
            m_networkConfiguration->initWithAuthentication( m_appEventBus, m_configEndlesssAPI, endlesssAuth );
        else
            m_networkConfiguration->initWithoutAuthentication( m_appEventBus, m_configEndlesssAPI );
    }
#else
    {
        // we should be already configured in the no-endlesss mode, forced to No Auth mode even though we also won't be using the netconfig anymore in this build
        ABSL_ASSERT( m_networkConfiguration->hasNoAccessSet() == false );
    }
#endif // OURO_HAS_NDLS_ONLINE

#if OURO_HAS_NDLS_ONLINE
    if ( advancedOptionsBlock.bShow )
    {
        if ( advancedOptionsBlock.bEnableNetworkLogging )
            m_networkConfiguration->enableFullNetworkDiagnostics();
        if ( advancedOptionsBlock.bEnableLastMinuteQuirkFixes )
            m_networkConfiguration->enableLastMinuteQuirkFixes();
    }
#endif // OURO_HAS_NDLS_ONLINE

    // save any config data blocks
    {
        const auto audioSaveResult = config::save( *this, audioConfig );
        if ( audioSaveResult != config::SaveResult::Success )
        {
            blog::error::cfg( "Unable to save audio configuration" );
        }

        const auto perfSaveResult = config::save( *this, m_configPerf );
        if ( perfSaveResult != config::SaveResult::Success )
        {
            blog::error::cfg( "Unable to save performance configuration" );
        }
    }

    m_taskExecutor.wait_for_all();

    if ( m_mdFrontEnd->wasQuitRequested() )
        return 0;


    // unplug status bar bits
    unregisterStatusBarBlock( sbbTimeStatusLeftID );
    unregisterStatusBarBlock( sbbTimeStatusRightID );


    registerMainMenuEntry( 2, "EXPORT", [this]()
        {
            if ( ImGui::MenuItem( "Open Output Folder..." ) )
            {
                xpOpenFolder( m_storagePaths->outputApp.string().c_str() );
            }
        });

    // events
    {
        APP_EVENT_REGISTER( ExportRiff );
        APP_EVENT_REGISTER_SPECIFIC( MixerRiffChange, 16 * 4096 );

        {
            base::EventBusClient m_eventBusClient( m_appEventBus );
            APP_EVENT_BIND_TO( ExportRiff );
            APP_EVENT_BIND_TO( RequestToShareRiff );
            APP_EVENT_BIND_TO( BNSCacheMiss );
            APP_EVENT_BIND_TO( BNSJamNameUpdate );
        }
    }

    // audio init must run on the main thread, saves any COM issues when spinning up ASIO drivers on a new thread
    const auto audioInitStatus = m_mdAudio->initOutput( audioConfig, audioSpectrumConfig );

    // kick post-configuration session tasks, run off main thread so we don't stall the whole UI
    auto sessionStartFuture = m_taskExecutor.async( "init_session", [this, audioInitStatus, &audioConfig, &audioSpectrumConfig]() -> absl::Status
        {
            if ( !audioInitStatus.ok() )
            {
                return audioInitStatus;
            }

            // boot stem cache now we have paths & audio configured
            const auto stemCacheStatus = m_stemCache.initialise( m_storagePaths->cacheCommon, m_mdAudio->getSampleRate() );
            if ( !stemCacheStatus.ok() )
            {
                return stemCacheStatus;
            }
            m_stemCacheLastPruneCheck.setToFuture( c_stemCachePruneCheckDuration );
            m_stemCachePruneTask.emplace( [this]() { m_stemCache.lockAndPrune( false ); } );

            // create universal warehouse instance
            {
                m_warehouse = std::make_unique<endlesss::toolkit::Warehouse>(
                    m_storagePaths.value(),
                    m_networkConfiguration,
                    m_appEventBus );

                m_warehouse->upsertJamDictionaryFromCache( m_jamLibrary );              // update warehouse list of jam IDs -> names from the current cache
                m_warehouse->upsertJamDictionaryFromBNS( m_jamNameService );            // .. and same with the BNS entries
                m_warehouse->extractJamDictionary( m_jamHistoricalFromWarehouse );      // pull full list of jam IDs -> names from warehouse as "historical" list
            }

            return absl::OkStatus();
        });

    // run a short "please wait" UI loop while we let the above async task complete & display any errors found
    bool bRunSessionWaitLoop = true;
    absl::Status sessionWaitResult = absl::OkStatus();
    while ( bRunSessionWaitLoop && beginInterfaceLayout( CoreGUI::VF_WithStatusBar ) )
    {
        const ImVec2 startupMessageSize     = ImVec2( 220.0f, 40.0f );
        const ImVec2 errorWindowSize        = ImVec2( 400.0f, 160.0f );
        const char* startupTitle            = ICON_FA_POWER_OFF " Creating Session###startup";


        if ( sessionStartFuture.valid() )
        {
            if ( sessionStartFuture.wait_for( 8ms ) != std::future_status::ready )
            {
                if ( ImGui::BeginFixedCenteredWindow( startupTitle, startupMessageSize ) )
                {
                    ImGui::Spinner( "##waiting", true, ImGui::GetTextLineHeight() * 0.3f, 3.0f, 0.0f, ImGui::GetColorU32( ImGuiCol_Text ) );
                    ImGui::SameLine( 0, 16.0f );
                    ImGui::TextUnformatted( "Please wait ..." );
                }
                ImGui::End();
            }
            else
            {
                sessionWaitResult = sessionStartFuture.get();
            }
        }
        else
        {
            if ( sessionWaitResult.ok() )
            {
                // show last message before (possibly) blocking code in EntrypointOuro(), eventually passing
                // control over to the normal UI cycle in the app
                if ( ImGui::BeginFixedCenteredWindow( startupTitle, startupMessageSize ) )
                {
                    ImGui::TextUnformatted( "Booting ..." );
                }
                ImGui::End();

                bRunSessionWaitLoop = false;
            }
            else
            {
                if ( ImGui::BeginFixedCenteredWindow( "Error", errorWindowSize ) )
                {
                    ImGui::TextColored( colour::shades::errors.light(), "Session Startup Failed" );
                    ImGui::TextWrapped( "%s", sessionWaitResult.ToString().c_str() );
                    ImGui::Separator();
                    if ( ImGui::Button( "  Quit  " ) )
                    {
                        bRunSessionWaitLoop = false;
                    }
                }
                ImGui::End();
            }
        }

        // dispatch the UI for rendering
        finishInterfaceLayoutAndRender();
    };

    if ( !sessionWaitResult.ok() )
        return -1;

    // plug in a callback on the main thread to do jam name resolution in the background
    registerMainThreadCall( "name-resolution", [this]( float deltaTime )
        {
            updateJamNameResolutionTasks( deltaTime ); 
        });

    // custom menu entries at the ouro level
    if ( bEnableCacheManagementMenu )
    {
        // build advanced cache control menu if enabled; these tools are not really "user facing" quality, mostly
        // for my own tests or debug usage
        registerMainMenuEntry( 2, "CACHE", [this]()
            {
                if ( ImGui::MenuItem( "Migration ..." ) )
                {
                    activateModalPopup( "Stem Cache Migration", [
                        this,
                            state = ux::createCacheMigrationState( m_storagePaths->cacheCommon )](const char* title)
                        {
                            ux::modalCacheMigration( title, *m_warehouse, *state );
                        } );
                }
                if ( ImGui::MenuItem( "Trim / Repair ..." ) )
                {
                    activateModalPopup( "Stem Cache Trim / Repair", [
                            state = ux::createCacheTrimState( m_storagePaths->cacheCommon )](const char* title)
                        {
                            ux::modalCacheTrim( title, *state );
                        } );
                }
            } );
    }


    // =================================================================================================================
    // all done, pass control over to next level
    //
    int appResult = EntrypointOuro();
    //
    // =================================================================================================================

    // unhook events
    {
        base::EventBusClient m_eventBusClient( m_appEventBus );
        APP_EVENT_UNBIND( BNSCacheMiss );
        APP_EVENT_UNBIND( RequestToShareRiff );
        APP_EVENT_UNBIND( ExportRiff );
    }

    // wrap up any dangling async work before teardown
    ensureStemCacheChecksComplete();
    
    // ensure executor is drained
    m_taskExecutor.wait_for_all();

    {
        m_warehouse.reset();
    }

    return appResult;
}

// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::ensureStemCacheChecksComplete()
{
    if ( m_stemCachePruneFuture.has_value() )
        m_stemCachePruneFuture->wait();
    m_stemCachePruneFuture = std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------------
endlesss::services::IJamNameResolveService::LookupResult OuroApp::lookupJamNameAndTime(
    const endlesss::types::JamCouchID& jamID,
    std::string& resultJamName,
    uint64_t& resultTimestamp ) const
{
    resultTimestamp = 0;    // default to unknown timestamp

    if ( !endlesss::types::Constants::isStandardJamID( jamID ) )
    {
        resultJamName = jamID.value();
        return LookupResult::PresumedPersonal;
    }

    endlesss::cache::Jams::Data jamData;
    if ( m_jamLibrary.loadDataForDatabaseID( jamID, jamData ) )
    {
        resultJamName = jamData.m_displayName;
        resultTimestamp = jamData.m_timestampOrdering;
        return LookupResult::FoundInPrimarySource;
    }

    // check our prefetched band/name LUT
    const auto nsIt = m_jamNameService.entries.find( jamID );
    if ( nsIt != m_jamNameService.entries.end() )
    {
        if ( nsIt->second.sync_name.empty() )
            resultJamName = nsIt->second.link_name;
        else
            resultJamName = nsIt->second.sync_name;
        return LookupResult::FoundInArchives;
    }

    // check fall-back data source, the "historical" list from the Warehouse db
    const auto historicalIt = m_jamHistoricalFromWarehouse.find( jamID );
    if ( historicalIt != m_jamHistoricalFromWarehouse.end() )
    {
        resultJamName = historicalIt->second;
        return LookupResult::FoundInArchives;
    }

    return LookupResult::NotFound;
}

// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::maintainStemCacheAsync()
{
    if ( m_stemCacheLastPruneCheck.hasPassed() )
    {
        const auto stemMemory          = m_stemCache.estimateMemoryUsageBytes();
        const auto stemMemoryTriggerMb = (std::size_t)m_configPerf.stemCacheAutoPruneAtMemoryUsageMb;

        if ( stemMemory >= stemMemoryTriggerMb * 1024 * 1024 )
        {
            ensureStemCacheChecksComplete();

            // the prune is usually measured in 10s of milliseconds, but we may as well 
            // toss it into the job queue; it locks the cache to do the work, worst case very briefly delaying 
            // async background loading
            m_stemCachePruneFuture = m_taskExecutor.run( m_stemCachePruneTask );
        }
        m_stemCacheLastPruneCheck.setToFuture( c_stemCachePruneCheckDuration );
    }
}


// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::event_ExportRiff( const events::ExportRiff* eventData )
{
    endlesss::toolkit::xp::RiffExportDestination destination(
        m_storagePaths.value(),
        m_configExportOutput.spec
    );

    auto netCfg = getNetworkConfiguration();
    const auto exportedFiles = endlesss::toolkit::xp::exportRiff(
        *netCfg,
        endlesss::toolkit::xp::RiffExportMode::Stems,
        destination,
        eventData->m_adjustments,
        eventData->m_riff );

    blog::core( FMTX( "Exported" ) );
    for ( const auto& exported : exportedFiles )
    {
        // have to convert from a u16 encoding as these paths may contain utf8 and calling string()
        // on them will then throw an exception
        std::string utf8path = utf8::utf16to8( exported.u16string() );

        blog::core( FMTX("   {}"), utf8path );
    }

    // show toast to signal export is finished
    {
        m_appEventBus->send<::events::AddToastNotification>( ::events::AddToastNotification::Type::Info,
            ICON_FA_FLOPPY_DISK " Riff Exported",
            eventData->m_riff->m_uiDetails );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::event_RequestToShareRiff( const events::RequestToShareRiff* eventData )
{
#if OURO_HAS_NDLS_ONLINE

    activateModalPopup( "Share Riff", [
        this,
        netCfg = getNetworkConfiguration(),
        state = ux::createModelRiffFeedShareState( eventData->m_identity ) ](const char* title)
    {
        ux::modalRiffFeedShare( title, *state, netCfg, getTaskExecutor() );
    });

#else 

    ABSL_ASSERT( 0 );
    m_appEventBus->send<::events::AddToastNotification>( ::events::AddToastNotification::Type::Error,
        "Sharing to Endlesss Disabled",
        "You shouldn't have been able to do this");

#endif // OURO_HAS_NDLS_ONLINE
}

// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::event_BNSCacheMiss( const events::BNSCacheMiss* eventData )
{
#if OURO_HAS_NDLS_ONLINE

    // kick a task that uses a few network calls to go from a band### id to a public name; this uses the permalink
    // endpoint (to get the extended ID) and the public riff API with that ID to snag the names
    getTaskExecutor().silent_async( [this, jamID = eventData->m_jamID, netCfg = getNetworkConfiguration()]()
        {
            blog::api( FMTX( "name resolution for {}" ), jamID );

            // use the permalink query to fetch the original name and the full extended ID we can use to investigate further
            endlesss::api::BandPermalinkMeta bandPermalink;
            if ( bandPermalink.fetch( *netCfg, jamID ) )
            {
                if ( bandPermalink.errors.empty() )
                {
                    std::string extendedID;
                    if ( bandPermalink.data.extractLongJamIDFromPath( extendedID ) )
                    {
                        endlesss::api::BandNameFromExtendedID bandNameFromExtended;
                        if ( bandNameFromExtended.fetch( *netCfg, extendedID ) && bandNameFromExtended.ok )
                        {
                            // note any name updates, just for interest
                            if ( bandNameFromExtended.data.name != bandPermalink.data.band_name )
                            {
                                blog::api( FMTX( "name resolution; update from original [{}] to [{}]" ),
                                    bandPermalink.data.band_name,
                                    bandNameFromExtended.data.name
                                );
                            }

                            // .. for now, just log out the name we found, this will be processed on the main thread
                            emplaceJamNameResolutionIntoQueue(
                                std::move( jamID ),
                                std::move( bandNameFromExtended.data.name )
                            );
                        }
                        else
                        {
                            blog::error::api( FMTX( "name resolution failure, BandNameFromExtendedID failed [{}]" ), bandNameFromExtended.message );
                        }
                    }
                    else
                    {
                        blog::error::api( FMTX( "name resolution failure, long-form ID not recognised [{}]" ), bandPermalink.data.path );
                    }
                }
                else
                {
                    blog::error::api( FMTX( "name resolution failure, {}" ), bandPermalink.errors.front() );
                }
            }
        });

#else

    blog::debug::api( FMTX( "ignored name resolution cache miss, {}" ), eventData->m_jamID );

#endif // OURO_HAS_NDLS_ONLINE
}

// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::event_BNSJamNameUpdate( const events::BNSJamNameUpdate* eventData )
{
    emplaceJamNameResolutionIntoQueue( eventData->m_jamID, eventData->m_bandName );
}

// ---------------------------------------------------------------------------------------------------------------------
void OuroApp::updateJamNameResolutionTasks( float deltaTime )
{
    // as we use some blocking calls to the warehouse here, pause name resolution while modals are active
    // because if they are also doing heavy write ops to the warehouse - eg. jam import - it can potentially stall 
    // the main UI thread
    if ( !m_modalsActive.empty() )
        return;

    // see if we have any outstanding new jam name resolution results
    JamNameRemoteResolution jamNameRemoteResolution;
    if ( m_jamNameRemoteFetchResultQueue.try_dequeue( jamNameRemoteResolution ) )
    {
        blog::api( FMTX( "BNS name resolution update: [{}] => [{}]" ), jamNameRemoteResolution.first, jamNameRemoteResolution.second );

        // plug this new resolved name directly back into the warehouse (upserting any existing ID)
        m_warehouse->upsertSingleJamIDToName( jamNameRemoteResolution.first, jamNameRemoteResolution.second );

        // and then also mirror it into the historical record we already pulled from the warehouse, so the data sets match
        m_jamHistoricalFromWarehouse.emplace( jamNameRemoteResolution.first, std::move( jamNameRemoteResolution.second ) );
        m_jamNameRemoteFetchUpdateBroadcastTimer = 1.0f;
    }

    // we add a degree of delay to sending update messages to try and cluster cache name updates
    if ( m_jamNameRemoteFetchUpdateBroadcastTimer > 0 )
    {
        // only send when we cross or hit the 0 boundary this frame
        m_jamNameRemoteFetchUpdateBroadcastTimer -= deltaTime;
        if ( m_jamNameRemoteFetchUpdateBroadcastTimer <= 0 )
        {
            getEventBusClient().Send< ::events::BNSWasUpdated >( 0 );
            m_jamNameRemoteFetchUpdateBroadcastTimer = 0;
        }
    }
}

base::OperationID OuroApp::enqueueJamStemArchiveImportAsync( const fs::path& pathToTARFile, tf::Taskflow& taskFlow )
{
    const fs::path inputTarFile = pathToTARFile;
    const fs::path outputPath = getStemCache().getCacheRootPath();

    blog::app( FMTX( "stem import task queued - from [{}] to [{}]" ), inputTarFile.string(), outputPath.string() );

    const auto importOperationID = base::Operations::newID( endlesss::toolkit::Warehouse::OV_ImportAction );

    // background task to unpack the stems into the chosen directory from the .TAR
    tf::Task workerTask = taskFlow.emplace( [this, inputTarFile, outputPath, importOperationID]()
        {
            base::EventBusClient m_eventBusClient( m_appEventBus );
            OperationCompleteOnScopeExit( importOperationID );

            std::size_t filesTouched = 0;

            const auto tarArchiveStatus = io::unarchiveTARIntoDirectory(
                inputTarFile,
                outputPath,
                [&]( const std::size_t bytesProcessed, const std::size_t filesProcessed )
                {
                    // ping that we're still working on async tasks
                    if ( (filesProcessed % 20) == 0 )
                    {
                        m_eventBusClient.Send< ::events::AsyncTaskActivity >();
                        std::this_thread::yield();
                    }

                    filesTouched++;
                });

            // deal with issues, tell user we bailed
            if ( !tarArchiveStatus.ok() )
            {
                m_appEventBus->send<::events::AddErrorPopup>(
                    "Stem Import from TAR Failed",
                    fmt::format( FMTX("Error reported during stem import:\n{}"), tarArchiveStatus.ToString() )
                );
            }
            else
            {
                m_appEventBus->send<::events::AddToastNotification>( ::events::AddToastNotification::Type::Info,
                    ICON_FA_BOXES_PACKING " Stem Import Success",
                    fmt::format( FMTX( "Extracted {} stems" ), filesTouched ) );
            }
        });

    // limit execution to serial, avoid hammering the file system too hard
    workerTask.acquire( m_jamStemImportSemaphore );
    workerTask.release( m_jamStemImportSemaphore );

    return importOperationID;
}

} // namespace app
