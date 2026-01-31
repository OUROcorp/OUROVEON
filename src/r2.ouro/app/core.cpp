//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#include "pch.h"

#include "base/instrumentation.h"
#include "base/operations.h"

#include "data/uuid.h"

#include "config/base.h"
#include "config/frontend.h"
#include "config/data.h"
#include "config/layout.h"

#include "app/core.h"
#include "app/module.frontend.h"
#include "app/module.frontend.fonts.h"
#include "app/module.audio.h"
#include "app/module.midi.h"

#include "filesys/fsutil.h"

#include "platform_folders.h"

// ---------------------------------------------------------------------------------------------------------------------

#if OURO_PLATFORM_WIN
#include "win32/utils.h"
#include <ios>
#include <conio.h>
#endif 

#if OURO_FEATURE_NST24
#include "effect/vst2/host.h"
#endif 

// ---------------------------------------------------------------------------------------------------------------------
#if OURO_DEBUG
// for hooking PureAudio debug output
extern "C"
{
    typedef void (*PaUtilLogCallback) (const char* log);
    extern void PaUtil_SetDebugPrintFunction( PaUtilLogCallback  cb );
}
void OuroPaUtilLogCallback( const char* log )
{
// this is optional, even in debug, as PA is quite verbose with the logging
#ifdef OURO_DEBUG_PUREAUDIO
    blog::audio( FMTX( "PA | {}" ), log );
#endif // OURO_DEBUG_PUREAUDIO
}
#endif // OURO_DEBUG


// ---------------------------------------------------------------------------------------------------------------------
// manual exposure for the one-off init/term in Operations

namespace base
{
    extern void OperationsInit();
    extern void OperationsTerm();
}

// ---------------------------------------------------------------------------------------------------------------------
// pch global function implementations

void ouroveonThreadEntry( const char* name )
{
    rpmalloc_thread_initialize();
    base::instr::setThreadName( name );
}

void ouroveonThreadExit()
{
    rpmalloc_thread_finalize( 1 );
}

OuroveonThreadScope::OuroveonThreadScope( const char* threadName )
{
    ouroveonThreadEntry( threadName );

    // we save the name to report it in the dtor but also this helps track
    // threads that don't get properly shutdown as rpmalloc will report them as leaks
    m_name = static_cast<char*>( rpmalloc(strlen(threadName) + 1) );
    strcpy( m_name, threadName );

    blog::instr( FMTX( "{{thread}} => {}" ), m_name );
}

OuroveonThreadScope::~OuroveonThreadScope()
{
    blog::instr( FMTX( "{{thread}} <= {}" ), m_name );
    rpfree( m_name );
    ouroveonThreadExit();
}


// ---------------------------------------------------------------------------------------------------------------------
// external threading hooks for naming & rpmalloc tls
//
struct TaskFlowWorkerHook : public tf::WorkerInterface
{
    TaskFlowWorkerHook( std::string_view usage )
        : m_usageString( usage )
    {}

    void scheduler_prologue( tf::Worker& worker ) override
    {
        ouroveonThreadEntry( fmt::format( FMTX( OURO_THREAD_PREFIX "TF:{}:{}" ), m_usageString, worker.id() ).c_str() );
    }

    void scheduler_epilogue( tf::Worker& worker, std::exception_ptr ptr ) override
    {
        ouroveonThreadExit();
    }

private:
    std::string     m_usageString;
};

// ---------------------------------------------------------------------------------------------------------------------
void _discord_dpp_thread_init( const char* name )
{
    ouroveonThreadEntry( fmt::format( FMTX( OURO_THREAD_PREFIX "DPP:{}" ), name ).c_str() );
}

void _discord_dpp_thread_exit()
{
    ouroveonThreadExit();
}

namespace dpp
{
    extern void delete_eventmap();
}


// ---------------------------------------------------------------------------------------------------------------------
namespace app {

// ---------------------------------------------------------------------------------------------------------------------
StoragePaths::StoragePaths( const config::Data& configData, const char* appName )
{
    cacheCommon = ( fs::path( configData.storageRoot ) / "cache"  ) / "common";
    cacheApp    = ( fs::path( configData.storageRoot ) / "cache"  ) / appName;
    outputApp   = ( fs::path( configData.storageRoot ) / "output" ) / appName;
}

// ---------------------------------------------------------------------------------------------------------------------
bool StoragePaths::tryToCreateAndValidate() const
{
    blog::core( FMTX( "  storage paths : " ) );

    blog::core( FMTX( "      app cache : {}" ), cacheApp.string() );
    if ( !filesys::ensureDirectoryExists( cacheApp ).ok() )
    {
        blog::error::core( "unable to create or find directory" );
        return false;
    }

    blog::core( FMTX( "   common cache : {}" ), cacheCommon.string() );
    if ( !filesys::ensureDirectoryExists( cacheCommon ).ok() )
    {
        blog::error::core( "unable to create or find directory" );
        return false;
    }

    blog::core( FMTX( "     app output : {}" ), outputApp.string() );
    if ( !filesys::ensureDirectoryExists( outputApp ).ok() )
    {
        blog::error::core( "unable to create or find directory" );
        return false;
    }

    return true;
}


// ---------------------------------------------------------------------------------------------------------------------
CoreStart::CoreStart()
{
    rpmalloc_initialize();
    base::instr::setThreadName( OURO_THREAD_PREFIX "$::main-thread" );

    base::OperationsInit();
}

CoreStart::~CoreStart()
{
    base::OperationsTerm();

    rpmalloc_finalize();
}

// ---------------------------------------------------------------------------------------------------------------------
Core::Core()
    : CoreStart()
    , m_networkConfiguration( std::make_shared<endlesss::api::NetConfiguration>() )
    , m_taskExecutor(        std::clamp( std::thread::hardware_concurrency(), 2U, OURO_THREAD_LIMIT ), std::make_shared<TaskFlowWorkerHook>("app")  )
    , m_taskExecutorPlugins( std::clamp( std::thread::hardware_concurrency(), 1U, 3U ),                std::make_shared<TaskFlowWorkerHook>("plug") )
{
}

Core::~Core()
{
}

// ---------------------------------------------------------------------------------------------------------------------
#if OURO_PLATFORM_OSX
// return the path to where the MacOS bundle is running from; this will be used as our root path for finding app-local data
std::string osxGetBundlePath()
{
    CFURLRef url;
    CFStringRef CFExePath;
    CFBundleRef Bundle;

    Bundle = CFBundleGetMainBundle();
    url = CFBundleCopyExecutableURL(Bundle);

    CFExePath = CFURLCopyFileSystemPath( url, kCFURLPOSIXPathStyle );

    char exeBuffer[1024];
    CFStringGetCString( CFExePath, exeBuffer, 1024, kCFStringEncodingISOLatin1 );

    CFRelease( url );
    CFRelease( CFExePath );

    return std::string( exeBuffer );
}
#endif // OURO_PLATFORM_OSX


// ---------------------------------------------------------------------------------------------------------------------
int Core::Run()
{
#if OURO_PLATFORM_WIN
    // ansi colouring via fmt{} seems to fail on consoles launched from outside VS without jamming these on
    ::SetConsoleMode( ::GetStdHandle( STD_OUTPUT_HANDLE ), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING );
    if ( ::IsValidCodePage( CP_UTF8 ) )
    {
        ::SetConsoleCP( CP_UTF8 );
        ::SetConsoleOutputCP( CP_UTF8 );
    }
#endif

    // http://www.cplusplus.com/reference/ios/ios_base/sync_with_stdio/
    std::ios_base::sync_with_stdio( false );

    // sup
    blog::core( FMTX( "Hello from OUROVEON {} [{}]" ), GetAppNameWithVersion(), getOuroveonPlatform() );

    // report if there are any build tweaks
    blog::debug::core( FMTX( "++ Debug Build" ) );
#ifdef OURO_PGO_INSTRUMENT
    blog::core( FMTX( "++ PGO Instrumentation Build" ) );
#endif // OURO_PGO_INSTRUMENT
#ifdef OURO_PGO_OPTIMIZE
    blog::core( FMTX( "++ PGO Optimised Build" ) );
#endif // OURO_PGO_OPTIMIZE

#if OURO_DEBUG
    PaUtil_SetDebugPrintFunction( &OuroPaUtilLogCallback );
#endif // OURO_DEBUG

    // scripting
    blog::core( FMTX( "initialising {}" ), LUA_VERSION );
    {
        m_lua.open_libraries(
            sol::lib::base,
            sol::lib::coroutine,
            sol::lib::math,
            sol::lib::string,
            sol::lib::io
        );
    }

    // big and wide
    blog::core( FMTX( "initialising taskflow {}" ), tf::version() );
    blog::core( FMTX( " + {} primary worker threads" ), m_taskExecutor.num_workers() );
    blog::core( FMTX( " + {} plugin pool threads" ), m_taskExecutorPlugins.num_workers() );

    // configure app event bus
    {
        m_appEventBus       = std::make_shared<base::EventBus>();
        m_appEventBusClient = base::EventBusClient( m_appEventBus );

        // register basic event IDs
        APP_EVENT_REGISTER( AddErrorPopup );
        APP_EVENT_REGISTER( AddToastNotification );
        APP_EVENT_REGISTER_SPECIFIC( OperationComplete, 64 * 1024 );    // enqueue an entire (techno) jam - then cancel it. can generate a lot of aborted events D:
        APP_EVENT_REGISTER( PanicStop );
        APP_EVENT_REGISTER( StemDataAmalgamGenerated );

        // MIDI event bus
        APP_EVENT_REGISTER( MidiEvent );

        // events from the endlesss sdk layer
        APP_EVENT_REGISTER( EnqueueRiffPlayback );
        APP_EVENT_REGISTER( BNSCacheMiss );
        APP_EVENT_REGISTER( BNSJamNameUpdate );
        APP_EVENT_REGISTER( BNSWasUpdated );
        APP_EVENT_REGISTER( NetworkActivity );
        APP_EVENT_REGISTER( AsyncTaskActivity );
        APP_EVENT_REGISTER( RiffTagAction );
        APP_EVENT_REGISTER( RequestNavigationToRiff );
        APP_EVENT_REGISTER_SPECIFIC( RequestToShareRiff, 4096 );        // in case we kick off all-shared-riff exports on some of the goons with thousands of shares
    }
    {
        base::EventBusClient m_eventBusClient( m_appEventBus );
        APP_EVENT_BIND_TO( NetworkActivity );
        APP_EVENT_BIND_TO( AsyncTaskActivity );

        m_avgNetPulseHistory.fill( 0 );
        m_asyncTaskPulseSlots.fill( 0 );
    }


    // we load configuration data from the known system config directory
    m_sharedConfigPath  = fs::path( sago::getConfigHome() ) / cOuroveonRootName;
    m_appConfigPath     = m_sharedConfigPath / GetAppCacheName();
    
    static const fs::path sharedPathRoot( "shared" );
#if OURO_PLATFORM_OSX
    // on MacOS we may be running as a THING.app bundle with our own copy of the 
    // shared resources files in a local /Contents/Resources tree - check for this first
    const auto osxBundlePath = osxGetBundlePath();
    auto sharedPathRootTest = ( fs::path( osxBundlePath ).parent_path().parent_path() ) / "Resources";

    // if running outside of a distribution bundle, assume the unpacked file structure
    if ( !fs::exists(sharedPathRootTest) )
    {
        blog::core( "osx launched unpacked" );
        sharedPathRootTest = fs::current_path().parent_path().parent_path();
    }
    else
    {
        blog::core( FMTX( "osx bundle path : {}" ), osxBundlePath );
    }
#else
    // on non-MacOS, running APP.EXE will launch with the working path set to wherever APP.EXE is
    // first, we check if we're right next to shared (to support a more comfortable distributed build layout)
    fs::path sharedPathRootTest = fs::current_path();
    if ( fs::exists( sharedPathRootTest / sharedPathRoot ) )
    {
        blog::core( FMTX( "running next to root shared path" ) );
    }
    else
    {
        blog::core( FMTX( "running from inside /bin distribution, stepping back to find /shared" ) );

        // otherwise, we step back twice (ie from `\bin\lore\windows_release_x86_64` back to `\bin`)
        sharedPathRootTest = fs::current_path().parent_path().parent_path();
    }
#endif 

    m_sharedDataPath    = sharedPathRootTest / sharedPathRoot;

    blog::core( FMTX( "core filesystem :" ) );
    blog::core( FMTX( "  shared config : {}" ), m_sharedConfigPath.string() );
    blog::core( FMTX( "     app config : {}" ), m_appConfigPath.string() );
    blog::core( FMTX( "    shared data : {}" ), m_sharedDataPath.string() );

    // point at TZ database
    date::set_install( ( m_sharedDataPath / "timezone" ).string() );

    // ensure all core directories exist or we can't continue
    {
        blog::core( FMTX( "ensuring core paths are viable ..." ) );

        const auto sharedConfigPathStatus = filesys::ensureDirectoryExists( m_sharedConfigPath );
        if ( !sharedConfigPathStatus.ok() )
        {
            blog::error::core( FMTX( "unable to create or find shared config directory, aborting\n[{}] ({})" ), m_sharedConfigPath.string(), sharedConfigPathStatus.ToString() );
            return -2;
        }

        const auto appConfigPathStatus = filesys::ensureDirectoryExists( m_appConfigPath );
        if ( !appConfigPathStatus.ok() )
        {
            blog::error::core( FMTX( "unable to create or find app config directory, aborting\n[{}] ({})" ), m_appConfigPath.string(), appConfigPathStatus.ToString() );
            return -2;
        }

        if ( !fs::exists( m_sharedDataPath ) )
        {
            blog::error::core( FMTX( "cannot find shared data directory, aborting" ) );
            return -2;
        }
    }

    // set network debugging capture output to be per-app into the writable config area
    m_networkConfiguration->setVerboseCaptureOutputPath( m_appConfigPath );

    // try and load the data storage configuration; may be unavailable on a fresh boot
    config::Data configData;
    const auto dataLoad = config::load( *this, configData );
    if ( dataLoad == config::LoadResult::Success )
    {
        // stash the loaded data as a starting point, otherwise code later on will need to set this up manually / via UI
        m_configData = configData;
        blog::core( FMTX( "   storage root : {}" ), m_configData.value().storageRoot );
    }
    // nothing found, create some kind of default that can be set manually later
    else if ( dataLoad == config::LoadResult::CannotFindConfigFile )
    {
        blog::core( FMTX( "no data configuration file [{}] found" ), config::Data::StorageFilename );
    }
    // couldn't load, will have to setup a new one
    else
    {
        blog::error::core( FMTX( "unable to parse, find or load data configuration file [{}]" ), config::Data::StorageFilename );
    }

    // try and load performance tuning; okay if this fails, we'll use defaults
    /* const auto perfLoad = */ config::load( *this, m_configPerf );


    blog::core( "initial Endlesss setup ..." );

    // try and load the API config bundle
    const auto apiLoadResult = config::load( *this, m_configEndlesssAPI );
    if ( apiLoadResult != config::LoadResult::Success )
    {
        // can't continue without the API config
        blog::error::cfg( FMTX( "Unable to load required Endlesss API configuration data [{}]" ),
            config::getFullPath< config::endlesss::rAPI >( *this ).string() );

        return -2;
    }


    // resolve cert bundle from the shared data path
    const auto certPath = ( m_sharedDataPath / fs::path( m_configEndlesssAPI.certBundleRelative ) );
    if ( !fs::exists( certPath ) )
    {
        blog::error::cfg( FMTX( "Cannot find CA root certificates file [{}], required for networking" ), certPath.string() );
        return -2;
    }
    // rewrite config option with the full path
    m_configEndlesssAPI.certBundleRelative = certPath.string();

    // drop the app name, version and platform into the user agent string
    m_configEndlesssAPI.userAgentApp += fmt::format( FMTX( "{} ({})" ), GetAppNameWithVersion(), getOuroveonPlatform() );
    m_configEndlesssAPI.userAgentDb  += fmt::format( FMTX( " ({})"   ), GetAppNameWithVersion(), getOuroveonPlatform() );

    tf::Taskflow asyncDataLoadFlow;

    asyncDataLoadFlow.emplace(
        [this]()
        {
            // load the jam cache data (or try to)
            m_jamLibrary.load( *this );
        },
        [this]()
        {
            // load and analyse population list for user autocomplete
            m_endlesssPopulation.loadPopulationData( *this );
        },
        [this]()
        {
            // load prefetched jam name dictionary
            const auto bnsLoadResult = config::load( *this, m_jamNameService );
            if ( bnsLoadResult != config::LoadResult::Success )
            {
                blog::error::core( FMTX( "BNS file failed to load" ) );
            }
            else
            {
                blog::core( FMTX( "BNS loaded {} entries" ), m_jamNameService.entries.size() );
            }
        }
    );

    m_taskExecutor.run( asyncDataLoadFlow );


#if OURO_PLATFORM_WIN
    // bring up global external registrations / services that will auto-unwind on exit
    win32::ScopedInitialiseCOM      scopedCOM;

#if OURO_FEATURE_NST24
    nst::ScopedInitialiseNSTHosting scopedVST;
#endif // OURO_FEATURE_NST24
#endif // OURO_PLATFORM_WIN

#if OURO_EXCHANGE_IPC
    // create shared buffer for exchanging data with other apps
    if ( !m_endlesssExchangeIPC.init(
        endlesss::toolkit::Exchange::GlobalMapppingNameW,
        endlesss::toolkit::Exchange::GlobalMutexNameW,
        win32::details::IPC::Access::Write ) )
    {
        blog::error::core( FMTX( "Failed to open global memory for data exchange; feature disabled" ) );
    }
    else
    {
        blog::core( FMTX( "Broadcasting data exchange on [{}]" ), endlesss::toolkit::Exchange::GlobalMapppingNameA );
    }
#endif // OURO_EXCHANGE_IPC


    // create our wrapper around PA; this doesn't connect to a device, just does initial startup, enumeration, plugin duties
    m_mdAudio = std::make_unique<app::module::Audio>( GetAppName() );
    {
        const auto audioStatus = m_mdAudio->create( this );
        if ( !audioStatus.ok() )
        {
            blog::error::core( FMTX( "app::module::AudioServices unable to start : {}" ), audioStatus.ToString() );
            return -2;
        }
    }

    // central MIDI manager module
    m_mdMidi = std::make_unique<app::module::Midi>();
    {
        const auto midiStatus = m_mdMidi->create( this );
        if ( !midiStatus.ok() )
        {
            blog::error::core( FMTX( "app::module::Midi unable to start : {}" ), midiStatus.ToString() );
            return -2;
        }
    }

    // plug in stem data event processor
    {
        const auto stemDataProcessorStatus = m_stemDataProcessor.connect( m_appEventBus );
        if ( !stemDataProcessorStatus.ok() )
        {
            blog::error::core( FMTX( "unable to connect stem data processor ({})" ), stemDataProcessorStatus.ToString() );
            return -2;
        }
    }

    // finish up any async tasks run during startup
    m_taskExecutor.wait_for_all();

    // ---------------------------------
    // run the app main loop
    int appResult = Entrypoint();
    // ---------------------------------

    // unplug processor
    {
        std::ignore = m_stemDataProcessor.disconnect( m_appEventBus );
    }

    // unwind started services
    m_mdMidi->destroy();
    m_mdAudio->destroy();

    {
        base::EventBusClient m_eventBusClient( m_appEventBus );
        APP_EVENT_UNBIND( AsyncTaskActivity );
        APP_EVENT_UNBIND( NetworkActivity );
    }

    m_appEventBusClient = std::nullopt;

    // HDD hack to stop D++ leaking global memory
    dpp::delete_eventmap();

    return appResult;
}

// ---------------------------------------------------------------------------------------------------------------------
void Core::waitForConsoleKey()
{
#if OURO_PLATFORM_WIN
    blog::core( FMTX( "[press any key]\n" ) );
    _getch();
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
void Core::encodeExchangeData(
    const endlesss::live::RiffPtr& riffInstance,
    std::string_view jamName,
    const uint64_t playbackSampleCount,
    const endlesss::types::RiffPlaybackPermutation* permutation )
{
    const auto currentRiff = riffInstance.get();

    // update the Exchange data block; this also serves as a way to push sanitized beat / energy / playback 
    // information around other parts of the app. this pulls data from a few different sources to try and 
    // give a solid, accurate overview of what is coming out of the audio engine at this instant
    {
        // take basic riff & stem data from the Riff instance (even if it's null)
        endlesss::toolkit::Exchange::copyDetailsFromRiff( m_endlesssExchange, riffInstance, jamName.data() );

        if ( currentRiff != nullptr )
        {
            // copy in the current stem energy/pulse data that may have arrived from the mix
            m_stemDataProcessor.copyToExchangeData( m_endlesssExchange );

            // compute the progression (bar/percentage through riff) of playback based on the current sample, embed in Exchange
            endlesss::live::RiffProgression playbackProgression;

            const auto& timingData = currentRiff->getTimingDetails();
            timingData.ComputeProgressionAtSample(
                playbackSampleCount,
                playbackProgression );

            endlesss::toolkit::Exchange::copyDetailsFromProgression( m_endlesssExchange, playbackProgression );

            // take a snapshot of the mixer layer gains and apply that to the exchange data so "stem gain" is 
            // more representative of what's coming out of the audio pipeline
            if ( permutation != nullptr )
            {
                const auto currentMixPermutation = *permutation;
                for ( std::size_t i = 0; i < currentMixPermutation.m_layerGainMultiplier.size(); i++ )
                    m_endlesssExchange.m_stemGain[i] *= currentMixPermutation.m_layerGainMultiplier[i];
            }

            // copy in the scope data
            const dsp::Scope8::Result& scopeResult = m_mdAudio->getCurrentScopeResult();
            static_assert(dsp::Scope8::FrequencyBucketCount == endlesss::toolkit::Exchange::ScopeBucketCount, "fft bucket count mismatch");
            for ( std::size_t i = 0; i < endlesss::toolkit::Exchange::ScopeBucketCount; i++ )
                m_endlesssExchange.m_scope[i] = scopeResult[i];

            // mark exchange as having a full complement of data
            m_endlesssExchange.m_dataflags |= endlesss::toolkit::Exchange::DataFlags_Playback;
            m_endlesssExchange.m_dataflags |= endlesss::toolkit::Exchange::DataFlags_Scope;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void Core::emitAndClearExchangeData()
{
#if OURO_EXCHANGE_IPC
    if ( m_endlesssExchangeIPC.canWrite() )
        m_endlesssExchangeIPC.writeType( m_endlesssExchange );
#endif // OURO_EXCHANGE_IPC

    m_endlesssExchange.clear();
#if OURO_EXCHANGE_IPC
    m_endlesssExchange.m_dataWriteCounter = m_endlesssExchangeWriteCounter++;
#endif // OURO_EXCHANGE_IPC
}

// ---------------------------------------------------------------------------------------------------------------------
void Core::tickActivityUpdate()
{
    const float deltaTime = ImGui::GetIO().DeltaTime;
    const float lagRate = deltaTime * 0.5f;

    // simple pulse bars for 'tasks are going on in the background'
    {
        m_asyncTaskActivityIntensity = std::max( m_asyncTaskActivityIntensity - deltaTime * 0.5f, 0.0f );

        float pulseDelta = 0.0f;
        for ( std::size_t pI = 0; pI < m_asyncTaskPulseSlots.size(); pI++ )
        {
            const auto pulseSine = (2.0 + std::sin( pulseDelta + ImGui::GetTime() * 4.0f )) * 0.333333 * m_asyncTaskActivityIntensity;
            m_asyncTaskPulseSlots[pI] = static_cast<uint8_t>(std::round( pulseSine * 7.0 ));

            pulseDelta += 0.75f;
        }
    }

    m_avgNetActivity.update( 0 );
    if ( m_avgNetActivity.get() < 0.01 )    // clip at small values
        m_avgNetActivity.set( 0.0 );

    // raise up "network is working" value linearly if activity window shows .. activity
    if ( m_avgNetActivity.get() > 0 )
    {
        m_avgNetActivityLag += lagRate;
    }
    else
    {
        m_avgNetActivityLag -= lagRate;
    }
    m_avgNetActivityLag = std::clamp( m_avgNetActivityLag, 0.0, 1.0 );

    // update the pulse bar every so often
    m_avgNetPulseUpdateTimer -= deltaTime;
    if ( m_avgNetPulseUpdateTimer <= 0.0f )
    {
        // shunt pulses left
        const auto pulseCountMinusOne = m_avgNetPulseHistory.size() - 1;
        for ( std::size_t idx = 0; idx < pulseCountMinusOne; idx++ )
        {
            m_avgNetPulseHistory[idx] = m_avgNetPulseHistory[idx + 1];
        }
        // write new pulse value at the end
        const auto pulseWave1 = ( 2.0 + std::sin( ImGui::GetTime() * 2.0f ) ) * 0.333333;
        const auto pulseWave2 = ( 2.0 + std::sin( ImGui::GetTime() * 1.0f ) ) * 0.333333;
        m_avgNetPulseHistory[pulseCountMinusOne] = static_cast<uint8_t>(std::round( std::max( pulseWave1, pulseWave2 ) * m_avgNetActivityLag * 7.0 ));

        m_avgNetPulseUpdateTimer = 0.2f;
    }

    // update the per-second averages .. uh, every second
    m_avgNetRollingPerSecTimer -= deltaTime;
    if ( m_avgNetRollingPerSecTimer <= 0.0f )
    {
        m_avgNetPayloadPerSec.update( m_avgNetPayloadValue );
        m_avgNetPayloadValue = 0;

        m_avgNetErrorsPerSec.update( m_avgNetErrorCount );
        m_avgNetErrorCount = 0;

        m_avgNetRollingPerSecTimer = 1.0f;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
config::Frontend CoreGUI::createDefaultFrontendConfig() const
{
    return config::Frontend{};
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::checkLayoutConfig()
{
    config::Layout configLayout;
    
    blog::core( FMTX("loading layout config ...") );

    const auto feLoad = config::load( *this, configLayout );
    if ( feLoad == config::LoadResult::Success )
    {
        // check if we need to force a layout reload from defaults; master revision change means any existing customisation
        // would be invalid and reset in a busted looking UI
        if ( configLayout.guiMasterRevision != config::Layout::CurrentGuiMasterRevision )
        {
            blog::core( FMTX( "GUI master revision has changed {} -> {}, forcing layout reset" ),
                configLayout.guiMasterRevision,
                config::Layout::CurrentGuiMasterRevision );

            m_resetLayoutInNextUpdate = true;
        }
    }

    configLayout.guiMasterRevision = config::Layout::CurrentGuiMasterRevision;

    const auto feSave = config::save( *this, configLayout );
    if ( feSave != config::SaveResult::Success )
    {
        blog::error::core( FMTX( "warning; unable to save [{}]" ), config::Layout::StorageFilename );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::event_AddErrorPopup( const events::AddErrorPopup* eventData )
{
    activateModalPopup( eventData->m_title, [this, contents = eventData->m_contents]( const char* title )
        {
            imguiModalBasicErrorPopup( title, contents );
        });
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::event_AddToastNotification( const events::AddToastNotification* eventData )
{
    colour::Preset toastShade = colour::shades::callout;
    switch ( eventData->m_type )
    {
        default:
        case events::AddToastNotification::Type::Info:
            break;

        case events::AddToastNotification::Type::Error:
            toastShade = colour::shades::errors;
            break;
    }

    // TODO drive the timing values from data
    m_toasts.emplace_back( std::make_unique<Toast>( m_toastCreationID ++, toastShade, eventData->m_title, eventData->m_contents, 0.25f, 3.0f ) );
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::updateToasts()
{
    const float deltaTime = ImGui::GetIO().DeltaTime;

    Toasts survivingToasts;
    survivingToasts.reserve( m_toasts.size() );

    for ( auto& toast : m_toasts )
    {
        const bool bActive = toast->update( deltaTime );
        if ( bActive )
        {
            survivingToasts.emplace_back( std::move( toast ) );
        }
    }

    m_toasts = std::move( survivingToasts );

    static constexpr float cToastPaddingX = 20.0f;
    static constexpr float cToastPaddingY = 10.0f;
    static constexpr float cToastViewPadY = 30.0f;

    const auto& viewportSize = ImGui::GetMainViewport()->Size;
    float toastHeightUse = viewportSize.y - cToastViewPadY;


    ImGui::PushID( "toasts" );
    for ( const auto& toast : m_toasts )
    {
        ImGui::PushStyleColor( ImGuiCol_PopupBg, toast->m_shade.dark( toast->m_phaseT ) );
        ImGui::SetNextWindowSize( ImVec2( 450.0f, 80.0f ), ImGuiCond_Always );
        ImGui::SetNextWindowPos( ImVec2( viewportSize.x - cToastPaddingX, toastHeightUse - (toast->m_phaseT * 10.0f) ), ImGuiCond_Always, ImVec2( 1.0f, 1.0f ) );
        if ( ImGui::Begin(
            toast->m_creationID.c_str(),
            nullptr,
            ImGuiWindowFlags_Tooltip |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoDocking ) )
        {
            ImGui::TextWrapped( "%s", toast->m_title.c_str());
            ImGui::SeparatorBreak();
            ImGui::TextWrapped( "%s", toast->m_content.c_str() );

            toastHeightUse -= ImGui::GetWindowHeight() + cToastPaddingY;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::imguiModalBasicErrorPopup( const char* title, std::string_view errorMessage )
{
    const ImVec2 configWindowSize = ImVec2( 600.0f, 150.0f );
    ImGui::SetNextWindowContentSize( configWindowSize );

    if ( ImGui::BeginPopupModal( title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize ) )
    {
        const ImVec2 buttonSize( 240.0f, 32.0f );

        ImGui::TextWrapped( "%s", errorMessage.data());
        ImGui::SeparatorBreak();

        const auto panelRegionAvail = ImGui::GetContentRegionAvail();
        {
            const float alignButtonsToBase = panelRegionAvail.y - (buttonSize.y + 6.0f);
            ImGui::Dummy( ImVec2( 0, alignButtonsToBase ) );
        }

        if ( ImGui::Button( "Close", buttonSize ) )
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::imguiModalBasicOkCancel( const char* title, std::string_view displayMessage, std::function<void( bool )> onCompletionCallback )
{
    const ImVec2 configWindowSize = ImVec2( 600.0f, 220.0f );
    ImGui::SetNextWindowContentSize( configWindowSize );

    if ( ImGui::BeginPopupModal( title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize ) )
    {
        const ImVec2 buttonSize( 240.0f, 32.0f );

        ImGui::TextWrapped( "%s", displayMessage.data() );
        ImGui::SeparatorBreak();

        const auto panelRegionAvail = ImGui::GetContentRegionAvail();
        {
            const float alignButtonsToBase = panelRegionAvail.y - (buttonSize.y + 6.0f);
            ImGui::Dummy( ImVec2( 0, alignButtonsToBase ) );
        }

        if ( ImGui::Button( "OK", buttonSize ) )
        {
            onCompletionCallback( true );
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, configWindowSize.x - ( buttonSize.x * 2.0f ) );
        if ( ImGui::Button( "Cancel", buttonSize ) )
        {
            onCompletionCallback( false );
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
int CoreGUI::Entrypoint()
{
    const auto feLoad = config::load( *this, m_configFrontend );
    if ( feLoad != config::LoadResult::Success )
    {
        blog::core( FMTX( "unable to find or load [{}], assuming defaults" ), config::Frontend::StorageFilename );
        m_configFrontend = createDefaultFrontendConfig();
    }

    // bring up frontend; SDL and IMGUI
    m_mdFrontEnd = std::make_unique<app::module::Frontend>( m_configFrontend, GetAppNameWithVersion() );
    {
        const auto feStatus = m_mdFrontEnd->create( this );
        if ( !feStatus.ok() )
        {
            blog::error::core( FMTX( "app::module::Frontend unable to start : {}" ), feStatus.ToString() );
            return -2;
        }
    }

    {
        base::EventBusClient m_eventBusClient( m_appEventBus );
        APP_EVENT_BIND_TO( AddErrorPopup );
        APP_EVENT_BIND_TO( AddToastNotification );
    }

    // check in on the layout breadcrumb file, see if we need to force a layout reset
    checkLayoutConfig();

    registerMainMenuEntry( -1, "WINDOW", [this]()
    {
        if ( ImGui::MenuItem( "Toggle Border" ) )
            m_mdFrontEnd->toggleBorderless();

        ImGui::Separator();

        if ( ImGui::BeginMenu( "Reset" ) )
        {
            // reset the window position/size to defaults
            if ( ImGui::MenuItem( "View Size / Position" ) )
                m_mdFrontEnd->resetWindowPositionAndSizeToDefault();

            // reload the default UI layout when it is next convenient to do so
            if ( ImGui::MenuItem( "GUI Layout" ) )
                m_resetLayoutInNextUpdate = true;

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if ( ImGui::BeginMenu( "Developer" ) )
        {
            for ( const auto& developerFlag : m_developerMenuRegistry )
            {
                ImGui::MenuItem( developerFlag.first.c_str(), "", developerFlag.second );
            }

            // add entries from overriden client code - if it returns false, nothing was added
            if ( addDeveloperMenuItems() )
                ImGui::Separator();

#if OURO_DEBUG
            ImGui::Separator();
            if ( ImGui::MenuItem( "Test Toast (info)" ) )
            {
                m_appEventBus->send<::events::AddToastNotification>( ::events::AddToastNotification::Type::Info, "Test Toast Information", "Toast notification test contents\nMultiple lines\n" ICON_FA_SNOWMAN );
            }
            if ( ImGui::MenuItem( "Test Toast (error)" ) )
            {
                m_appEventBus->send<::events::AddToastNotification>( ::events::AddToastNotification::Type::Error, "Test Toast Error", "Oh no, something has gone badly wrong. Oh no. Oh brother." );
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Test Error Pop" ) )
            {
                m_appEventBus->send<::events::AddErrorPopup>( "Test Error Popup", "Here's my error message, something broke, oh boy." );
            }
            ImGui::Separator();
            if ( ImGui::MenuItem( "Test UUID generator" ) )
            {
                for ( uint32_t gen = 0; gen < 8; gen++ )
                {
                    blog::app( FMTX( "uuid | {}" ), data::generateUUID_V1( true ) );
                }
                for ( uint32_t gen = 0; gen < 8; gen++ )
                {
                    blog::app( FMTX( "uuid | {}" ), data::generateUUID_V1( false ) );
                }
            }
#endif // OURO_DEBUG
            ImGui::EndMenu();
        }
    });


    addDeveloperMenuFlag( "Performance Tracing", &m_showPerformanceWindow );
#if OURO_DEBUG
    addDeveloperMenuFlag( "ImGui Demo", &m_showImGuiDebugWindow );
#endif // OURO_DEBUG


    // run the app main loop
    int appResult = EntrypointGUI();

    {
        base::EventBusClient m_eventBusClient( m_appEventBus );
        APP_EVENT_UNBIND( AddToastNotification );
        APP_EVENT_UNBIND( AddErrorPopup );
    }

    // unwind started services
    m_mdFrontEnd->destroy();

    return appResult;
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::activateModalPopup( const std::string_view& label, ModalPopupExecutor&& executor )
{
    blog::core( FMTX( "activating modal [{}]" ), label );

    m_modalsWaiting.emplace_back( label );
    m_modalsActive.emplace_back( label, std::move( executor ) );
}

// ---------------------------------------------------------------------------------------------------------------------
bool CoreGUI::beginInterfaceLayout( const ViewportFlags viewportFlags )
{
    // begin tracking perf cost of the imgui 'build' stage
    m_perfData.m_moment.setToNow();

    // resetting/loading new layouts has to happen before layout is underway
    if ( m_resetLayoutInNextUpdate )
    {
        blog::core( FMTX( "resetting layout from default ..." ) );

        m_mdFrontEnd->reloadImguiLayoutFromDefault();
        m_resetLayoutInNextUpdate = false;
    }

    // tick the presentation layer, begin a new imgui Frame
    if ( m_mdFrontEnd->appTick() )
        return false;

    // run main-thread callbacks & misc updates
    {
        const float deltaTime = ImGui::GetIO().DeltaTime;
        std::scoped_lock<std::mutex> slm( m_mainThreadCallsMutex );
        for ( auto& mtCall : m_mainThreadCalls )
        {
            mtCall.second( deltaTime );
        }

        m_stemDataProcessor.update( deltaTime, 0.35f );
    }

    // inject dock space if we're expecting to lay the imgui out with docking
    if ( hasViewportFlag( viewportFlags, VF_WithDocking ) )
        ImGui::DockSpaceOverViewport( ImGui::GetMainViewport() );

    // add main menu bar
    if ( hasViewportFlag( viewportFlags, VF_WithMainMenu ) )
    {
        if ( ImGui::BeginMainMenuBar() )
        {
            // the only uncustomisable, first-entry menu item; everything else uses callbacks, run below
            if ( ImGui::BeginMenu( "OUROVEON" ) )
            {
                ImGui::MenuItem( GetAppNameWithVersion(), nullptr, nullptr, false );
                ImGui::MenuItem( OURO_FRAMEWORK_CREDIT, nullptr, nullptr, false );

                ImGui::Separator();
                if ( ImGui::MenuItem( "About" ) )
                {
                    activateModalPopup( "About", [&]( const char* title )
                    {
                        imguiModalAboutBox( title );
                    });
                }

                ImGui::Separator();
                if ( ImGui::MenuItem( "Quit" ) )
                {
                    m_mdFrontEnd->requestQuit();
                }

                ImGui::EndMenu();
            }

            // inject main menu items that have been registered by client code
            for ( const auto& mainMenuEntry : m_mainMenuEntries )
            {
                if ( ImGui::BeginMenu( mainMenuEntry.m_name.c_str() ) )
                {
                    for ( const auto& callback : mainMenuEntry.m_callbacks )
                    {
                        callback();
                    }
                    ImGui::EndMenu();
                }
            }

            ImGui::EndMainMenuBar();
        }
    }

    // status bar at base of viewport
    if ( hasViewportFlag( viewportFlags, VF_WithStatusBar ) )
    {
        if ( ImGui::BeginStatusBar() )
        {
            // not white, choose a bright but blended theme colour for text on the status bar
            ImGui::PushStyleColor( ImGuiCol_Text, GImGui->Style.Colors[ImGuiCol_ResizeGripActive] );

            ImGui::TextUnformatted( GetAppName() );
            ImGui::Separator();

            // left-aligned blocks; adds in order, dummy-bulks out space to the specified minimum size as we go.
            // no controls on blocks that end up larger than the specified size, no clipping done

            for ( const auto& statusBlock : m_statusBarBlocksLeft )
            {
                const float regionBefore = ImGui::GetContentRegionAvail().x;
                statusBlock.m_callback();
                const float regionAfter = ImGui::GetContentRegionAvail().x;

                const float padSpace = statusBlock.m_size - (regionBefore - regionAfter);
                if ( padSpace > 0 )
                {
                    ImGui::Dummy( ImVec2( padSpace, 0.0f ) );
                }
                ImGui::Separator();
            }

            // right alignment; we save the left-hand cursor and then re-apply dummy elements on each
            //      iteration to add the expected spacing - note there is no clipping yet so right-aligned things may overflow

            constexpr float audioLoadBlockSize = 90.0f;
            const float contentRemainingWidth = ImGui::GetContentRegionAvail().x;

            float cursorSave = ImGui::GetCursorPosX();
            float rightLoad  = audioLoadBlockSize;

            // add the audio load indicator first, far right
            {
                ImGui::Dummy( ImVec2( contentRemainingWidth - audioLoadBlockSize, 0.0f ) );
                ImGui::Separator();

                const double audioEngineLoad = m_mdAudio->getAudioEngineCPULoadPercent();
                m_audoLoadAverage.update( audioEngineLoad );

                ImGui::Text( " CPU %03.0f%%", m_audoLoadAverage.get() );
            }

            for ( const auto& statusBlock : m_statusBarBlocksRight )
            {
                ImGui::SetCursorPosX( cursorSave );

                ImGui::Dummy( ImVec2( contentRemainingWidth - ( statusBlock.m_size + rightLoad ), 0.0f ) );
                ImGui::Separator();

                statusBlock.m_callback();

                rightLoad += statusBlock.m_size;
            }

            ImGui::PopStyleColor();
            ImGui::EndStatusBar();
        }
    }

#if OURO_DEBUG
    if ( m_showImGuiDebugWindow )
        ImGui::ShowDemoWindow( &m_showImGuiDebugWindow );
#endif // OURO_DEBUG

    ImGui::PushFont( m_mdFrontEnd->getFont( app::module::Frontend::FontChoice::FixedMain ) );

    // show built-in UI when we have docking & menus in play
    if ( hasViewportFlag( viewportFlags, VF_WithMainMenu ) &&
         hasViewportFlag( viewportFlags, VF_WithDocking ) )
    {
        if ( m_showPerformanceWindow )
            ImGuiPerformanceTracker();

        m_mdAudio->imgui( *this );
        m_mdMidi->imgui( *this );
    }

    // run active modal dialog callbacks
    for ( const auto& modalPair : m_modalsActive )
    {
        std::get<1>( modalPair )( std::get<0>( modalPair ).c_str() );
    }
    // .. and if any were run, go check if they closed and tidy up appropriately if so
    if ( !m_modalsActive.empty() )
    {
        // purge any modals that ImGui no longer claims as "Open"
        auto new_end = std::remove_if( m_modalsActive.begin(),
                                       m_modalsActive.end(),
                                       [this]( const std::tuple< std::string, ModalPopupExecutor >& ma )
                                       {
                                           const auto modalName = std::get<0>( ma );

                                           // check the "waiting" list; don't remove anything that hasn't had a chance to be processed at least once yet
                                           if ( std::find( m_modalsWaiting.begin(), m_modalsWaiting.end(), modalName ) != m_modalsWaiting.end() )
                                               return false;

                                           return !ImGui::IsPopupOpen( modalName.c_str() );
                                       });

        // log out the ones we marked as closed and then trim the list of active dialogs
        for ( auto it = new_end; it != m_modalsActive.end(); ++it )
        {
            blog::core( FMTX( "modal discard : [{}]" ), std::get<0>( *it ) );
        }
        m_modalsActive.erase( new_end, m_modalsActive.end() );
    }

    // run pop-up file dialog as a special top-level case as it requires some custom handling
    if ( m_activeFileDialog != nullptr )
    {
        ImGuiIO& io = ImGui::GetIO();
        const auto maxSize = ImVec2( io.DisplaySize.x, io.DisplaySize.y );
        const auto minSize = ImVec2( maxSize.x * 0.75f, maxSize.y * 0.5f );

        const auto dialogKey = m_activeFileDialog->GetOpenedKey();
        if ( m_activeFileDialog->Display( dialogKey.c_str(), ImGuiWindowFlags_NoCollapse, minSize, maxSize ) )
        {
            if ( m_activeFileDialog->IsOk() )
            {
                m_fileDialogCallbackOnOK( *m_activeFileDialog );
            }

            m_activeFileDialog->Close();

            m_activeFileDialog       = nullptr;
            m_fileDialogCallbackOnOK = nullptr;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::finishInterfaceLayoutAndRender()
{
    // trigger popups that are waiting after we're clear of the imgui UI stack
    for ( const auto& modalToPop : m_modalsWaiting )
    {
        blog::core( FMTX( "modal open : [{}]" ), modalToPop );
        ImGui::OpenPopup( modalToPop.c_str() );
    }
    m_modalsWaiting.clear();

    // update and display any toast notifications
    updateToasts();

    ImGui::PopFont();

    // get perf cost of the UI 'build' code
    m_perfData.m_uiPreRender = m_perfData.m_moment.delta< std::chrono::milliseconds >();
    m_perfData.m_moment.setToNow();

    // flush the main thread event bus #HDD move to Core:: main tick 
    m_appEventBus->mainThreadDispatch();

    // update networking averages #HDD move to Core:: main tick 
    tickActivityUpdate();

    m_perfData.m_uiEventBus = m_perfData.m_moment.delta< std::chrono::milliseconds >();
    m_perfData.m_moment.setToNow();

    m_mdFrontEnd->appRenderBegin();

    // callbacks for pre-imgui custom rendering
    for ( const auto& renderCallback : m_preImguiRenderCallbacks )
    {
        renderCallback();
    }

    m_mdFrontEnd->appRenderImguiDispatch();

    // callbacks for post-imgui custom rendering
    for ( const auto& renderCallback : m_postImguiRenderCallbacks )
    {
        renderCallback();
    }

    // perf cost of UI render dispatch
    m_perfData.m_uiPostRender = m_perfData.m_moment.delta< std::chrono::milliseconds >();

    m_mdFrontEnd->appRenderFinalise();

    // flush render callbacks
    m_preImguiRenderCallbacks.clear();
    m_postImguiRenderCallbacks.clear();
}


// ---------------------------------------------------------------------------------------------------------------------
void CoreGUI::ImGuiPerformanceTracker()
{
    if ( !m_showPerformanceWindow )
        return;

    if ( ImGui::Begin( ICON_FA_CLOCK " Profiling###coregui_profiling" ) )
    {
        const float column0size = 90.0f;
        const auto& aeState = m_mdAudio->getState();

        if ( ImGui::BeginTable( "##aengine", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_ResizeGripHovered ) );
            ImGui::TableSetupColumn( "Buffers", ImGuiTableColumnFlags_WidthFixed, column0size );
            ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_None );
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Min" );
            ImGui::TableNextColumn(); ImGui::Text( "  %7i", m_mdAudio->getState().m_minBufferFillSize );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Max" );
            ImGui::TableNextColumn(); ImGui::Text( "  %7i", m_mdAudio->getState().m_maxBufferFillSize );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Rate" );
            ImGui::TableNextColumn(); ImGui::Text( "  %7i", m_mdAudio->getSampleRate() );

            ImGui::EndTable();
        }

        static constexpr auto executionStages = app::module::Audio::ExposedState::cNumExecutionStages;
        using PerfPoints = std::array< int64_t, executionStages >;

        static int32_t maxCountdown = 256;       // ignore early [max] readings to disregard boot-up spikes
        static PerfPoints maxPerf = { 0, 0, 0, 0, 0 };
        PerfPoints totalPerf;
        totalPerf.fill( 0 );

        for ( auto cI = 1; cI < executionStages; cI++ )
        {
            const int64_t perfValue = aeState.m_perfCounters[cI].getInt64();

            // only update max scores once initial grace period has passed
            if ( maxCountdown == 0 )
            {
                maxPerf[cI] = std::max( maxPerf[cI], perfValue );
            }

            totalPerf[cI] += perfValue;
        }
        maxCountdown = std::max( 0, maxCountdown - 1 );

        uint64_t totalSum = 0;
        for ( auto cI = 0; cI < executionStages; cI++ )
        {
            totalSum += totalPerf[cI];
        }

        if ( ImGui::BeginTable( "##perf_stats_avg", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_ResizeGripHovered ) );
            ImGui::TableSetupColumn( "AVERAGE", ImGuiTableColumnFlags_WidthFixed, column0size );
            ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_None );
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Mixer" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", totalPerf[1] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "VST" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", totalPerf[2] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Scope" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", totalPerf[3] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Interleave" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", totalPerf[4] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Recorder" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", totalPerf[5] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Total" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", totalSum );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Load" );
            ImGui::TableNextColumn(); ImGui::Text( "%9.1f %%", m_mdAudio->getAudioEngineCPULoadPercent() );

            ImGui::EndTable();
        }
        if ( ImGui::BeginTable( "##perf_stats_max", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_ResizeGripHovered ) );
            ImGui::TableSetupColumn( "PEAK", ImGuiTableColumnFlags_WidthFixed, column0size );
            ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_None );
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Mixer" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", maxPerf[1] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "VST" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", maxPerf[2] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Interleave" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", maxPerf[3] );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Recorder" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIi64 " us", maxPerf[4] );

            ImGui::TableNextColumn();
            ImGui::Spacing();
            if ( ImGui::Button( "Reset" ) )
            {
                maxPerf.fill( 0 );
            }
            ImGui::Spacing(); ImGui::TableNextColumn();

            ImGui::EndTable();
        }
        if ( ImGui::BeginTable( "##perf_stats_ext", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_ResizeGripHovered ) );
            ImGui::TableSetupColumn( "UI", ImGuiTableColumnFlags_WidthFixed, column0size );
            ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_None );
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Event Bus" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIu64 " ms", m_perfData.m_uiEventBus.count() );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Build" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIu64 " ms", m_perfData.m_uiPreRender.count() );
            ImGui::TableNextColumn(); ImGui::TextUnformatted( "Dispatch" );
            ImGui::TableNextColumn(); ImGui::Text( "%9" PRIu64 " ms", m_perfData.m_uiPostRender.count() );

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

app::CoreGUI::UIInjectionHandle CoreGUI::registerStatusBarBlock( const StatusBarAlignment alignment, const float size, const UIInjectionCallback& callback )
{
    const auto newHandle = ++m_injectionHandleCounter;

    if ( alignment == StatusBarAlignment::Left )
        m_statusBarBlocksLeft.emplace_back( newHandle, size, callback );
    else
        m_statusBarBlocksRight.emplace_back( newHandle, size, callback );

    return newHandle;
}

bool CoreGUI::unregisterStatusBarBlock( const UIInjectionHandle handle )
{
    bool foundSomething = false;
    {
        auto new_end = std::remove_if( m_statusBarBlocksLeft.begin(),
                                       m_statusBarBlocksLeft.end(),
                                       [handle]( const StatusBarBlock& sbb )
                                       {
                                           return sbb.m_handle == handle;
                                       });
        foundSomething |= (new_end != m_statusBarBlocksLeft.end());
        m_statusBarBlocksLeft.erase( new_end, m_statusBarBlocksLeft.end() );
    }
    {
        auto new_end = std::remove_if( m_statusBarBlocksRight.begin(),
                                       m_statusBarBlocksRight.end(),
                                       [handle]( const StatusBarBlock& sbb )
                                       {
                                           return sbb.m_handle == handle;
                                       });
        foundSomething |= (new_end != m_statusBarBlocksRight.end());
        m_statusBarBlocksRight.erase( new_end, m_statusBarBlocksRight.end() );
    }

    return foundSomething;
}

app::CoreGUI::UIInjectionHandle CoreGUI::registerMainMenuEntry( const int32_t ordering, const std::string& menuName, const UIInjectionCallback& callback )
{
    const auto newHandle = ++m_injectionHandleCounter;

    MenuMenuEntry* menuEntry = nullptr;

    // find existing top level menu? 
    for ( auto& existingMenu : m_mainMenuEntries )
    {
        if ( existingMenu.m_name == menuName )
        {
            menuEntry = &existingMenu;
            break;
        }
    }
    // .. or not, so create new entry for this name
    if ( menuEntry == nullptr )
    {
        auto& newEntry = m_mainMenuEntries.emplace_back();
        newEntry.m_name = menuName;
        menuEntry = &newEntry;
    }

    menuEntry->m_ordering = ordering;
    menuEntry->m_handles.emplace_back( newHandle );
    menuEntry->m_callbacks.emplace_back( callback );

    // resort menu stack each time we add anything
    std::sort( m_mainMenuEntries.begin(), m_mainMenuEntries.end(), []( const MenuMenuEntry& lhs, const MenuMenuEntry& rhs )
        {
            return lhs.m_ordering < rhs.m_ordering;
        });

    return newHandle;
}

bool CoreGUI::unregisterMainMenuEntry( const UIInjectionHandle handle )
{
    return false;
}

void CoreGUI::registerMainThreadCall( std::string_view name, MainThreadCall func )
{
    std::scoped_lock<std::mutex> slm( m_mainThreadCallsMutex );
    m_mainThreadCalls.emplace( name, func );
}

void CoreGUI::unregisterMainThreadCall( std::string_view name )
{
    std::scoped_lock<std::mutex> slm( m_mainThreadCallsMutex );
    m_mainThreadCalls.erase( name );
}


void CoreGUI::registerRenderCallback( const RenderPoint rp, const RenderInjectionCallback& callback )
{
    switch ( rp )
    {
    case ICoreCustomRendering::RenderPoint::PreImgui:  m_preImguiRenderCallbacks.push_back( callback );  break;
    case ICoreCustomRendering::RenderPoint::PostImgui: m_postImguiRenderCallbacks.push_back( callback ); break;
    default:
        ABSL_ASSERT( false );
    }
}

void CoreGUI::imguiModalAboutBox( const char* title )
{
    const ImVec2 configWindowSize = ImVec2( 720.0f, 690.0f );
    ImGui::SetNextWindowContentSize( configWindowSize );

    ImGui::PushStyleColor( ImGuiCol_PopupBg, ImGui::GetStyleColorVec4( ImGuiCol_ChildBg ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 18.0f, 18.0f ) );

    if ( ImGui::BeginPopupModal( title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize ) )
    {
        static constexpr auto markdownText = R"(# OUROVEON

Developed by Harry Denholm / ishani @ [ishani.org/shelf/ouroveon/](https://ishani.org/shelf/ouroveon/)

Built with many wonderful [3rd party components](https://github.com/Unbundlesss/OUROVEON/blob/main/LIBS.md)

___
 
**Thanks fly out to ...**

  * [von](https://soundcloud.com/audubonswampgarden) for support, ideas, testing and being my partner in experimental audio crime

  * [firephly](https://endlesss.fm/firephly) and [oddSTAR](https://endlesss.fm/oddstar) for going so far as to make entire tracks using early versions

  * [littlewing](https://linktr.ee/littlewingart) for their foresight and efforts to keep records of early Endlesss jams; the archive would be much smaller without their help

  * For taking time to test, use and feedback on the tools ...
    * [afta8](https://endlesss.fm/afta8)
    * [dsorce](https://endlesss.fm/dsorce)
    * dgQwerty
    * [loop](https://endlesss.fm/loop)
    * [ludi](https://endlesss.fm/ludi)
    * [lwlkc](https://endlesss.fm/lwlkc)
    * [njlang](https://endlesss.fm/njlang)
    * [slowgaffle](https://endlesss.fm/slowgaffle)

  * The folks of [Track Club](https://trackclub.live) for early encouragement and a million hours of source music to be inspired and distracted by

)";
        {
            getFrontend()->imguiRenderMarkdown( markdownText );

            if ( ImGui::Button( "   Close   " ) )
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace app
