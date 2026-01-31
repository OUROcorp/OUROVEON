//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#include "pch.h"
#include "app/core.h"
#include "app/module.audio.h"

#include "config/audio.h"

#include "base/mathematics.h"
#include "dsp/scope.h"

#include "plug/utils.clap.h"
#include "effect/vst2/host.h"
#include "win32/errors.h"
#include "win32/utils.h"

#include "ssp/ssp.file.wav.h"
#include "ssp/ssp.file.flac.h"

#include "portaudio.h"


namespace app {
namespace module {

#if OURO_HAS_CLAP

// ---------------------------------------------------------------------------------------------------------------------
namespace clap_detail {

static const void* clapGetExtension( const struct clap_host* host, const char* extension_id ) noexcept
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    return clapEffect->m_audioModule->clapGetExtension( *clapEffect, extension_id );
}

static void clapRequestRestart( const struct clap_host* host ) noexcept
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    clapEffect->m_audioModule->clapRequestRestart( *clapEffect );
}

static void clapRequestProcess( const struct clap_host* host ) noexcept
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    clapEffect->m_audioModule->clapRequestProcess( *clapEffect );
}

static void clapRequestCallback( const struct clap_host* host ) noexcept
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    clapEffect->m_audioModule->clapRequestCallback( *clapEffect );
}

static void clapHostLog( const clap_host_t* host, clap_log_severity severity, const char* msg )
{
    static constexpr std::array< std::string_view, 7 > ClapSeverityString =
    {
        "  (Debug)",
        "   (Info)",
        "(Warning)",
        "  (Error)",
        "  (Fatal)",
        "(Host-Misbehave)",
        "(Plug-Misbehave)",
    };

    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    switch ( severity )
    {
        default:
            blog::plug( FMTX( "[CLAP:{}] (unknown) {}" ), clapEffect->m_displayName, msg );
            break;

        case CLAP_LOG_DEBUG:
        case CLAP_LOG_INFO:
            blog::plug( FMTX( "[CLAP:{}] {} {}" ), clapEffect->m_displayName, ClapSeverityString[(std::size_t)severity], msg );
            break;

        case CLAP_LOG_WARNING:
        case CLAP_LOG_ERROR:
        case CLAP_LOG_FATAL:
        case CLAP_LOG_HOST_MISBEHAVING:
        case CLAP_LOG_PLUGIN_MISBEHAVING:
            blog::plug( FMTX( "[CLAP:{}] {} {}" ), clapEffect->m_displayName, ClapSeverityString[(std::size_t)severity], msg );
            break;
    }
}

static void clapLatencyChanged( const clap_host_t* host )
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    blog::debug::plug( FMTX( "[CLAP:{}] <host> => clapLatencyChanged" ), clapEffect->m_displayName );
    clapEffect->getRuntimeInstance().updateLatency();
}

static bool clapIsRescanFlagSupported(const clap_host_t* host, uint32_t flag)
{
    return true;
}

static void clapRescan(const clap_host_t* host, uint32_t flags)
{

}

static void clapParamRescan( const clap_host_t* host, clap_param_rescan_flags flags )
{

}

static void clapParamClear( const clap_host_t* host, clap_id param_id, clap_param_clear_flags flags )
{

}

static void clapParamRequestFlush(const clap_host_t* host)
{

}

static void clapMarkDirty(const clap_host_t* host)
{

}

static void clapGuiResizeHintsChanged(const clap_host_t* host)
{
}

// Request the host to resize the client area to width, height.
// Return true if the new size is accepted, false otherwise.
// The host doesn't have to call set_size().
//
// Note: if not called from the main thread, then a return value simply means that the host
// acknowledged the request and will process it asynchronously. If the request then can't be
// satisfied then the host will call set_size() to revert the operation.
// [thread-safe & !floating]
static bool clapGuiRequestResize(const clap_host_t* host, uint32_t width, uint32_t height)
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    return clapEffect->getRuntimeInstance().uiRequestResize( width, height );
}

// Request the host to show the plugin gui.
// Return true on success, false otherwise.
// [thread-safe]
static bool clapGuiRequestShow(const clap_host_t* host)
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    return clapEffect->getRuntimeInstance().uiRequestShow();
}

// Request the host to hide the plugin gui.
// Return true on success, false otherwise.
// [thread-safe]
static bool clapGuiRequestHide(const clap_host_t* host)
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    return clapEffect->getRuntimeInstance().uiRequestHide();
}

// The floating window has been closed, or the connection to the gui has been lost.
//
// If was_destroyed is true, then the host must call clap_plugin_gui->destroy() to acknowledge
// the gui destruction.
// [thread-safe]
static void clapGuiClosed(const clap_host_t* host, bool was_destroyed)
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    clapEffect->getRuntimeInstance().uiRequestClosed( was_destroyed );
}

static bool clapIsMainThread( const clap_host_t* host )
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    return clapEffect->m_audioModule->isMainThreadID( std::this_thread::get_id() );
}

static bool clapIsAudioThread( const clap_host_t* host )
{
    CLAPEffect* clapEffect = static_cast<CLAPEffect*>(host->host_data);
    ABSL_ASSERT( clapEffect != nullptr );

    return clapEffect->m_audioModule->isAudioThreadID( std::this_thread::get_id() );
}

} // namespace clap_detail

#endif // OURO_HAS_CLAP

// ---------------------------------------------------------------------------------------------------------------------
Audio::Audio( const char* appName )
#if OURO_HAS_CLAP
    : m_clapHost {
        CLAP_VERSION,
        nullptr,
        appName,
        OURO_FRAMEWORK_CREDIT,
        OURO_FRAMEWORK_URL,
        OURO_FRAMEWORK_VERSION,
        clap_detail::clapGetExtension,
        clap_detail::clapRequestRestart,
        clap_detail::clapRequestProcess,
        clap_detail::clapRequestCallback }
    , m_clapHostLog {
        clap_detail::clapHostLog }
    , m_clapHostLatency {
        clap_detail::clapLatencyChanged }
    , m_clapAudioPorts {
        clap_detail::clapIsRescanFlagSupported,
        clap_detail::clapRescan }
    , m_clapHostParams {
        clap_detail::clapParamRescan,
        clap_detail::clapParamClear,
        clap_detail::clapParamRequestFlush }
    , m_clapHostState {
        clap_detail::clapMarkDirty }
    , m_clapHostGui {
        clap_detail::clapGuiResizeHintsChanged,
        clap_detail::clapGuiRequestResize,
        clap_detail::clapGuiRequestShow,
        clap_detail::clapGuiRequestHide,
        clap_detail::clapGuiClosed }
    , m_clapHostThreadCheck {
        clap_detail::clapIsMainThread,
        clap_detail::clapIsAudioThread }
#endif // OURO_HAS_CLAP
{
}

// ---------------------------------------------------------------------------------------------------------------------
Audio::~Audio()
{
}

// ---------------------------------------------------------------------------------------------------------------------
absl::Status Audio::create( app::Core* appCore )
{
    const auto baseStatus = Module::create( appCore );
    if ( !baseStatus.ok() )
        return baseStatus;

    // bring up PA, check we have things to play with
    const auto paErrorInit = Pa_Initialize();
    if ( paErrorInit != paNoError )
    {
        return absl::UnavailableError( 
            fmt::format( "PortAudio failed to initalise, {}", Pa_GetErrorText( paErrorInit ) ) );
    }

    const auto paDeviceCount = Pa_GetDeviceCount();
    if ( paDeviceCount <= 0 )
    {
        return absl::UnavailableError( "PortAudio was unable to iterate or find any audio devices" );
    }

    blog::core( "Initialised PortAudio [ {} ]", Pa_GetVersionText() );
    blog::core( "                      [ .. found {} audio endpoints ]", paDeviceCount );

    const auto hostAPIcount     = Pa_GetHostApiCount();
    const auto hostAPIdefault   = Pa_GetDefaultHostApi();
    blog::core( "                      [ .. {} host APIs ]", hostAPIcount );
    for ( auto hApi = 0; hApi < hostAPIcount; hApi++ )
    {
        const auto* apiInfo = Pa_GetHostApiInfo( hApi );
        blog::core( "                      [ {} {}]", apiInfo->name, ( hostAPIdefault == hApi ) ? "(default) " : "" );
    }

#if OURO_HAS_CLAP

    // go collect & analyse local CLAP plugins in the background, building the library of known plugins
    m_pluginStashClap = plug::stash::CLAP::createAndPopulateAsync( appCore->getTaskExecutorPlugins() );

#endif // OURO_HAS_CLAP

    // stash thread ID, used to check when things are running on main vs audio
    m_mainThreadID = std::this_thread::get_id();

    return absl::OkStatus();
}

// ---------------------------------------------------------------------------------------------------------------------
void Audio::destroy()
{
    if ( m_paStream != nullptr )
        termOutput();

    // graceful audio shutdown
    const auto paErrorTerm = Pa_Terminate();
    if ( paErrorTerm != paNoError )
    {
        blog::error::core( "PortAudio failed to terminate cleanly, {}", Pa_GetErrorText( paErrorTerm ) );
    }

    Module::destroy();
}

// ---------------------------------------------------------------------------------------------------------------------
absl::Status Audio::initOutput( const config::Audio& outputDevice, const config::Spectrum& scopeSpectrumConfig )
{
    blog::core( "Establishing audio output using [{}] @ {}", outputDevice.lastDevice, outputDevice.sampleRate );

    // cache sample rate & buffer choice
    m_outSampleRate = outputDevice.sampleRate;

    // create the rolling spectrum scope
    m_scope = std::make_unique< dsp::Scope8 >( 1.0f / 60.0f, m_outSampleRate, scopeSpectrumConfig );

    PaStreamParameters outputParameters;
    if ( AudioDeviceQuery::generateStreamParametersFromDeviceConfig( outputDevice, outputParameters ) < 0 )
    {
        return absl::UnavailableError( "Audio engine was unable to resolve stream parameters; could not find an output device to use?" );
    }

    PaError err = Pa_OpenStream(
                        &m_paStream,
                        nullptr,
                        &outputParameters,
                        outputDevice.sampleRate,
                        outputDevice.bufferSize,
                        paNoFlag,
                        PortAudioCallback,
                        (void *)this );

    if ( err != paNoError )
    {
        return absl::UnavailableError( fmt::format( FMTX( "Audio engine error - Pa_OpenStream failed [{}]" ), Pa_GetErrorText( err ) ) );
    }

    m_outMaxBufferSize = outputDevice.bufferSize;
    if ( m_outMaxBufferSize == 0 )
        m_outMaxBufferSize = getMaximumBufferSize();    // when == 0 PortAudio (rather, the output device) will automatically adjust buffer sizes 
                                                        // so we need to allocate a large-enough-to-handle-anything buffer instead even if most of it remains unused

    m_mixerBuffers = new OutputBuffer( m_outMaxBufferSize );


    err = Pa_StartStream( m_paStream );
    if ( err != paNoError )
    {
        termOutput();

        return absl::UnavailableError( fmt::format( FMTX( "Audio engine error - Pa_StartStream failed [{}]" ), Pa_GetErrorText( err ) ) );
    }

    // stash the output latency reported as milliseconds, used by Ableton Link compensation
    m_outLatencyMs = ( std::chrono::microseconds( llround( outputParameters.suggestedLatency * 1.0e6 ) ) );

#if OURO_HAS_CLAP
    // setup & bind clap processing structures
    {
        memset( &m_clapProcess, 0, sizeof( m_clapProcess ) );
        memset( &m_clapProcessTransport, 0, sizeof( m_clapProcessTransport ) );

        m_clapProcess.transport     = &m_clapProcessTransport;

        m_clapProcess.in_events     = m_clapProcessEventsIn.clapInputEvents();
        m_clapProcess.out_events    = m_clapProcessEventsOut.clapOutputEvents();
    }
#endif // OURO_HAS_CLAP

    return absl::OkStatus();
}

// ---------------------------------------------------------------------------------------------------------------------
void Audio::termOutput()
{
    if ( m_paStream )
    {
        Pa_StopStream( m_paStream );
        Pa_CloseStream( m_paStream );
        m_paStream = nullptr;
    }

    if ( m_mixerBuffers != nullptr )
    {
        delete m_mixerBuffers;
        m_mixerBuffers = nullptr;
    }

    m_outSampleRate = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// returns true if the early boot-stage configuration of plugin support is done; the audio engine and plugin loading
// shouldn't continue if this isn't complete as we need the basic manifest loaded upfront
bool Audio::isPreflightSetupComplete() const
{
#if OURO_HAS_CLAP

    if ( m_pluginStashClap == nullptr )
        return false;

    return m_pluginStashClap->asyncPopulateFinished();

#else // OURO_HAS_CLAP

    return true;

#endif 
}

// ---------------------------------------------------------------------------------------------------------------------
double Audio::getAudioEngineCPULoadPercent() const
{
    if ( m_paStream == nullptr )
        return 0;

    return Pa_GetStreamCpuLoad( m_paStream ) * 100.0;
}

// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::toggleMute()
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_mixThreadCommandQueue.emplace( MixThreadCommand::ToggleMute );
    return AsyncCommandCounter{ commandCounter };
}

// ---------------------------------------------------------------------------------------------------------------------
int Audio::PortAudioCallback(
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData )
{
    app::module::Audio* audioModule = static_cast<app::module::Audio*>(userData);

    if ( !audioModule->m_threadInitOnce )
    {
        ouroveonThreadEntry( OURO_THREAD_PREFIX "AudioMix" );

        audioModule->m_audioThreadID = std::this_thread::get_id();

        audioModule->m_threadInitOnce = true;
    }

    return audioModule->PortAudioCallbackInternal( outputBuffer, framesPerBuffer, timeInfo );
}

// ---------------------------------------------------------------------------------------------------------------------
void Audio::ProcessMixCommandsOnMixThread()
{
    MixThreadCommandData mixCmdData;
    while ( m_mixThreadCommandQueue.try_dequeue( mixCmdData ) )
    {
        const int64_t commandI64 = mixCmdData.getI64();

        switch ( mixCmdData.getCommand() )
        {
            case MixThreadCommand::Invalid:
            default:
                ABSL_ASSERT( false );
                break;

            case MixThreadCommand::SetMixerFunction:
                m_mixerInterface = mixCmdData.getPtrAs<MixerInterface>();
                break;
            case MixThreadCommand::InstallPlugin:
#if OURO_FEATURE_NST24
                m_pluginStack.emplace_back( mixCmdData.getPtrAs<nst::Instance>() );
#endif // OURO_FEATURE_NST24
                break;
            case MixThreadCommand::ClearAllPlugins:
#if OURO_FEATURE_NST24
                m_pluginStack.clear();
#endif // OURO_FEATURE_NST24
                break;
            case MixThreadCommand::TogglePluginBypass:
#if OURO_FEATURE_NST24
                m_pluginBypass = !m_pluginBypass;
#endif // OURO_FEATURE_NST24
                break;
            case MixThreadCommand::ToggleMute:
                m_mute = !m_mute;
                break;
            case MixThreadCommand::AttachSampleProcessor:
                {
                    ssp::SampleStreamProcessorInstance instance;
                    if ( m_sampleProcessorsToInstall.try_dequeue( instance ) )
                    {
                        m_sampleProcessorsInstalled.emplace_back( instance );
                        blog::mix( "attaching SSP [{}]", instance->getInstanceID().get() );
                    }
                    else
                    {
                        blog::error::mix( "unable to deque new SSP on mix thread" );
                    }
                }
                break;
            case MixThreadCommand::DetatchSampleProcessor:
                {
                    ssp::StreamProcessorInstanceID idToRemove( (uint32_t)commandI64 );

                    auto new_end = std::remove_if( m_sampleProcessorsInstalled.begin(),
                                                   m_sampleProcessorsInstalled.end(),
                                                  [=]( ssp::SampleStreamProcessorInstance& ssp )
                                                  {
                                                      return ssp->getInstanceID() == idToRemove;
                                                  });
                    if ( new_end == m_sampleProcessorsInstalled.end() )
                    {
                        blog::error::mix( "unable to detach SSP [{}]", commandI64 );
                    }
                    else
                    {
                        blog::mix( "detaching SSP [{}]", commandI64 );
                    }
                    m_sampleProcessorsInstalled.erase( new_end, m_sampleProcessorsInstalled.end() );
                }
                break;
        }

        m_mixThreadCommandsComplete++;
    }
}

#if OURO_HAS_CLAP

// ---------------------------------------------------------------------------------------------------------------------
void Audio::ProcessClapEventsOnMixThread()
{
    for ( uint32_t eventIndex = 0; eventIndex < static_cast<uint32_t>( m_clapProcessEventsOut.size() ); ++eventIndex )
    {
        clap_event_header* eventHeader = m_clapProcessEventsOut.get( eventIndex );
        blog::plug( FMTX( "[CLAP] event-out : {} @ sample {}" ), plug::utils::clapEventTypeToString( eventHeader->type ), eventHeader->time );
    }

    m_clapProcessEventsOut.clear();
    m_clapProcessEventsIn.clear();
}

#endif // OURO_HAS_CLAP

// ---------------------------------------------------------------------------------------------------------------------
int Audio::PortAudioCallbackInternal( void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo )
{
    ABSL_ASSERT( m_mixerBuffers );
    ProcessMixCommandsOnMixThread();

    m_state.mark( ExposedState::ExecutionStage::Start );

    // pass control to the installed mixer, if we have one
    if ( m_mixerInterface != nullptr )
    {
        OutputSignal outputSignal;
        outputSignal.m_linearGain   = m_outputSignalGain;

        m_mixerInterface->update( *m_mixerBuffers, outputSignal, framesPerBuffer, m_state.m_samplePos );
    }
    // otherwise, ssshhh
    else
    {
        m_mixerBuffers->applySilence();
    }

    // set up the buffer pointers for the final stage of the process
    float* inputs[12]
    {
        m_mixerBuffers->m_workingLR[0],
        m_mixerBuffers->m_workingLR[1],
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence,
        m_mixerBuffers->m_silence
    };
    float* outputs[12]
    {
        m_mixerBuffers->m_finalOutputLR[0],
        m_mixerBuffers->m_finalOutputLR[1],
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff,
        m_mixerBuffers->m_runoff
    };

    // track performance pre-plugin 
    m_state.mark( ExposedState::ExecutionStage::Mixer );

    // if we don't use plugins to process our samples into the output buffer, then we must eventually manually copy them raw; this tracks that
    bool outputsWritten = false;

#if OURO_FEATURE_NST24
#if 1
    if ( m_pluginBypass == false )
    {
        for ( auto pluginInst : m_pluginStack )
        {
            if ( pluginInst &&
                 pluginInst->availableForUse() )
            {
                pluginInst->process( inputs, outputs, framesPerBuffer );
                inputs[0] = outputs[0];
                inputs[1] = outputs[1];
                outputsWritten = true;
            }
        }
    }
#endif 
#endif // OURO_FEATURE_NST24

#if OURO_HAS_CLAP
    {
        const app::AudioPlaybackTimeInfo* playbackTimeInfo = ( m_mixerInterface != nullptr ) ? m_mixerInterface->getPlaybackTimeInfo() : nullptr;

        m_clapProcess.steady_time += static_cast<int64_t>( framesPerBuffer );
        m_clapProcess.frames_count = static_cast<uint32_t>( framesPerBuffer );

        if ( playbackTimeInfo != nullptr )
        {
            m_clapProcessTransport.flags = CLAP_TRANSPORT_HAS_TEMPO 
                                         | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;

            m_clapProcessTransport.tempo        = playbackTimeInfo->tempo;
            m_clapProcessTransport.tsig_num     = static_cast< uint16_t >( playbackTimeInfo->timeSigNumerator );
            m_clapProcessTransport.tsig_denom   = static_cast< uint16_t >( playbackTimeInfo->timeSigDenominator );
        }
        else
        {
            m_clapProcessTransport.flags = 0;
        }

        if ( m_clapEffectTest != nullptr )
        {
            if ( m_clapEffectTest->m_ready && m_clapEffectTest->m_online != nullptr )
            {
                auto& inputBuffers  = m_clapEffectTest->getRuntimeInstance().getInputAudioBuffers();
                for ( std::size_t bI = 0U; bI < inputBuffers.size(); bI++ )
                {
                    inputBuffers[bI].data32 = inputs;
                }
                m_clapProcess.audio_inputs          = &inputBuffers[0];
                m_clapProcess.audio_inputs_count    = static_cast< uint32_t >( inputBuffers.size() );

                auto& outputBuffers = m_clapEffectTest->getRuntimeInstance().getOutputAudioBuffers();
                for ( std::size_t bI = 0U; bI < outputBuffers.size(); bI++ )
                {
                    outputBuffers[bI].data32 = outputs;
                }
                m_clapProcess.audio_outputs         = &outputBuffers[0];
                m_clapProcess.audio_outputs_count   = static_cast< uint32_t >( outputBuffers.size() );


                plug::online::Processing processing( m_clapEffectTest->m_online );

                int32_t processingResult = processing( m_clapProcess );

                inputs[0] = outputs[0];
                inputs[1] = outputs[1];
                outputsWritten = true;
            }
        }

        ProcessClapEventsOnMixThread();
    }
#endif // OURO_HAS_CLAP

    m_state.mark( ExposedState::ExecutionStage::Plugins );

    // by default we'll assume the results are still in the working buffers
    float* resultChannelLeft  = inputs[0];
    float* resultChannelRight = inputs[1];

    // .. but if plugins ran, we will use the resulting output buffers
    if ( outputsWritten )
    {
        resultChannelLeft  = outputs[0];
        resultChannelRight = outputs[1];
    }

    m_scope->append( resultChannelLeft, resultChannelRight, framesPerBuffer );

    m_state.mark( ExposedState::ExecutionStage::Scope );

    {
        float* out = (float*)outputBuffer;
        for ( auto i = 0U; i < framesPerBuffer; i++ )
        {
            out[0] = resultChannelLeft[i];
            out[1] = resultChannelRight[i];
            out += 2;
        }
    }

    // update state block
    {
        uint32_t fpb32 = (uint32_t)framesPerBuffer;
        m_state.m_maxBufferFillSize = std::max( m_state.m_maxBufferFillSize, fpb32 );
        m_state.m_minBufferFillSize = std::min( m_state.m_minBufferFillSize, fpb32 );
        m_state.m_samplePos += fpb32;
    }

    m_state.mark( ExposedState::ExecutionStage::Interleave );

    for ( auto& ssp : m_sampleProcessorsInstalled )
    {
        ssp->appendSamples( resultChannelLeft, resultChannelRight, framesPerBuffer );
    }

    m_state.mark( ExposedState::ExecutionStage::SampleProcessing );

    // mute only the final output if requested - preserve all the processed work and WAV-output
    if ( m_mute )
        memset( outputBuffer, 0, sizeof( float ) * framesPerBuffer * 2 );

    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
bool Audio::beginRecording( const fs::path& outputPath, const std::string& filePrefix )
{
    ABSL_ASSERT( !isRecording() );

    auto outputFilename = fs::absolute( outputPath / fmt::format( "{}_finalmix.flac", filePrefix ) ).string();
    blog::core( "Opening FLAC final-mix output '{}'", outputFilename );

    ABSL_ASSERT( m_currentRecorderProcessor == nullptr );
    m_currentRecorderProcessor = ssp::FLACWriter::Create( outputFilename, getSampleRate(), 1.0f );
    if ( m_currentRecorderProcessor )
    {
        attachSampleProcessor( m_currentRecorderProcessor );
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
void Audio::stopRecording()
{
    ABSL_ASSERT( isRecording() );
    blockUntil( 
        detachSampleProcessor( m_currentRecorderProcessor->getInstanceID() ) );
    m_currentRecorderProcessor.reset();
}

// ---------------------------------------------------------------------------------------------------------------------
bool Audio::isRecording() const
{
    return ( m_currentRecorderProcessor != nullptr );
}

// ---------------------------------------------------------------------------------------------------------------------
uint64_t Audio::getRecordingDataUsage() const
{
    if ( isRecording() )
        return m_currentRecorderProcessor->getStorageUsageInBytes();

    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::toggleEffectBypass()
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_mixThreadCommandQueue.emplace( MixThreadCommand::TogglePluginBypass );
    return AsyncCommandCounter{ commandCounter };
}

// ---------------------------------------------------------------------------------------------------------------------
bool Audio::isEffectBypassEnabled() const
{
#if OURO_FEATURE_NST24
    return m_pluginBypass;
#else
    return false;
#endif 
}

// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::effectAppend( nst::Instance* nst )
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_mixThreadCommandQueue.emplace( MixThreadCommand::InstallPlugin, nst );
    return AsyncCommandCounter{ commandCounter };
}

// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::effectClearAll()
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_mixThreadCommandQueue.emplace( MixThreadCommand::ClearAllPlugins );
    return AsyncCommandCounter{ commandCounter };
}

// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::installMixer( MixerInterface* mixIf )
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_mixThreadCommandQueue.emplace( MixThreadCommand::SetMixerFunction, mixIf );
    return AsyncCommandCounter{ commandCounter };
}


// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::attachSampleProcessor( ssp::SampleStreamProcessorInstance sspInstance )
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_sampleProcessorsToInstall.enqueue( sspInstance );
    m_mixThreadCommandQueue.emplace( MixThreadCommand::AttachSampleProcessor, commandCounter );
    return AsyncCommandCounter{ commandCounter };
}

// ---------------------------------------------------------------------------------------------------------------------
AsyncCommandCounter Audio::detachSampleProcessor( ssp::StreamProcessorInstanceID sspID )
{
    const uint32_t commandCounter = m_mixThreadCommandsIssued++;
    m_mixThreadCommandQueue.emplace( MixThreadCommand::DetatchSampleProcessor, sspID.get() );
    return AsyncCommandCounter{ commandCounter };
}

// ---------------------------------------------------------------------------------------------------------------------
void Audio::blockUntil( AsyncCommandCounter counter )
{
    for ( ;; )
    {
        const uint32_t currentCounter = m_mixThreadCommandsComplete.load();
        if ( currentCounter > counter.get() )
            break;

        std::this_thread::yield();
    }
}


#if OURO_HAS_CLAP

// ---------------------------------------------------------------------------------------------------------------------
// Query an extension.
// The returned pointer is owned by the host.
// It is forbidden to call it before plugin->init().
// You can call it within plugin->init() call, and after.
// [thread-safe]
const void* Audio::clapGetExtension( CLAPEffect& clapEffect, const char* extension_id ) noexcept
{
    blog::debug::plug( FMTX( "[CLAP:{}] clapGetExtension({})" ), clapEffect.m_displayName, extension_id );

    if ( !std::strcmp( extension_id, CLAP_EXT_LOG ) )
        return &m_clapHostLog;
    if ( !std::strcmp( extension_id, CLAP_EXT_AUDIO_PORTS ) )
        return &m_clapAudioPorts;
    if ( !std::strcmp( extension_id, CLAP_EXT_PARAMS ) )
        return &m_clapHostParams;
    if ( !std::strcmp( extension_id, CLAP_EXT_STATE ) )
        return &m_clapHostState;
    if ( !std::strcmp( extension_id, CLAP_EXT_GUI ) )
        return &m_clapHostGui;
    if ( !std::strcmp( extension_id, CLAP_EXT_THREAD_CHECK ) )
        return &m_clapHostThreadCheck;

    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Request the host to deactivate and then reactivate the plugin.
// The operation may be delayed by the host.
// [thread-safe]
void Audio::clapRequestRestart( CLAPEffect& clapEffect ) noexcept
{
}

// ---------------------------------------------------------------------------------------------------------------------
// Request the host to activate and start processing the plugin.
// This is useful if you have external IO and need to wake up the plugin from "sleep".
// [thread-safe]
void Audio::clapRequestProcess( CLAPEffect& clapEffect ) noexcept
{
}

// ---------------------------------------------------------------------------------------------------------------------
// Request the host to schedule a call to plugin->on_main_thread(plugin) on the main thread.
// [thread-safe]
void Audio::clapRequestCallback( CLAPEffect& clapEffect ) noexcept
{
}

#endif // OURO_HAS_CLAP

} // namespace module

// ---------------------------------------------------------------------------------------------------------------------
void AudioDeviceQuery::findSuitable( const config::Audio& audioConfig )
{
    m_deviceNames.clear();
    m_deviceNamesUnpacked.clear();
    m_deviceLatencies.clear();
    m_devicePaIndex.clear();

    const auto paDeviceCount = Pa_GetDeviceCount();
    if ( paDeviceCount <= 0 )
        return;

    absl::flat_hash_set< std::string > uniqueDeviceNames;
    uniqueDeviceNames.reserve( paDeviceCount );

    PaStreamParameters outputParameters;
    memset( &outputParameters, 0, sizeof( outputParameters ) );
    outputParameters.channelCount              = 2;
    outputParameters.sampleFormat              = paFloat32;

    PaHostApiInfo fallbackApiInfo;
    fallbackApiInfo.name = "[unknown-api]";

    const PaDeviceInfo* paDeviceInfo;
    const PaHostApiInfo* paApiInfo;
    for ( auto paDi = 0; paDi < paDeviceCount; paDi++ )
    {
        try
        {
            paDeviceInfo = Pa_GetDeviceInfo( paDi );
            if ( paDeviceInfo == nullptr )
            {
                blog::error::core( "Pa_GetDeviceInfo failed #{}", paDi );
                continue;
            }

            paApiInfo = Pa_GetHostApiInfo( paDeviceInfo->hostApi );
            if ( paApiInfo == nullptr )
                paApiInfo = &fallbackApiInfo;

            outputParameters.device = paDi;
            outputParameters.suggestedLatency = audioConfig.lowLatency ? paDeviceInfo->defaultLowOutputLatency :
                paDeviceInfo->defaultHighOutputLatency;

            if ( Pa_IsFormatSupported( nullptr, &outputParameters, audioConfig.sampleRate ) == paFormatIsSupported )
            {
                const auto paDeviceNameVerbose = fmt::format( FMTX( "[{:>22}] {}" ), paApiInfo->name, paDeviceInfo->name );

                if ( uniqueDeviceNames.contains( paDeviceNameVerbose ) == false )
                {
                    uniqueDeviceNames.emplace( paDeviceNameVerbose );

                    m_deviceNames.emplace_back( std::move( paDeviceNameVerbose ) );
                    m_deviceLatencies.push_back( outputParameters.suggestedLatency );
                    m_devicePaIndex.push_back( paDi );
                }
            }
        }
        catch (...)
        {
            blog::error::core( "findSuitable device #{} threw exception", paDi );
        }
    }

    for ( const auto& eachName : m_deviceNames )
        m_deviceNamesUnpacked.emplace_back( eachName.c_str() );
}

// ---------------------------------------------------------------------------------------------------------------------
int32_t AudioDeviceQuery::generateStreamParametersFromDeviceConfig( const config::Audio& audioConfig, PaStreamParameters& streamParams )
{
    memset( &streamParams, 0, sizeof( PaStreamParameters ) );
    streamParams.channelCount              = 2;
    streamParams.sampleFormat              = paFloat32;

    const auto paDeviceCount = Pa_GetDeviceCount();
    if ( paDeviceCount <= 0 )
        return -1;

    PaHostApiInfo fallbackApiInfo;
    fallbackApiInfo.name = "[unknown-api]";

    const PaDeviceInfo* paDeviceInfo;
    const PaHostApiInfo* paApiInfo;
    for ( auto paDi = 0; paDi < paDeviceCount; paDi++ )
    {
        try
        {
            paDeviceInfo = Pa_GetDeviceInfo( paDi );
            if ( paDeviceInfo == nullptr )
            {
                blog::error::core( "Pa_GetDeviceInfo failed #{}", paDi );
                continue;
            }

            paApiInfo = Pa_GetHostApiInfo( paDeviceInfo->hostApi );
            if ( paApiInfo == nullptr )
                paApiInfo = &fallbackApiInfo;

            const auto paDeviceNameVerbose = fmt::format( FMTX( "[{:>22}] {}" ), paApiInfo->name, paDeviceInfo->name );

            if ( paDeviceNameVerbose == audioConfig.lastDevice )
            {
                streamParams.device                    = paDi;
                streamParams.suggestedLatency          = audioConfig.lowLatency ? paDeviceInfo->defaultLowOutputLatency :
                                                                                  paDeviceInfo->defaultHighOutputLatency;

                if ( Pa_IsFormatSupported( nullptr, &streamParams, audioConfig.sampleRate ) == paFormatIsSupported )
                {
                    blog::core( "Selecting device #{} [{}]", paDi, paDeviceNameVerbose );

                    return paDi;
                }
                else
                {
                    return -1;
                }
            }
        }
        catch (...)
        {
            blog::error::core( "configuration device #{} threw exception", paDi );
        }
    }
    return -1;
}

} // namespace app
