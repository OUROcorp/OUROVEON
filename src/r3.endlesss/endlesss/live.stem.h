//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#pragma once

#include "endlesss/api.h"
#include "core.types.h"
#include "base/float.util.h"
#include "dsp/octave.h"

struct PFFFT_Setup;

// this can be enabled and a BP stuck in the function to catch whenever a stem fails to decode - I used this to rapidly
// test decoding at scale by enqueuing an entire jam and letting it blitz through stem loading in a Release build
// (as debug builds are very slow to decode). Any decode issues will be captured via the hasFailed() function later
#ifdef OURO_STRESS_DEBUG_STEM_DECODE
#pragma optimize("", off)
inline void _release_mode_breakpoint()
{
    int breakpointMe = 1;
}
#pragma optimize("", on)
#endif // OURO_STRESS_DEBUG_STEM_DECODE

namespace config { namespace endlesss { struct rAPI; } }

namespace endlesss {
namespace live {

// ---------------------------------------------------------------------------------------------------------------------
// upon asynchronous load, an analysis pass is run against each stem, producing data for use by visualisation / debugging etc
//
struct StemAnalysisData
{
    // avoid accidental copying of this data, it's bulky
    DECLARE_NO_COPY_NO_MOVE( StemAnalysisData );

    StemAnalysisData() = default;


    static constexpr std::size_t    BeatBitsShift = 6;      // shift left/right by 64

    ouro_nodiscard inline std::size_t estimateMemoryUsageBytes() const
    {
        std::size_t result = 0;
        result += m_psaWave.size()      * sizeof( uint8_t );
        result += m_psaBeat.size()      * sizeof( uint8_t );
        result += m_psaLowFreq.size()   * sizeof( uint8_t );
        result += m_psaHighFreq.size()  * sizeof( uint8_t );
        result += m_beatBitfield.size() * sizeof( uint64_t );

        return result;
    }

    void resize( int32_t sampleCount )
    {
        m_psaWave.resize( sampleCount );
        m_psaBeat.resize( sampleCount );
        m_psaLowFreq.resize( sampleCount );
        m_psaHighFreq.resize( sampleCount );
        
        m_beatBitfield.resize( (sampleCount >> BeatBitsShift) + 1 );
    }


    ouro_nodiscard inline uint8_t getWaveU8( const int64_t sampleIndex ) const { return m_psaWave[sampleIndex]; }
    ouro_nodiscard inline float   getWaveF(  const int64_t sampleIndex ) const { return base::LUT::u8_to_float[ getWaveU8(sampleIndex) ]; }

    ouro_nodiscard inline uint8_t getBeatU8( const int64_t sampleIndex ) const { return m_psaBeat[sampleIndex]; }
    ouro_nodiscard inline float   getBeatF(  const int64_t sampleIndex ) const { return base::LUT::u8_to_float[ getBeatU8(sampleIndex) ]; }

    ouro_nodiscard inline uint8_t getLowFreqU8( const int64_t sampleIndex ) const { return m_psaLowFreq[sampleIndex]; }
    ouro_nodiscard inline float   getLowFreqF(  const int64_t sampleIndex ) const { return base::LUT::u8_to_float[ getLowFreqU8(sampleIndex) ]; }

    ouro_nodiscard inline uint8_t getHighFreqU8( const int64_t sampleIndex ) const { return m_psaHighFreq[sampleIndex]; }
    ouro_nodiscard inline float   getHighFreqF(  const int64_t sampleIndex ) const { return base::LUT::u8_to_float[ getHighFreqU8(sampleIndex) ]; }



    inline void setBeatAtSample( const int64_t sampleIndex )
    {
        const uint64_t bitBlock = sampleIndex >> BeatBitsShift;
        const uint64_t bitBit = sampleIndex - (bitBlock << BeatBitsShift);

        m_beatBitfield[bitBlock] |= 1ULL << bitBit;
    }

    ouro_nodiscard inline bool queryBeatAtSample( const int64_t sampleIndex ) const
    {
        const uint64_t bitBlock = sampleIndex >> BeatBitsShift;
        const uint64_t bitBit = sampleIndex - (bitBlock << BeatBitsShift);

        return (m_beatBitfield[bitBlock] >> bitBit) != 0;
    }


    // all the per-sample 0..1 analysis data is stored quantised as mostly we're using it for visualisation
    // or debugging / alignment, full precision generally isn't required. `psa` being per-sample average, just for a name for it
    //
    std::vector< uint8_t >          m_psaWave;         // rms-follower of original waveform
    std::vector< uint8_t >          m_psaBeat;         // peak-follower on detected beats, giving smooth decay off each
    std::vector< uint8_t >          m_psaLowFreq;      // smoothed extraction of lower-band frequencies
    std::vector< uint8_t >          m_psaHighFreq;     // smoothed extraction of higher-band frequencies

    // one bit per sample bitfield, talk to it via functions above
    std::vector< uint64_t >         m_beatBitfield;
};

// ---------------------------------------------------------------------------------------------------------------------
struct Stem
{
    // compression format decoded on load
    enum class Compression
    {
        Unknown,                    // should only be in this state if State != Complete
        OggVorbis,
        FLAC
    };

    enum class State
    {
        Empty,                      // nothing scheduled
        WorkEnqueued,               // background work happening to load/download/decompress/etc
        Complete,                   // audio ready to go

        // failure states:
        Failed_Http,                // network issues
        Failed_DataUnderflow,       // not enough data, compared to what the servers promised us
        Failed_DataOverflow,        // too much data
        Failed_Decompression,       // failed to decompress what we got
        Failed_CacheDirectory,      // failed to create or interact with the stem cache
    };

    enum class AnalysisState
    {
        InProgress,                 // data is being processed in the background
        AnalysisValid,              // analysis complete, valid data is available
        AnalysisEmpty               // analysis complete, could not complete process (not enough samples, etc)
    };

    // data used during stem processing; we keep a single one of these as a const singleton, shared between
    // all stem processing (so it includes no working buffers, etc)
    struct Processing
    {
        using UPtr = std::unique_ptr<Processing>;
        using FFTOctaves = dsp::FFTOctaveBuckets< 3 >;      // bucketing used during frequency collection

        ~Processing();

        PFFFT_Setup*    m_pffftPlan;                        // pffft setup, valid to share between simultaneous workers
        int32_t         m_fftWindowSize;
        float           m_sampleRateF;

        FFTOctaves      m_octaves;

        // tuning values used during processing for various filters
        // these have been settled upon after some browsing through jams with the Stem Analysis UI and trying to 
        // search for values that work across a wide range of audio
        struct Tuning
        {
            float       m_beatFollowDuration = 0.350f;      // decay duration on beat peak-following
            float       m_waveFollowDuration = 0.040f;      // follower duration for main waveform signal

            float       m_trackerSensitivity = 0.980f;      // peak-tracker settings for beat detection
            float       m_trackerHysteresis  = 0.100f;

        }               m_tuning;
    };

    static Processing::UPtr createStemProcessing( const uint32_t targetSampleRate );


    Stem( const types::Stem& stemData, const uint32_t targetSampleRate );
    ~Stem();


    // instigate a fetch of the stem data from either the cache or the network
    // note this is a blocking call and is designed to be called from a background thread in most cases
    void fetch( const api::NetConfiguration& ncfg, const fs::path& cachePath );

    // run analysis pass, producing things like onsets / peak-following / etc into the given result;
    // this result is passed as an argument so that we can also run this in debug tools to tune the processing
    bool analyse( const Processing& processing, StemAnalysisData& result ) const;
    bool analyse( const Processing& processing );   // convenience function that calls the above on current instance, also then toggling m_hasValidAnalysis


    // stem needs a copy of the analysis task future to ensure that in the unlikely case
    // of destruction arriving before the task is done, we wait to avoid the analysis working with a deleted object
    inline void keepFuture( std::shared_future<void>& analysisFuture )
    {
        ABSL_ASSERT( !m_analysisFuture.valid() );
        m_analysisFuture = analysisFuture;
    }

    ouro_nodiscard constexpr bool hasFailed() const
    {
        const bool failure = ( 
                 m_state == State::Failed_Http           ||
                 m_state == State::Failed_DataUnderflow  ||
                 m_state == State::Failed_DataOverflow   ||
                 m_state == State::Failed_Decompression  ||
                 m_state == State::Failed_CacheDirectory );

// see top of file for explanation
#ifdef OURO_STRESS_DEBUG_STEM_DECODE
        if ( failure )
            _release_mode_breakpoint();
#endif // OURO_STRESS_DEBUG_STEM_DECODE

        return failure;
    }

    // NB this value will be 0 in all cases where we have no useful http status to cache, only useful if the state is Failed_Http
    ouro_nodiscard constexpr uint32_t httpFailureStatus() const
    {
        return m_stateHttpStatus;
    }

    ouro_nodiscard constexpr Compression getCompressionFormat() const
    {
        return m_compressionFormat;
    }

    ouro_nodiscard inline std::size_t estimateMemoryUsageBytes() const
    {
        std::size_t result = sizeof( Stem );

        if ( m_state != State::Complete )
            return result;

        // buffer data
        result += ( static_cast<std::size_t>(m_sampleCount) * 2 ) * sizeof( float );

        // add analysis chunk if it is ready
        if ( getAnalysisState() == AnalysisState::AnalysisValid )
        {
            result += m_analysisData.estimateMemoryUsageBytes();
        }
        return result;
    }

    ouro_nodiscard inline AnalysisState getAnalysisState() const
    {
        return m_analysisState;
    }

    ouro_nodiscard constexpr const StemAnalysisData& getAnalysisData() const { return m_analysisData; }
    ouro_nodiscard constexpr StemAnalysisData& getAnalysisData() { return m_analysisData; }

private:

    struct RawAudioMemory
    {
        RawAudioMemory( size_t size );
        ~RawAudioMemory();

        void allocate( size_t newSize );

        size_t      m_rawLength;
        size_t      m_rawReceived;
        uint8_t*    m_rawAudio;
    };

    // make an attempt to download the stem from the Endlesss CDN; this may fail and that may be because the CDN
    // hasn't actually got the data yet - so we can call this function repeatedly to see if success is possible 
    // after a little delay
    // returns false if something broke; sets the m_state appropriately in that case
    ouro_nodiscard bool attemptRemoteFetch( const api::NetConfiguration& ncfg, const uint32_t attemptUID, RawAudioMemory& audioMemory );

    // blend a small window of samples at each end of the stem to reduce clicks on looping
    // (as best we can tell Endlesss also does something like this)
    void applyLoopSewingBlend();



    std::shared_future<void>        m_analysisFuture;
    std::atomic< AnalysisState >    m_analysisState; // set in async analysis if analysis data is to be trusted

    Compression                     m_compressionFormat = Compression::Unknown;

    // #TODO move into accessors
public:
    const types::Stem               m_data;
    ImU32                           m_colourU32;            // converted from m_data and cached
    State                           m_state;
    uint32_t                        m_stateHttpStatus = 0;  // saves the HTTP status if m_state is Failed_Http and it makes any sense to save it; 0 otherwise

    uint32_t                        m_sampleRate;
    int32_t                         m_sampleCount;
    std::array<float*, 2>           m_channel;

private:
    StemAnalysisData                m_analysisData;
};

using StemPtr = std::shared_ptr<Stem>;

} // namespace live
} // namespace endlesss
