//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//

#include "pch.h"

#include "mix/preview.h"

#include "base/paging.h"
#include "math/rng.h"

#include "app/core.h"
#include "app/imgui.ext.h"
#include "app/module.frontend.fonts.h"

#include "ssp/ssp.file.flac.h"
#include "ssp/ssp.file.wav.h"

#include "ux/stem.beats.h"

#include "endlesss/core.constants.h"
#include "endlesss/live.stem.h"
#include "endlesss/toolkit.riff.export.h"

#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>

// ---------------------------------------------------------------------------------------------------------------------
#define _PV_VIEW_STATES(_action)    \
      _action(Default)              \
      _action(Tuning)

DEFINE_PAGE_MANAGER( PreviewView, ICON_FA_WAVE_SQUARE " Playback Engine", "preview_mix_playback", _PV_VIEW_STATES );

#undef _PV_VIEW_STATES


namespace mix {

// ---------------------------------------------------------------------------------------------------------------------
struct Preview::AbletonLinkControl
{
    AbletonLinkControl()
        : m_link( 120.0 )
    {
        m_link.enableStartStopSync( true );
    }
    ~AbletonLinkControl()
    {
        m_link.enable( false );
    }

    using LinkHostTime = ableton::link::HostTimeFilter<ableton::link::platform::Clock>;

    void Transaction_StopPlaying()
    {
        // if the current link state is "playing", snag and change the session to stop it
        if ( m_linkIsPlaying )
        {
            auto linkSessionState = m_link.captureAudioSessionState();
            {
                linkSessionState.setIsPlaying(
                    false,
                    m_hostTimeFilter.sampleTimeToHostTime( m_sampleTime ) );

                m_linkIsPlaying = false;
            }
            m_link.commitAudioSessionState( linkSessionState );
        }
    }


    ableton::Link               m_link;
    LinkHostTime                m_hostTimeFilter;
    std::chrono::microseconds   m_outputLatency;
    double                      m_sampleTime = 0;
    int32_t                     m_linkBar = -1;
    bool                        m_linkIsPlaying = false;
};

// ---------------------------------------------------------------------------------------------------------------------
Preview::Preview( const int32_t maxBufferSize, const int32_t sampleRate, const std::chrono::microseconds outputLatency, base::EventBusClient& eventBusClient )
    : RiffMixerBase( maxBufferSize, sampleRate, eventBusClient )
{
    m_txBlendCacheLeft.fill( 0 );
    m_txBlendCacheRight.fill( 0 );
    m_txBlendActiveLeft.fill( 0 );
    m_txBlendActiveRight.fill( 0 );

    const auto blendDelta =  1.0 / (double)txBlendBufferSize;
          auto blendValue = -1.0;

    // precompute the lerp values for each blend sample in the buffer; based on constant-power fade through
    for ( std::size_t bI = 0; bI < txBlendBufferSize; bI++, blendValue += blendDelta * 2.0 )
        m_txBlendInterp[bI] = (float)std::sqrt( 0.5 * (1.0 - blendValue) );

    m_abletonLinkControl = new AbletonLinkControl();
    m_abletonLinkControl->m_outputLatency = outputLatency;
}

// ---------------------------------------------------------------------------------------------------------------------
Preview::~Preview()
{
    delete m_abletonLinkControl;
    m_abletonLinkControl = nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::renderCurrentRiff(
    const uint32_t      outputOffset,
    const uint32_t      samplesToWrite)
{
    if ( samplesToWrite == 0 )
        return;

    stemAmalgamUpdate();

    std::array< float, 8 >                  stemTimeStretch;
    std::array< float, 8 >                  stemGains;
    std::array< bool, 8 >                   stemAnalysed;
    std::array< endlesss::live::Stem*, 8 >  stemPtr;

    const endlesss::live::Riff* currentRiff = m_riffCurrent.get();

    // unzip the riff and stem data into stack-local items
    for ( auto stemI = 0U; stemI < 8; stemI++ )
    {
        stemTimeStretch[stemI]  = currentRiff->m_stemTimeScales[stemI];
        stemGains[stemI]        = currentRiff->m_stemGains[stemI] * m_permutationCurrent.m_layerGainMultiplier[stemI];
        stemPtr[stemI]          = currentRiff->m_stemPtrs[stemI];
        stemAnalysed[stemI]     = ( stemPtr[stemI] != nullptr ) && ( stemPtr[stemI]->getAnalysisState() == endlesss::live::Stem::AnalysisState::AnalysisValid );
    }

    // keep note of where we are mixing in terms of the 0..N sample count of the current riff
    const auto riffLengthInSamples      = currentRiff->m_timingDetails.m_lengthInSamples;

    // ensure the current playback sample position for the riff isn't off the end
    while ( m_riffPlaybackSample >= riffLengthInSamples )
    {
        m_riffPlaybackSample -= riffLengthInSamples;
    }

    const auto riffWrappedSampleStart   = m_riffPlaybackSample;

    for ( auto stemI = 0U; stemI < 8; stemI++ )
    {
        const auto  stemInst = stemPtr[stemI];
        const float stemGain = stemGains[stemI];

        float permGain = m_permutationCurrent.m_layerGainMultiplier[stemI];

        m_txBlendCacheLeft[stemI]  = 0;
        m_txBlendCacheRight[stemI] = 0;

        // any stem problem -> silence
        if ( stemInst == nullptr || 
             stemInst->hasFailed() )
        {
            for ( auto sI = 0U; sI < samplesToWrite; sI++ )
            {
                m_mixChannelLeft[stemI][outputOffset + sI] = 0;
                m_mixChannelRight[stemI][outputOffset + sI] = 0;
            }

            permGain += m_permutationSampleGainDelta[stemI] * samplesToWrite;
            m_permutationCurrent.m_layerGainMultiplier[stemI] = permGain - m_permutationSampleGainDelta[stemI];

            continue;
        }

        // get sample position in context of the riff
        uint64_t riffSample = riffWrappedSampleStart;

        float lastSampleLeft  = 0;
        float lastSampleRight = 0;

        auto& stemAnalysis = stemInst->getAnalysisData();

        for ( auto sI = 0U; sI < samplesToWrite; sI++ )
        {
            const auto sampleCount = stemInst->m_sampleCount;
            uint64_t finalSampleIdx = riffSample;

            int32_t sampleOffset = m_riffPlaybackNudge;

            if (stemTimeStretch[stemI] != 1.0f)
            {
                finalSampleIdx  = (uint64_t)( (double)finalSampleIdx * stemTimeStretch[stemI] );
                sampleOffset    = (int32_t)( (double)sampleOffset * stemTimeStretch[stemI] );
            }
            finalSampleIdx += sampleOffset;
            finalSampleIdx %= sampleCount;

            // contribute data from the stem analysis to amalgamated block of data as we go
            if ( stemAnalysed[stemI] && permGain > 0 )
            {
                const float stemWave = stemAnalysis.getWaveF( finalSampleIdx ) * permGain;
                const float stemBeat = stemAnalysis.getBeatF( finalSampleIdx ) * permGain;
                const float stemLow  = stemAnalysis.getLowFreqF( finalSampleIdx ) * permGain;
                const float stemHigh = stemAnalysis.getHighFreqF( finalSampleIdx ) * permGain;

                m_stemDataAmalgam.m_wave[stemI] = std::max( m_stemDataAmalgam.m_wave[stemI], stemWave );
                m_stemDataAmalgam.m_beat[stemI] = std::max( m_stemDataAmalgam.m_beat[stemI], stemBeat );
                m_stemDataAmalgam.m_low[stemI]  = std::max( m_stemDataAmalgam.m_low[stemI],  stemLow  );
                m_stemDataAmalgam.m_high[stemI] = std::max( m_stemDataAmalgam.m_high[stemI], stemHigh );
            }

            lastSampleLeft  = stemInst->m_channel[0][finalSampleIdx] * stemGain * permGain;
            lastSampleRight = stemInst->m_channel[1][finalSampleIdx] * stemGain * permGain;
            m_mixChannelLeft[stemI][outputOffset + sI]  = lastSampleLeft;
            m_mixChannelRight[stemI][outputOffset + sI] = lastSampleRight;

            riffSample++;
            if ( riffSample >= riffLengthInSamples )
                riffSample -= riffLengthInSamples;

            permGain += m_permutationSampleGainDelta[stemI];
        }

        m_txBlendCacheLeft[stemI]  = lastSampleLeft;
        m_txBlendCacheRight[stemI] = lastSampleRight;

        m_permutationCurrent.m_layerGainMultiplier[stemI] = permGain - m_permutationSampleGainDelta[stemI];
    }

    m_riffPlaybackSample += samplesToWrite;

    m_stemDataAmalgamSamplesUsed += samplesToWrite;
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::applyBlendBuffer( const uint32_t outputOffset, const uint32_t samplesToWrite )
{
    if ( m_txBlendSamplesRemaining <= 0 || samplesToWrite == 0 )
        return;

    // txIntepIndex requires this or it will underflow (and it should never be > the buffer size)
    ABSL_ASSERT( m_txBlendSamplesRemaining <= txBlendBufferSize );

    // work out minimum number of values to walk
    const auto maxSamplesToWrite = std::min( samplesToWrite, m_txBlendSamplesRemaining );

    // find where to read from our blend-amount array, offset how many samples we've already used (in previous update()s)
    const auto txIntepIndex = txBlendBufferSize - m_txBlendSamplesRemaining;

    for ( auto stemI = 0U; stemI < 8; stemI++ )
    {
        for ( auto sI = 0U; sI < maxSamplesToWrite; sI++ )
        {
            const float existingLeft  = m_mixChannelLeft[stemI][outputOffset + sI];
            const float existingRight = m_mixChannelRight[stemI][outputOffset + sI];

            m_mixChannelLeft[stemI][outputOffset + sI]  = std::lerp( existingLeft,  m_txBlendActiveLeft[stemI],  m_txBlendInterp[txIntepIndex + sI] );
            m_mixChannelRight[stemI][outputOffset + sI] = std::lerp( existingRight, m_txBlendActiveRight[stemI], m_txBlendInterp[txIntepIndex + sI] );

#ifdef PRV_DEBUG
            // enable to view transition blocks in audacity or whatnot
            if ( sI == 0 )
                m_mixChannelRight[stemI][outputOffset + sI] = 1.0f;
            if ( sI == maxSamplesToWrite - 1 )
                m_mixChannelRight[stemI][outputOffset + sI] = -1.0f;
#endif
        }
    }

    m_txBlendSamplesRemaining -= maxSamplesToWrite;
    ABSL_ASSERT( m_txBlendSamplesRemaining <= txBlendBufferSize );
}

// ---------------------------------------------------------------------------------------------------------------------
// process commands from the main thread
void Preview::processCommandQueue()
{
    EngineCommandData engineCmd;
    if ( m_commandQueue.try_dequeue( engineCmd ) )
    {
        switch ( engineCmd.getCommand() )
        {
            // start multitrack on the edge of a riff; the actual recording begins 
            // when that is detected during the sample loop below
            case EngineCommand::BeginRecording:
            {
                m_multiTrackRecording = true;
                m_multiTrackInFlux = false;
            }
            break;

            // cleanly disengage recording from the mix thread
            case EngineCommand::StopRecording:
            {
                m_multiTrackRecording = false;
                m_multiTrackInFlux = false;

                // move recorders over to destroy on the main thread, avoid any stalls
                // from whatever may be required to tie off recording
                for ( auto i = 0; i < 8; i++ )
                {
                    ABSL_ASSERT( m_multiTrackOutputsToDestroyOnMainThread[i] == nullptr );

                    m_multiTrackOutputsToDestroyOnMainThread[i] = m_multiTrackOutputs[i];
                    m_multiTrackOutputs[i].reset();
                }
            }
            break;

            default:
            case EngineCommand::Invalid:
                blog::error::mix( "Unknown or invalid command received" );
                break;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::update( 
    const AudioBuffer&  outputBuffer,
    const AudioSignal&  outputSignal,
    const uint32_t      samplesToWrite,
    const uint64_t      /* samplePosition */ )
{
    processCommandQueue();

    // cache the current playback sample value before we do the next batch of operations, used to analyse what might have happened at the end of update()
    const int64_t riffPlaybackSampleAtStartOfUpdate = m_riffPlaybackSample;


    // "drain and stop" flag; set on main thread, this will make the mixer drain off all the mix-side request queues
    // and set the current riff playing to null, leaving silence
    const bool shouldDrainAndStop = m_drainQueueAndStop.load();
    if ( shouldDrainAndStop )
    {
        // drain the current queue, reporting change/complete on each one to ensure any waiting 
        // systems on other threads see that we processed the request to some degree
        RiffPtrOperation riffOperation;
        while ( m_riffQueue.try_dequeue( riffOperation ) )
        {
            m_eventBusClient.Send< ::events::MixerRiffChange >( riffOperation.m_value, true ); // mark "was cancelled" as true to show we didn't actually play it
            m_eventBusClient.Send< ::events::OperationComplete >( riffOperation.m_operation );
        }
        // .. finally, we play nothing
        m_riffCurrent = nullptr;
        
        // notify finally that we are not playing anything
        m_eventBusClient.Send< ::events::MixerRiffChange >( m_riffCurrent );

        // mark stop() operation as finished
        m_drainQueueAndStop = false;
    }

    bool        riffEnqueued = ( m_riffQueue.peek() != nullptr );
    const auto dequeNextRiff = [this, &riffEnqueued]()
    {
        RiffPtrOperation riffOperation;
        if ( !m_riffQueue.try_dequeue( riffOperation ) )
        {
            ABSL_ASSERT( false );
        }

        // brand new riff from a silent state
        if ( m_riffCurrent == nullptr )
        {
            m_lockTransitionBarCounter = m_lockTransitionBarMultiple.load();
        }
        
        m_riffCurrent = riffOperation.m_value;

        m_eventBusClient.Send< ::events::MixerRiffChange >( m_riffCurrent );
        m_eventBusClient.Send< ::events::OperationComplete >( riffOperation.m_operation );

        // update enqueued state now we just removed something from the pile
        riffEnqueued = ( m_riffQueue.peek() != nullptr );


        m_riffTransitionedInMix = true;
    };

    const auto addTxBlend = [this]
    {
        for ( auto stemI = 0U; stemI < 8; stemI++ )
        {
            m_txBlendActiveLeft[stemI]  = m_txBlendCacheLeft[stemI];
            m_txBlendActiveRight[stemI] = m_txBlendCacheRight[stemI];
        }
        m_txBlendSamplesRemaining = txBlendBufferSize;
    };

    const bool riffEmpty = ( m_riffCurrent == nullptr ||
                             m_riffCurrent->m_timingDetails.m_lengthInSamples == 0 );

    updatePermutations( samplesToWrite, riffEmpty ? 1.0 : m_riffCurrent->m_timingDetails.m_lengthInSecPerBar );

    // early out when nothing is happening
    if ( riffEmpty && !riffEnqueued )
    {
        // keep permutations updating even if we early out
        flushPendingPermutations();

        outputBuffer.applySilence();

        // blank out tx cache
        m_txBlendCacheLeft.fill( 0 );
        m_txBlendCacheRight.fill( 0 );

        // reset computed riff playback variables
        m_playbackProgression.reset();

        // force playback cursor back to the start while we have nothing to play; this means that 
        // the first riff enqueued to play after we've been idle will start from scratch rather
        // than just wherever the playback head had wandered onto in the background
        m_riffPlaybackSample = 0;

        // deal with LINK session update with nothing playing
        if ( m_abletonLinkControl )
        {
            m_abletonLinkControl->Transaction_StopPlaying();
            m_abletonLinkControl->m_sampleTime += static_cast<double>(samplesToWrite);
        }

        return;
    }

    uint32_t txOffset = 0;
    uint32_t txSampleLimit = samplesToWrite;

    // instant transition mode; or if we have no current riff, default to just grabbing one instantly
    if ( m_lockTransitionToNextBar == false || riffEmpty )
    {
        flushPendingPermutations();

        // in instant mode, any time a riff is enqueued we pull it for playing immediately
        if ( riffEnqueued )
        {
            dequeNextRiff();
            addTxBlend();

            // in case of a null enqueue (ie. a command to stop playing)
            // revert to silence and bail 
            if ( m_riffCurrent == nullptr )
            {
                mixChannelsWriteSilence( 0, samplesToWrite );
            }
            else
            {
                renderCurrentRiff( 0, samplesToWrite );
            }
        }
        // .. nothing to do, just keep rendering samples
        else
        {
            renderCurrentRiff( 0, samplesToWrite );
        }
    }
    // bar transition mode, more to think about
    else
    {
        auto shiftTransisionSample = m_lockTransitionOnBeat * ( m_riffCurrent->m_timingDetails.m_lengthInSamplesPerBar / m_riffCurrent->m_timingDetails.m_quarterBeats );

        auto segmentLengthInSamples = (int64_t)m_riffCurrent->m_timingDetails.m_lengthInSamplesPerBar;
        switch ( m_lockTransitionBarCount )
        {
            case TransitionBarCount::Eighth:    segmentLengthInSamples /= 8; shiftTransisionSample /= 8; break;
            case TransitionBarCount::Quarter:   segmentLengthInSamples /= 4; shiftTransisionSample /= 4; break;
            case TransitionBarCount::Half:      segmentLengthInSamples /= 2; shiftTransisionSample /= 2; break;
            default:
            case TransitionBarCount::Once:
            case TransitionBarCount::Many:      // loop for m_lockTransitionBarMultiple
                break;
        }

        // work out how many samples we have to render before we hit a transition point
        auto shiftedPlaybackSample = m_riffPlaybackSample - shiftTransisionSample;
        if ( shiftedPlaybackSample < 0 )
        {
            shiftedPlaybackSample += segmentLengthInSamples;
        }
        const auto samplesUntilNextSegment = (uint32_t)(segmentLengthInSamples - (shiftedPlaybackSample % segmentLengthInSamples));

        const bool waitingOnMultiBarCountdown = ( m_lockTransitionBarCount == TransitionBarCount::Many );

        // if there are more samples than we're currently rendering, that means we won't be hitting a transition in this update so just blit out the current riff
        if ( samplesUntilNextSegment > samplesToWrite )
        {
            renderCurrentRiff( 0, samplesToWrite );
        }
        // .. otherwise we will render out the the samples up to the transition point, then do any riff switching or logic that
        //    requires processing on that transition, then continue
        else
        {
            // write out the remaining chunk of riff before we consider what happens at the transition
            if ( samplesUntilNextSegment > 0 )
                renderCurrentRiff( 0, samplesUntilNextSegment );

            // work out how many samples now remain from the transition point to the end of the current output buffer
            const auto samplesRemainingToWrite = samplesToWrite - samplesUntilNextSegment;
            
            bool doRiffTransitionLogic = true;

            // check if we're waiting on N bars, update the countdown
            if ( waitingOnMultiBarCountdown && riffEnqueued )
            {
                --m_lockTransitionBarCounter;

                // still got bars to go
                if ( m_lockTransitionBarCounter > 0 )
                {
                    renderCurrentRiff( samplesUntilNextSegment, samplesRemainingToWrite );
                    doRiffTransitionLogic = false;
                }
            }

            if ( doRiffTransitionLogic )
            {
                if ( riffEnqueued )
                {
                    dequeNextRiff();
                    addTxBlend();
                }

                flushPendingPermutations();

                // in the case where we paused playback, write silence from the current offset
                if ( m_riffCurrent == nullptr )
                {
                    mixChannelsWriteSilence( samplesUntilNextSegment, samplesRemainingToWrite );
                }
                else
                {
                    renderCurrentRiff( samplesUntilNextSegment, samplesRemainingToWrite );
                }

                // set the transition blending to start at this point in the buffer if applyBlendBuffer() will be doing any work
                txOffset = samplesUntilNextSegment;
                txSampleLimit = samplesRemainingToWrite;

                // reset multi-bar countdown on transition
                if ( waitingOnMultiBarCountdown )
                {
                    m_lockTransitionBarCounter = m_lockTransitionBarMultiple.load();
                }
            }
        }
    }


    // if we have some cached transition smoothing values in play, weave them in
    applyBlendBuffer( txOffset, txSampleLimit );


    // compute where we are (roughly) for the UI
    if ( m_riffCurrent )
    {
        const auto& timingData = m_riffCurrent->getTimingDetails();

        // compute timing state when update() began
        endlesss::live::RiffProgression progressionAtStartOfUpdate;
        timingData.ComputeProgressionAtSample(
            riffPlaybackSampleAtStartOfUpdate,
            progressionAtStartOfUpdate );

        timingData.ComputeProgressionAtSample(
            m_riffPlaybackSample,
            m_playbackProgression );

        m_timeInfo.samplePos          = (double)m_riffPlaybackSample;
        m_timeInfo.tempo              = timingData.m_bpm;
        m_timeInfo.timeSigNumerator   = timingData.m_quarterBeats;
        m_timeInfo.timeSigDenominator = 4;


        // LINK logic EXTREMELY WIP HACK
        if ( m_abletonLinkControl )
        {
            const double timingQuantum = static_cast<double>(timingData.m_quarterBeats);

            const auto hostTime = m_abletonLinkControl->m_hostTimeFilter.sampleTimeToHostTime( m_abletonLinkControl->m_sampleTime );
            m_abletonLinkControl->m_sampleTime += static_cast<double>(samplesToWrite);

            const auto bufferBeginAtOutput = hostTime + m_abletonLinkControl->m_outputLatency;

            auto linkSessionState = m_abletonLinkControl->m_link.captureAudioSessionState();

            // always write our tempo. we are the tempo.
            linkSessionState.setTempo( timingData.m_bpm, bufferBeginAtOutput );

            // on the arrival of a new riff when nothing was playing, tag IsPlaying in the session state and force
            // a new 0-beat to match
            if ( !m_abletonLinkControl->m_linkIsPlaying )
            {
                linkSessionState.setIsPlaying( true, bufferBeginAtOutput );
                linkSessionState.forceBeatAtTime( 0, bufferBeginAtOutput, timingQuantum );
                m_abletonLinkControl->m_linkIsPlaying = true;
            }

            if ( m_playbackProgression.m_playbackBarSegment != progressionAtStartOfUpdate.m_playbackBarSegment )
            {
                std::chrono::microseconds zeroBeatUsOffset( std::llround( m_playbackProgression.m_playbackQuarterTimeSec * 1.0e6 ) );

                if ( m_playbackProgression.m_playbackBarSegment == 0 )
                    linkSessionState.forceBeatAtTime( m_playbackProgression.m_playbackBarSegment, bufferBeginAtOutput + zeroBeatUsOffset, timingQuantum );
                else
                    linkSessionState.requestBeatAtTime( m_playbackProgression.m_playbackBarSegment, bufferBeginAtOutput + zeroBeatUsOffset, timingQuantum );

//                 blog::mix( FMTX( "BEAT {} : {}, {} : {} ~ {} : {:08}" ),
//                     m_playbackProgression.m_playbackBarSegment,
//                     progressionAtStartOfUpdate.m_playbackSegmentPercentage,
//                     m_playbackProgression.m_playbackSegmentPercentage,
//                     m_playbackProgression.m_playbackQuarterPercentage,
//                     m_playbackProgression.m_playbackQuarterTimeSec,
//                     zeroBeatUsOffset );
            }

            m_abletonLinkControl->m_link.commitAudioSessionState( linkSessionState );
        }
    }
    else
    {
        // nothing to play, tell LINK about that
        if ( m_abletonLinkControl )
        {
            m_abletonLinkControl->Transaction_StopPlaying();
            m_abletonLinkControl->m_sampleTime += static_cast<double>(samplesToWrite);
        }
    }

    // spool samples out to recorders if running
    if ( m_multiTrackRecording )
    {
        for ( auto i = 0; i < 8; i++ )
        {
            m_multiTrackOutputs[i]->appendSamples( m_mixChannelLeft[i], m_mixChannelRight[i], samplesToWrite );
        }
    }

    mixChannelsToOutput( outputBuffer, outputSignal, samplesToWrite );
}


// =====================================================================================================================


// ---------------------------------------------------------------------------------------------------------------------
ImVec4 GetTransitionColourVec4( const float lerpT )
{
    const auto colour1 = ImGui::GetStyleColorVec4( ImGuiCol_PlotHistogram );
    const auto colour2 = ImGui::GetStyleColorVec4( ImGuiCol_Text );
    return lerpVec4( colour1, colour2, lerpT );
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::imgui()
{
    static PreviewView previewView = PreviewView::Default;

    if ( ImGui::Begin( previewView.generateTitle().c_str(), nullptr, ImGuiWindowFlags_Ouro_MultiDimensional) )
    {
        // stupid hack to allow toggling page on this panel as it lacks a tab bar by default and on mac the pageup 
        // approach is spotty, thanks steve jobs
        if ( ImGui::IsWindowHovered( ImGuiHoveredFlags_RootWindow ) && ImGui::IsMouseDoubleClicked( 1 ) )
            previewView.switchToNextPage();

        previewView.checkForImGuiTabSwitch();

        if ( previewView == PreviewView::Default )
            imguiDefault();
        if ( previewView == PreviewView::Tuning )
            imguiTuning();
    }

    ImGui::End();

    for ( auto layer = 0U; layer < 8; layer++ )
        m_multiTrackOutputsToDestroyOnMainThread[layer].reset();
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::enableAbletonLink( bool bEnabled )
{
    if ( m_abletonLinkControl )
        m_abletonLinkControl->m_link.enable( bEnabled );
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::imguiDefault()
{
    const auto panelVolumeModule = 64.0f;
//    const auto panelNudgeModule = 140.0f;

    const auto panelRegionAvailable = ImGui::GetContentRegionAvail();
    if ( panelRegionAvailable.x < panelVolumeModule )
        return;

    // simple pulse from a riff change in the mixer, pulse our transition bits
    m_riffTransitionedUI += ( 0.0f - m_riffTransitionedUI ) * 0.165f;
    if ( m_riffTransitionedInMix )
    {
        m_riffTransitionedUI = 1.0f;
        m_riffTransitionedInMix = false;
    }

    const auto riffTransitColourU32 = ImGui::GetColorU32( GetTransitionColourVec4( m_riffTransitionedUI ) );

    // for rendering state from the current riff;
    // take a shared ptr copy here, just in case the riff is swapped out mid-tick
    endlesss::live::RiffPtr currentRiffPtr = m_riffCurrent;

    const auto currentRiff  = currentRiffPtr.get();
    bool currentRiffIsValid = ( currentRiff &&
                                currentRiff->m_syncState == endlesss::live::Riff::SyncState::Success );

    {
//         endlesss::toolkit::xp::RiffExportAdjustments riffExportAdjustments;
//         riffExportAdjustments.m_exportSampleOffset = m_riffPlaybackNudge;
// 
//         {
//             ImGui::Columns( 2, "##nudge_edit", false );
//             ImGui::SetColumnWidth( 0, ( panelRegionAvailable.x - panelNudgeModule ) + 16.0f );
//             ImGui::SetColumnWidth( 1, panelNudgeModule );
// 
//             ImGui::ux::RiffDetails( currentRiffPtr, m_eventBusClient, &riffExportAdjustments );
// 
//             ImGui::NextColumn();
//             if ( currentRiffIsValid )
//             {
//                 // you come up with a better name then go on
//                 ImGui::TextUnformatted( "  Wuncle Nudge" );
//                 const auto stepSize = currentRiffPtr->m_timingDetails.m_lengthInSamplesPerBar / 32;
// 
//                 int32_t nudgeEdit = m_riffPlaybackNudge;
//                 ImGui::SetNextItemWidth( panelNudgeModule - 20.0f );
//                 const auto nudgeChanged = ImGui::InputInt(
//                     "##wuncle",
//                     &nudgeEdit,
//                     stepSize,
//                     stepSize * 2,
//                     ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue );
// 
//                 if ( nudgeChanged || ImGui::IsItemDeactivatedAfterEdit() )
//                 {
//                     m_riffPlaybackNudge = nudgeEdit;
//                 }
//             }
// 
//             ImGui::Columns( 1 );
//         }
// 
//         ImGui::Spacing();
//         ImGui::Spacing();

        ImGui::Columns( 2, "##beat_display", false );
        ImGui::SetColumnWidth( 0, panelVolumeModule );
        ImGui::SetColumnWidth( 1, ( panelRegionAvailable.x - panelVolumeModule ) + 16.0f );

        // toggle transition mode; take copy of atomic to update/use in ui
        bool lockTransitionLocal = m_lockTransitionToNextBar.load();
        {
            {
                ImGui::Scoped::ToggleButtonLit highlightButton( lockTransitionLocal, riffTransitColourU32 );
                if ( ImGui::Button( ICON_FA_TIMELINE, ImVec2( 48.0f, 24.0f ) ) )
                {
                    lockTransitionLocal = !lockTransitionLocal;
                    m_lockTransitionToNextBar = lockTransitionLocal;
                }
            }
            ImGui::CompactTooltip( lockTransitionLocal ? "Transition Timing : On Chosen Bar" : "Transition Timing : Instant" );
        }

        ImGui::NextColumn();

        const float progressBarHeight = 5.0f;
        if ( currentRiffIsValid )
        {
            const bool waitingOnMultiBarCountdown = ( m_lockTransitionBarCount == TransitionBarCount::Many );
            const int32_t remainingBarCounter = waitingOnMultiBarCountdown ? ( m_playbackProgression.m_playbackBar + m_lockTransitionBarCounter ) : -1;

            ImGui::ProgressBar( (float)m_playbackProgression.m_playbackPercentage, ImVec2( -1, progressBarHeight ), "" );
            ImGui::BeatSegments( "##bars_play", currentRiff->m_timingDetails.m_barCount, m_playbackProgression.m_playbackBar, remainingBarCounter, progressBarHeight, riffTransitColourU32 );
            ImGui::BeatSegments( "##beats", currentRiff->m_timingDetails.m_quarterBeats, m_playbackProgression.m_playbackBarSegment );
        }
        else
        {
            ImGui::ProgressBar( 0, ImVec2( -1, progressBarHeight ), "" );
            ImGui::BeatSegments( "##bars_play", 1, -1, -1, progressBarHeight, riffTransitColourU32 );
            ImGui::BeatSegments( "##beats", 1, -1 );
        }

        {
            ImGui::BeginDisabledControls( !lockTransitionLocal );

            ImGui::NextColumn();
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::TextUnformatted( "Offset" );

            ImGui::NextColumn();
            ImGui::Spacing();
            {
                if ( currentRiffIsValid )
                {
                    float barRateScale = 1.0f;
                    float barBlockArea = ImGui::GetContentRegionAvail().x;
                    switch ( m_lockTransitionBarCount )
                    {
                        case TransitionBarCount::Eighth:    barRateScale = 8.0f; break;
                        case TransitionBarCount::Quarter:   barRateScale = 4.0f; break;
                        case TransitionBarCount::Half:      barRateScale = 2.0f; break;
                        default:
                            break;
                    }

                    const float barButtonWidth  = 12.0f;
                    const float barBlockWidth   = ( barBlockArea - barButtonWidth ) / (float)currentRiff->m_timingDetails.m_quarterBeats;
                    const float barSpacerWidth  = ( barBlockWidth - barButtonWidth );

                    const auto barButtonDim = ImVec2( barButtonWidth, ImGui::GetTextLineHeight() );

                    // an index for the transition point, scaled by the bar count so we can blink the buttons below
                    // in sync with when the mixer will transition; helps visualise the right point to hop track while you listen
                    int32_t subBarBlink = (int32_t)std::floor( m_playbackProgression.m_playbackQuarterPercentage * barRateScale );
                    subBarBlink %= currentRiff->m_timingDetails.m_quarterBeats;

                    for ( auto qb = 0; qb < currentRiff->m_timingDetails.m_quarterBeats; qb++ )
                    {
                        if ( qb > 0 )
                            ImGui::SameLine( 0, barSpacerWidth );

                        // add a little bump to the button to indicate the current transition preview point
                        ImVec2 localButtomDim = barButtonDim;
                        if ( subBarBlink == qb )
                            localButtomDim.y += 7.0f;

                        ImGui::PushID( qb );
                        ImGui::Scoped::ToggleButtonLit lit( m_lockTransitionOnBeat == qb, riffTransitColourU32 );
                        if ( ImGui::Button( "##btr", localButtomDim ) )
                        {
                            m_lockTransitionOnBeat = qb;
                        }
                        ImGui::PopID();
                    }
                    {
                        ImGui::SameLine( 0, barSpacerWidth );
                        ImGui::BeginDisabledControls( true );
                        ImGui::Button( "##endpt", barButtonDim );
                        ImGui::EndDisabledControls( true );
                    }
                }
                else
                {
                    ImGui::Dummy( ImVec2( 20.0f, ImGui::GetTextLineHeight() + 7.0f ) );
                }

                ImGui::NextColumn();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( "Repeat" );

                ImGui::NextColumn();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::ItemSize( ImVec2( 1.0f, 2.0f ) );

                const auto repeatSliderWidth = ImGui::GetContentRegionAvail().x * 0.3f;
                const auto buttonBarAreaWidth = ImGui::GetContentRegionAvail().x - repeatSliderWidth;

                const auto buttonGap   = 4.0f;
                const auto buttonCount = (float)TransitionBarCount::Count;
                const auto buttonSize  = ImVec2( (buttonBarAreaWidth - (buttonGap * (buttonCount - 1.0f) ) ) / buttonCount, ImGui::GetTextLineHeight() * 1.35f );

                META_FOREACH( TransitionBarCount, lb )
                {
                    if ( lb != TransitionBarCount::Eighth )
                        ImGui::SameLine( 0, buttonGap );

                    ImGui::Scoped::ToggleButtonLit lit( m_lockTransitionBarCount == lb, riffTransitColourU32 );
                    if ( ImGui::Button( TransitionBarCount::toString( lb ), buttonSize ) )
                    {
                        m_lockTransitionBarCount = lb;
                        m_lockTransitionBarCounter = m_lockTransitionBarMultiple.load();
                    }
                }
                ImGui::SameLine( 0, buttonGap );
                {
                    ImGui::SetNextItemWidth( repeatSliderWidth );

                    const bool disableMultipleSlider = (m_lockTransitionBarCount != TransitionBarCount::Many);
                    ImGui::BeginDisabledControls( disableMultipleSlider );
                    int32_t localMultipleValue = m_lockTransitionBarMultiple;
                    if ( ImGui::SliderInt( "##mlti", &localMultipleValue, 2, 12, "%d", ImGuiSliderFlags_AlwaysClamp ) )
                    {
                        m_lockTransitionBarMultiple = localMultipleValue;
                        m_lockTransitionBarCounter  = localMultipleValue;  // reset current countdown on change
                    }
                    ImGui::EndDisabledControls( disableMultipleSlider );
                }
            }
            ImGui::EndDisabledControls( !lockTransitionLocal );
        }

        ImGui::Columns( 1 );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::imguiTuning()
{
    ImGui::TextUnformatted( ICON_FA_STOPWATCH " Mute/Solo Blend Speed" );
    ImGui::Spacing();

    const auto buttonGap = 4.0f;
    const auto buttonSize = ImVec2( 100.0f, ImGui::GetTextLineHeight() * 1.25f );

    META_FOREACH( PermutationChangeRate, pcr )
    {
        if ( pcr != PermutationChangeRate::Instant )
            ImGui::SameLine( 0, buttonGap );

        ImGui::Scoped::ToggleButton lit( m_permutationChangeRate == pcr );
        if ( ImGui::Button( PermutationChangeRate::toString( pcr ), buttonSize ) )
        {
            m_permutationChangeRate = pcr;
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted( ICON_FA_BARS " 8-Track Recording Format" );
    ImGui::Spacing();

    // although it wouldn't actually do anything to switch while active but disable changing output format while recording to show that fact
    const bool disableFormatSwitching = ( m_multiTrackInFlux == true || m_multiTrackRecording == true );

    META_FOREACH( MultiTrackOutputFormat, mto )
    {
        if ( mto != MultiTrackOutputFormat::WAV )
            ImGui::SameLine( 0, buttonGap );

        ImGui::BeginDisabledControls( disableFormatSwitching );
        ImGui::Scoped::ToggleButton lit( m_multiTrackOutputFormat == mto );
        if ( ImGui::Button( MultiTrackOutputFormat::toString( mto ), buttonSize ) )
        {
            m_multiTrackOutputFormat = mto;
        }
        ImGui::EndDisabledControls( disableFormatSwitching );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
bool Preview::beginRecording( const fs::path& outputPath, const std::string& filePrefix )
{
    // should not be calling this if we're already in the process of streaming out
    ABSL_ASSERT( !isRecording() );
    if ( isRecording() )
        return false;

    // randomise the write buffer sizes to avoid all outputs flushing outputs simultaneously
    math::RNG32 writeBufferShuffleRNG;

    // set up 8 output streams, one for each Endlesss layer
    for ( auto i = 0; i < 8; i++ )
    {
        if ( m_multiTrackOutputFormat == MultiTrackOutputFormat::WAV )
        {
            auto recordFile = outputPath / fmt::format( "{}_layer-{}.wav", filePrefix, i + 1 );
            m_multiTrackOutputs[i] = ssp::WAVWriter::Create(
                recordFile.string(),
                m_audioSampleRate,
                writeBufferShuffleRNG.genInt32( 5, 15 ) );
        }
        else if ( m_multiTrackOutputFormat == MultiTrackOutputFormat::FLAC )
        {
            auto recordFile = outputPath / fmt::format( "{}_layer-{}.flac", filePrefix, i + 1 );
            m_multiTrackOutputs[i] = ssp::FLACWriter::Create(
                recordFile.string(),
                m_audioSampleRate,
                writeBufferShuffleRNG.genFloat( 0.75f, 1.75f ) );
        }
        else
        {
            ABSL_ASSERT( false );
        }
    }

    // tell the worker thread to begin writing to our streams
    m_commandQueue.enqueue( EngineCommand::BeginRecording );
    m_multiTrackInFlux = true;

    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
void Preview::stopRecording()
{
    // can't stop what hasn't started
    ABSL_ASSERT( isRecording() );
    if ( !isRecording() )
        return;

    m_commandQueue.enqueue( EngineCommand::StopRecording );
    m_multiTrackInFlux = true;
}

// ---------------------------------------------------------------------------------------------------------------------
bool Preview::isRecording() const
{
    return m_multiTrackRecording ||
           m_multiTrackInFlux;
}

// ---------------------------------------------------------------------------------------------------------------------
uint64_t Preview::getRecordingDataUsage() const
{
    if ( !isRecording() )
        return 0;

    uint64_t usage = 0;
    for ( auto i = 0; i < 8; i++ )
        usage += m_multiTrackOutputs[i]->getStorageUsageInBytes();

    return usage;
}

} // namespace mix
