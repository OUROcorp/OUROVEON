//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#include "pch.h"

#include "app/module.frontend.h"
#include "base/text.h"
#include "base/instrumentation.h"
#include "base/utils.h"

#include "dsp/fft.util.h"
#include "dsp/octave.h"
#include "endlesss/live.stem.h"
#include "filesys/fsutil.h"
#include "math/rng.h"
#include "spacetime/moment.h"
#include "config/spectrum.h"

// vorbis decode
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

// foxen flac
#include <foxen/flac.h>

// fft
#include "pffft.h"

// r8brain
#include "CDSPResampler.h"

// q
#include <q/fx/schmitt_trigger.hpp>
#include <q/fx/signal_conditioner.hpp>
#include <q/fx/differentiator.hpp>
#include <q/fx/envelope.hpp>
#include <q/fx/peak.hpp>


namespace endlesss {
namespace live {

// ---------------------------------------------------------------------------------------------------------------------
Stem::Processing::~Processing()
{
    if ( m_pffftPlan != nullptr )
    {
        pffft_destroy_setup( m_pffftPlan );
        m_pffftPlan = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
Stem::Processing::UPtr Stem::createStemProcessing( const uint32_t targetSampleRate )
{
    static constexpr float measurementLengthSeconds = 1.0f / 60.0f;

    // work out an FFT size that is about the size required to sample at the requested measurement length
    const uint32_t samplesPerMeasurement = static_cast<uint32_t>( measurementLengthSeconds * static_cast<float>(targetSampleRate) );


    Processing::UPtr result = std::make_unique<Processing>();
    result->m_fftWindowSize = base::nextPow2( samplesPerMeasurement );
    result->m_sampleRateF   = static_cast<float>( targetSampleRate );
    result->m_pffftPlan     = pffft_new_setup( result->m_fftWindowSize, PFFFT_REAL );

    result->m_octaves.configure(
        { 3, 4, 10 },
        targetSampleRate,
        result->m_fftWindowSize );

    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
Stem::Stem( const types::Stem& stemData, const uint32_t targetSampleRate )
    : m_data( stemData )
    , m_state( State::Empty )
    , m_sampleRate( targetSampleRate )
    , m_sampleCount( 0 )
    , m_analysisState( AnalysisState::InProgress )
{
    m_channel.fill( nullptr );

    m_colourU32 = ImGui::ParseHexColour( m_data.colour.c_str() );

    blog::stem( FMTX( "[s:{}] allocated" ), m_data.couchID );
}

// ---------------------------------------------------------------------------------------------------------------------
Stem::~Stem()
{
    // let any outstanding background functions finish
    if ( m_analysisFuture.valid() )
        m_analysisFuture.wait();

    blog::stem( FMTX( "[s:{}] released" ), m_data.couchID );

    mem::free16( m_channel[0] );
    mem::free16( m_channel[1] );

    m_sampleCount       = 0;
    m_state             = State::Empty;
    m_stateHttpStatus   = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
void Stem::fetch( const api::NetConfiguration& ncfg, const fs::path& cachePath )
{
    // ensure we have a space to write the stem back out to
    const absl::Status cachePathAvailable = filesys::ensureDirectoryExists( cachePath );
    if ( !cachePathAvailable.ok() )
    {
        blog::error::stem( FMTX( "Unable to create sub-directory in stem cache [{}], {}" ),
            cachePath.string(),
            cachePathAvailable.ToString() );

        m_state = State::Failed_CacheDirectory;
        return;
    }

    m_state = State::WorkEnqueued;

    // take a short ID snippet to use as a more readable tag in the log in front of everything related to this stem
    const std::string stemCouchSnip = m_data.couchID.substr( 8 );

    spacetime::ScopedTimer stemTiming( "stem finalize" );

    // prepare download buffer
    RawAudioMemory audioMemory( m_data.fileLengthBytes );

    // check to see if we already have it downloaded
    auto cacheFile = cachePath / m_data.couchID.value();
    if ( fs::exists( cacheFile ) )
    {
        blog::cache( FMTX( "[s:{}..] found in cache" ), stemCouchSnip );

        const auto fileSize = fs::file_size( cacheFile );

        if ( fileSize != audioMemory.m_rawLength )
        {
            // check if we should just accept discrepancies in the db/CDN size reports
            if ( fileSize > 0 && ncfg.api().hackAllowStemSizeMismatch )
            {
                blog::cache( FMTX( "[s:{}..] allowed cached file size mismatch; expected {}, got {}" ),
                    stemCouchSnip,
                    audioMemory.m_rawLength,
                    fileSize );

                audioMemory.allocate( fileSize );
            }
            else
            {
                blog::error::cache( FMTX( "[s:{}..] cached file size mismatch! expected {}, got {}" ),
                    stemCouchSnip,
                    audioMemory.m_rawLength,
                    fileSize );

                m_state = State::Failed_DataUnderflow;
                return;
            }
        }

        std::basic_ifstream<char> ifs( cacheFile, std::ios::in | std::ios::binary );
        ifs.read( (char*)audioMemory.m_rawAudio, fileSize );

        audioMemory.m_rawReceived = fileSize;
    }

    math::RNG32 lRng;

    if ( audioMemory.m_rawReceived == 0 )
    {
        bool stemFetchSuccess = false;

        blog::stem( FMTX( "[s:{}..] downloading [{}/{}] ..." ),
            stemCouchSnip,
            m_data.fullEndpoint(),
            m_data.fileKey );

        // things can take a while to propogate to the CDN; wait longer each cycle and try repeatedly
        for ( auto remoteFetchAttempst = 0; remoteFetchAttempst < ncfg.getRequestRetries(); remoteFetchAttempst++ )
        {
            // extend & jitter fetch delay each time we start a full fetch attempt, up to 1s
            const auto fetchDelayMs = std::min( lRng.genInt32( 0, 500 ) + ( remoteFetchAttempst * 250 ), 1000 );

            std::this_thread::sleep_for( std::chrono::milliseconds( fetchDelayMs ) );

            if ( !attemptRemoteFetch( ncfg, lRng.genUInt32(), audioMemory ) )
            {
                blog::stem( FMTX( "[s:{}..] failed attempt {} for [{}], will retry" ),
                    stemCouchSnip,
                    remoteFetchAttempst + 1,
                    m_data.fileKey );
            }
            else
            {
                stemFetchSuccess = true;
                break;
            }
        }

        if ( !stemFetchSuccess )
        {
            blog::stem( FMTX( "[s:{}..] unable to acquire [{}]"),
                stemCouchSnip,
                m_data.fileKey );
            return;
        }
    }

    // luckily we can tell what compression is in play from the first 4 bytes (so far, at least)
    const bool stemIsFLAC = (audioMemory.m_rawAudio[0] == 'f' && audioMemory.m_rawAudio[1] == 'L' && audioMemory.m_rawAudio[2] == 'a' && audioMemory.m_rawAudio[3] == 'C');
    const bool stemIsOGG  = (audioMemory.m_rawAudio[0] == 'O' && audioMemory.m_rawAudio[1] == 'g' && audioMemory.m_rawAudio[2] == 'g' && audioMemory.m_rawAudio[3] == 'S');

    // header check - do we have a file we know how to decompress?
    if ( !stemIsFLAC && !stemIsOGG )
    {
        blog::error::stem( FMTX( "[s:{}..] audio compression format not recognised" ), stemCouchSnip );
        m_state = State::Failed_Decompression;
        return;
    }

    if ( stemIsOGG )
    {
        base::instr::ScopedEvent wte( "Stem::fetch::OGG", base::instr::PresetColour::Cyan );

        // decode the vorbis stream into interleaved shorts
        short* oggData = nullptr;
        int32_t oggChannels;
        int32_t oggSampleRate;

        m_sampleCount = stb_vorbis_decode_memory(
            audioMemory.m_rawAudio,
            (int32_t)audioMemory.m_rawLength,
            &oggChannels,
            &oggSampleRate,
            &oggData );

        if ( m_sampleCount <= 0 )
        {
            blog::error::stem( FMTX( "[s:{}..] vorbis decode failure, sample count <0 ({})" ), stemCouchSnip, m_sampleCount );
            m_state = State::Failed_Decompression;
            free( oggData );
            return;
        }

        if ( oggChannels != 2 )
        {
            blog::error::stem( FMTX( "[s:{}..] invalid vorbis stream, only stereo supported; {} channels found" ), stemCouchSnip, oggChannels );
            m_state = State::Failed_Decompression;
            free( oggData );
            return;
        }

        m_compressionFormat = Compression::OggVorbis;

        // emit a successful capture back to the cache
        {
            std::basic_ofstream<char> ofs( cacheFile, std::ios::out | std::ios::binary );
            ofs.write( (char*)audioMemory.m_rawAudio, audioMemory.m_rawReceived );
        }

        static constexpr double shortToDoubleNormalisedRcp = 1.0 / 32768.0;

        // if the ogg is coming in at a different sample rate, up or downsample it to match our chosen mixer rate
        if ( oggSampleRate != m_sampleRate )
        {
            blog::stem( FMTX( "[s:{}..] resampling ogg data from {}"), stemCouchSnip, oggSampleRate );

            auto* resampleIn = mem::alloc16<double>( m_sampleCount );

            r8b::CDSPResampler24 resampler24(
                (double)oggSampleRate,
                m_sampleRate,
                m_sampleCount );

            const auto outputSampleLength = resampler24.getMaxOutLen( 0 );
            double* resampleOut = mem::alloc16<double>( outputSampleLength );

            // resample each channel to the chosen sample rate using r8brain
            for ( std::size_t channel = 0; channel < 2; channel++ )
            {
                // convert sint16 to doubles
                for ( size_t s = 0, readIndex = channel; s < m_sampleCount; s++, readIndex += 2 )
                {
                    resampleIn[s] = (double)oggData[readIndex] * shortToDoubleNormalisedRcp;
                }

                // resample stem as one-shot, standalone task
                resampler24.oneshot( resampleIn, m_sampleCount, resampleOut, outputSampleLength );

                // allocate actual channel data storage and copy across, out of resampling buffer
                m_channel[channel] = mem::alloc16<float>( outputSampleLength );
                for ( size_t s = 0; s < outputSampleLength; s++ )
                {
                    m_channel[channel][s] = static_cast<float>(resampleOut[s]);
                }
            }

            mem::free16( resampleOut );
            mem::free16( resampleIn );

            m_sampleCount = outputSampleLength;
        }
        else
        {
            // blog::stem( FMTX( "[s:{}..] stem already at {}" ), stemCouchSnip, m_sampleRate );

            m_channel[0] = mem::alloc16<float>( m_sampleCount );
            m_channel[1] = mem::alloc16<float>( m_sampleCount );

            for ( std::size_t s = 0, readIndex = 0; s < m_sampleCount; s++ )
            {
                m_channel[0][s] = (float)((double)oggData[readIndex++] * shortToDoubleNormalisedRcp);
                m_channel[1][s] = (float)((double)oggData[readIndex++] * shortToDoubleNormalisedRcp);
            }
        }

        // stb mallocs, we discard the data it gave us
        free( oggData );
    }
    else // stemIsFLAC
    {
        base::instr::ScopedEvent wte( "Stem::fetch::FLAC", base::instr::PresetColour::Amber );

        uint8_t* rawAudio = audioMemory.m_rawAudio;
        uint32_t rawAudioLen = (uint32_t)audioMemory.m_rawLength;

        // create working memory buffer for the decoder
        const uint32_t flacWorkingMemorySize = fx_flac_size( FLAC_MAX_BLOCK_SIZE, FLAC_MAX_CHANNEL_COUNT );
        void* flacWorkingMemory = mem::alloc16< uint8_t >( flacWorkingMemorySize );

        // instance the decoder with the memory pool
        fx_flac_t* flac = fx_flac_init( flacWorkingMemory, FLAC_MAX_BLOCK_SIZE, FLAC_MAX_CHANNEL_COUNT );

        // prep two channels to append to as we decode frames
        std::array<double*, 2> flacStreamChannels;
        flacStreamChannels.fill( nullptr );
        std::size_t flacStreamChannelsWriteIndex = 0;

        // inner loop decoder buffer
        static constexpr std::size_t flacDecoderBufferSize = FLAC_MAX_BLOCK_SIZE;
        int32_t* decodeBuffer = mem::alloc16< int32_t >( flacWorkingMemorySize );


        // data to pull from METADATA block
        uint64_t flacSampleRate = 0;
        uint64_t flacSampleSize = 0;
        uint64_t flacChannelCount = 0;
        uint64_t flacSampleCount = 0;

        // int32_t -> double conversion values
        double conversionNegativeRecp = 1.0;
        std::size_t conversionBitShift = 0;

        uint64_t flacFrameIndex = 0;
        bool flacFrameBeingProcessed = false;

        while( rawAudioLen > 0 ||
             ( rawAudioLen == 0 && flacFrameBeingProcessed ) )
        {
            // hit an error? bail out
            if ( m_state != State::WorkEnqueued )
                break;

            // reset incoming data sizes each time
            uint32_t rawAudioInBytes = rawAudioLen; // size of remaining raw audio
            uint32_t flacAudioOutSamples = flacDecoderBufferSize; // size of output buffer

#if OURO_DEBUG
            memset( decodeBuffer, 0, sizeof( int32_t ) * flacDecoderBufferSize );
#endif // OURO_DEBUG

            const fx_flac_state_t flacState = fx_flac_process( flac, rawAudio, &rawAudioInBytes, decodeBuffer, &flacAudioOutSamples );

#ifdef OURO_FLAC_VERBOSE
            {
                // dump out every state; only do this with a single thread running, obviously
                static constexpr std::array< std::string_view, 8 > flacStateStrings = {
                    "FLAC_ERR             ( -1 )",
                    "FLAC_INIT            (  0 )",
                    "FLAC_IN_METADATA     (  1 )",
                    "FLAC_END_OF_METADATA (  2 )",
                    "FLAC_SEARCH_FRAME    (  3 )",
                    "FLAC_IN_FRAME        (  4 )",
                    "FLAC_DECODED_FRAME   (  5 )",
                    "FLAC_END_OF_FRAME    (  6 )",
                };
                blog::stem( FMTX( "[s:{}..] FLAC-DEC Frame {} | state = {} | remaining : {:06} - {:06} | buffer to process : {:06} | write_index : {:06}" ),
                    stemCouchSnip,
                    flacFrameIndex,
                    flacStateStrings[(std::size_t)flacState + 1],
                    rawAudioLen,
                    rawAudioInBytes,
                    flacAudioOutSamples,
                    flacStreamChannelsWriteIndex );
            }
#endif // OURO_FLAC_VERBOSE

            switch ( flacState )
            {
                default:
                    blog::error::stem( FMTX( "[s:{}..] flac decode - unknown flac state [{}]" ), stemCouchSnip, (std::size_t)flacState );
                    break;

                case FLAC_ERR:
                {
                    blog::error::stem( FMTX( "[s:{}..] flac decode error - unknown error from fx_flac_process" ), stemCouchSnip );
                    m_state = State::Failed_Decompression;
                    break;
                }

                case FLAC_INIT:
                    break;
                case FLAC_IN_METADATA:
                    break;

                // metadata acquired, fetch all the stats we need
                case FLAC_END_OF_METADATA:
                {
                    // check we haven't already seen a metadata block, should just be the one (AFAIK)
                    ABSL_ASSERT( flacStreamChannels[0] == nullptr );

                    flacSampleRate      = fx_flac_get_streaminfo( flac, FLAC_KEY_SAMPLE_RATE );
                    flacChannelCount    = fx_flac_get_streaminfo( flac, FLAC_KEY_N_CHANNELS );
                    flacSampleSize      = fx_flac_get_streaminfo( flac, FLAC_KEY_SAMPLE_SIZE );
                    flacSampleCount     = fx_flac_get_streaminfo( flac, FLAC_KEY_N_SAMPLES );

                    if ( flacChannelCount != 2 )
                    {
                        blog::error::stem( FMTX( "[s:{}..] flac decode error - expecting 2 channels, got {}" ), stemCouchSnip, flacChannelCount );
                        m_state = State::Failed_Decompression;
                    }

                    blog::stem( FMTX( "[s:{}..] start flac decode : {} samples, {}-bit @ {}" ),
                        stemCouchSnip,
                        flacSampleCount,
                        flacSampleSize,
                        flacSampleRate );

                    // given the original sample bit-depth, compute what the largest value we would expect in the stream is
                    // and then produce the reciprocal used to convert to -1..+1 double values
                    const int32_t sampleMaxPositiveValue = ((int32_t)1 << (flacSampleSize - 1)) - 1;
                    const int32_t sampleMaxNegativeValue = sampleMaxPositiveValue + 1;

                    conversionNegativeRecp = 1.0 / static_cast<double>(sampleMaxNegativeValue);
                    conversionBitShift = 32 - flacSampleSize;

                    // prepare storage for the decompressed frames
                    flacStreamChannels[0] = mem::alloc16<double>( flacSampleCount );
                    flacStreamChannels[1] = mem::alloc16<double>( flacSampleCount );
                    break;
                }

                case FLAC_SEARCH_FRAME:
                    break;

                case FLAC_IN_FRAME:
                case FLAC_DECODED_FRAME:
                    flacFrameBeingProcessed = true;
                case FLAC_END_OF_FRAME:
                {
                    // check we got a metadata block first, otherwise we have nowhere to decode into
                    if ( flacStreamChannels[0] == nullptr )
                    {
                        blog::error::stem( FMTX( "[s:{}..] flac decode error - no metadata decoded before frames encountered" ), stemCouchSnip );
                        m_state = State::Failed_Decompression;
                        break;
                    }

                    ABSL_ASSERT( (flacAudioOutSamples % 2) == 0 );
                    for ( uint32_t sample = 0; sample < flacAudioOutSamples; sample+=2, flacStreamChannelsWriteIndex++ )
                    {
                        /* Quote: Note that this data is always shifted such that it uses the
                            entire 32-bit signed integer; shift to the right to the desired
                            output bit depth. You can obtain the bit-depth used in the file
                            using fx_flac_get_streaminfo(). */

                        double sampleDoubleL = static_cast<double>( decodeBuffer[sample+0] >> conversionBitShift ) * conversionNegativeRecp;
                        double sampleDoubleR = static_cast<double>( decodeBuffer[sample+1] >> conversionBitShift ) * conversionNegativeRecp;

                        flacStreamChannels[0][flacStreamChannelsWriteIndex] = sampleDoubleL;
                        flacStreamChannels[1][flacStreamChannelsWriteIndex] = sampleDoubleR;
                    }
                }
                break;
            }

            if ( flacState == FLAC_END_OF_FRAME )
            {
                flacFrameIndex++;
                flacFrameBeingProcessed = false;
            }

            rawAudio += rawAudioInBytes;
            ABSL_ASSERT( rawAudioInBytes <= rawAudioLen );
            rawAudioLen -= rawAudioInBytes;
        }

        // toss the flac decoder instance now we're done with it
        mem::free16( decodeBuffer );
        mem::free16( flacWorkingMemory );

        // check if we emerged from the loop with errors
        if ( m_state != State::WorkEnqueued )
        {
            // clean up the channel buffers before leaving
            mem::free16( flacStreamChannels[0] );
            mem::free16( flacStreamChannels[1] );

            blog::error::stem( FMTX( "[s:{}..] stem discarded, flac decompression error" ), stemCouchSnip );
            return;
        }

        // we should expect (AFAIK) to have eaten the entire incoming audio stream and produces the 
        // same number of samples as the metadata specified
        ABSL_ASSERT( rawAudioLen == 0 );

        // case where the number of samples extracted does not match what the file specified it contained : 
        // I have found this to happen exactly once amongst over 1 million stems so far - what seems to happen is we end the FLAC
        // with a tiny number of samples missing (in the specific example, just 2 samples). The final frame is a FLAC_SEARCH_FRAME
        // rather than FLAC_END_OF_FRAME. I'm not sure why that happens; to try and support these final quirks, we allow a tiny amount of
        // drift here (but also log it out as an error)
        ABSL_ASSERT( flacStreamChannelsWriteIndex <= flacSampleCount );
        if ( flacStreamChannelsWriteIndex != flacSampleCount )
        {
            blog::error::stem( FMTX( "[s:{}..] FLAC decompression sample mismatch ( written {} != declared {} )" ), stemCouchSnip, flacStreamChannelsWriteIndex, flacSampleCount );
        }

        // go backfill those missing samples, just copy them from the last one we got
        if ( flacStreamChannelsWriteIndex > 1 && flacStreamChannelsWriteIndex < flacSampleCount )
        {
            // allow a maximum number of missing samples
            const std::size_t missingSamples = flacSampleCount - flacStreamChannelsWriteIndex;
            if ( missingSamples < 4 )
            {
                blog::error::stem( FMTX( "[s:{}..] FLAC fixing {} missing samples" ), stemCouchSnip, missingSamples );

                // .. and 'fix' by just copying the last valid one we have over the gaps to avoid a click
                for ( std::size_t hackSample = flacStreamChannelsWriteIndex; hackSample < flacSampleCount; hackSample++ )
                {
                    flacStreamChannels[0][hackSample] = flacStreamChannels[0][flacStreamChannelsWriteIndex - 1];
                    flacStreamChannels[1][hackSample] = flacStreamChannels[1][flacStreamChannelsWriteIndex - 1];
                }
                flacStreamChannelsWriteIndex = flacSampleCount;
            }
            else
            {
                m_state = State::Failed_Decompression;
                return;
            }
        }
        m_sampleCount = static_cast<int32_t>( flacSampleCount );


        // similar to OGG, handle sample rate conversion as we create the final data buffers
        if ( flacSampleRate != m_sampleRate )
        {
            blog::stem( FMTX( "[s:{}..] resampling flac from {}" ), stemCouchSnip, flacSampleRate );

            r8b::CDSPResampler24 resampler24(
                (double)flacSampleRate,
                m_sampleRate,
                m_sampleCount );

            const auto outputSampleLength = resampler24.getMaxOutLen( 0 );
            double* resampleOut = mem::alloc16<double>( outputSampleLength );

            // resample each channel to the chosen sample rate using r8brain
            for ( std::size_t channel = 0; channel < 2; channel++ )
            {
                // resample stem as one-shot, standalone task
                resampler24.oneshot( flacStreamChannels[channel], m_sampleCount, resampleOut, outputSampleLength);

                // allocate actual channel data storage and copy across, out of resampling buffer
                m_channel[channel] = mem::alloc16<float>( outputSampleLength );
                for ( std::size_t s = 0; s < outputSampleLength; s++ )
                {
                    m_channel[channel][s] = static_cast<float>(resampleOut[s]);
                }
            }

            mem::free16( resampleOut );

            m_sampleCount = outputSampleLength;
        }
        // if the sample rate already matches, just copy across verbatim
        else
        {
            // blog::stem( FMTX( "[s:{}..] stem already at {}" ), stemCouchSnip, m_sampleRate );

            m_channel[0] = mem::alloc16<float>( m_sampleCount );
            m_channel[1] = mem::alloc16<float>( m_sampleCount );

            for ( std::size_t channel = 0; channel < 2; channel++ )
            {
                for ( std::size_t s = 0; s < m_sampleCount; s++ )
                {
                    m_channel[channel][s] = static_cast<float>(flacStreamChannels[channel][s]);
                }
            }
        }

        mem::free16( flacStreamChannels[0] );
        mem::free16( flacStreamChannels[1] );

        m_compressionFormat = Compression::FLAC;

        // if the decode worked, stash the original data in the cache
        {
            std::basic_ofstream<char> ofs( cacheFile, std::ios::out | std::ios::binary );
            ofs.write( (char*)audioMemory.m_rawAudio, audioMemory.m_rawReceived );
        }
    }

    // immediate post-processing steps that modify samples
    applyLoopSewingBlend();

    m_state = State::Complete;

    // report on our hard work
    {
        auto stemTime = stemTiming.stop();
        const auto humanisedMemoryUsage = base::humaniseByteSize( "using approx mem : ", estimateMemoryUsageBytes() );

        blog::stem( FMTX( "[s:{}..] finalizing took {}, {}" ),
            stemCouchSnip,
            stemTime,
            humanisedMemoryUsage );
    }
}

// ---------------------------------------------------------------------------------------------------------------------
bool Stem::attemptRemoteFetch( const api::NetConfiguration& ncfg, const uint32_t attemptUID, RawAudioMemory& audioMemory )
{
    // log network traffic
    ncfg.metricsActivitySend();

    // create client to fetch audio stream from the CDN
    const auto& httpUrl = m_data.fullEndpoint();
    auto cdnClient      = std::make_unique< httplib::SSLClient >( httpUrl.c_str() );

    cdnClient->set_ca_cert_path( ncfg.api().certBundleRelative.c_str() );
    cdnClient->enable_server_certificate_verification( true );

    cdnClient->set_default_headers(
        {
            { "Host",            httpUrl },
            { "User-Agent",      ncfg.api().userAgentApp.c_str() },
            { "Accept",          "audio/ogg" },
            { "Accept-Encoding", "gzip, deflate, br" }
        } );

    auto slashedKey = fmt::format( "/{}", m_data.fileKey );

    
    auto precheckResult = cdnClient->Head( slashedKey.c_str() );
    auto precheckError = precheckResult.error();
    
    if ( precheckError != httplib::Error::Success )
    {
        blog::error::stem( "HEAD [{}] client failure with error : {}", slashedKey, endlesss::api::getHttpLibErrorString(precheckError) );
        m_state = State::Failed_Http;
        m_stateHttpStatus = precheckResult->status;
        return false;
    }

    if ( precheckResult->status != 200 )
    {
        blog::error::stem( "HEAD [{}] response [{}]", slashedKey, precheckResult->status );
        m_state = State::Failed_Http;
        m_stateHttpStatus = precheckResult->status;
        return false;
    }

    size_t precheckDataLength = 0;
    if ( precheckResult->has_header( "content-length" ) )
    {
        auto val = precheckResult->get_header_value( "content-length" );
        if ( !val.empty() )
            precheckDataLength = (size_t) std::atoll( val.c_str() );
    }

    if ( precheckDataLength != audioMemory.m_rawLength )
    {
        // check if we should just accept discrepancies in the db/CDN size reports
        if ( precheckDataLength > 0 && 
            ncfg.api().hackAllowStemSizeMismatch )
        {
            blog::stem( "HEAD [{}] allowing content-length mismatch; got [{}], DB expected [{}]", slashedKey, precheckDataLength, audioMemory.m_rawLength );

            // rebuild the audio memory block to cope
            audioMemory.allocate( precheckDataLength );
        }
        else
        {
            blog::error::stem( "HEAD [{}] content-length mismatch; got [{}], DB expected [{}]", slashedKey, precheckDataLength, audioMemory.m_rawLength );
            m_state = State::Failed_Http;
            return false;
        }
    }

    auto res = cdnClient->Get( slashedKey.c_str(), [&]( const char* data, size_t data_length )
        {
            if ( audioMemory.m_rawReceived + data_length > audioMemory.m_rawLength )
            {
                m_state = State::Failed_DataOverflow;
                return false;
            }

            memcpy( &audioMemory.m_rawAudio[audioMemory.m_rawReceived], data, data_length );
            audioMemory.m_rawReceived += data_length;

            return true;
        });

    // log network traffic
    ncfg.metricsActivityRecv( audioMemory.m_rawReceived );

    if ( audioMemory.m_rawReceived != audioMemory.m_rawLength )
    {
        // auto result = cdnClient->get_openssl_verify_result();

        if ( !ncfg.api().hackAllowStemUnderflow )
        {
            blog::error::stem( "ogg data size mismatch [{}{}] (expected {}, got {})", httpUrl, slashedKey, audioMemory.m_rawLength, audioMemory.m_rawReceived );
            m_state = State::Failed_DataUnderflow;
            return false;
        }

        blog::stem( "fixing ogg data size mismatch [{}{}] (expected {}, got {})", httpUrl, slashedKey, audioMemory.m_rawLength, audioMemory.m_rawReceived );
        audioMemory.m_rawLength = audioMemory.m_rawReceived;
    }

    if ( m_state == State::Failed_DataOverflow )
    {
        blog::error::stem( "fetch ogg overflow [{}{}]", httpUrl, slashedKey );
        return false;
    }

    if ( res == nullptr )
    {
        blog::error::stem( "fetch ogg failed [{}{}]", httpUrl, slashedKey );
        m_state = State::Failed_Http;
        return false;
    }

    if ( res->status != 200 )
    {
        blog::error::stem( "fetch ogg failed [{}{}] | {}", httpUrl, slashedKey, res->status );
        m_state = State::Failed_Http;
        m_stateHttpStatus = res->status;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// a fairly basic edit applied to each stem that cross-fades it with itself, blending a tiny blob of the front/end samples
// to avoid trivial clicks that happen when loops don't perfectly loop (which is often). A better version of this would be to mirror 
// (say) Audacity's sample healing tool which does something far more elegant and complicated and could work on a slightly larger window
//
void Stem::applyLoopSewingBlend()
{
    static constexpr int32_t xfadeWindowSize = 128;
    static constexpr double xfadeWindowSizeRecp = 1.0 / (double)xfadeWindowSize;

    // 1..0 constant-power blend over the window size
    static constexpr auto xfadeBlendCoeff{ []() consteval
    {
        std::array<float, xfadeWindowSize> result{};
        for ( int i = 0; i < xfadeWindowSize; ++i )
        {
            const double t = -1.0f + ( (double)i * xfadeWindowSizeRecp * 2.0 );
            result[i] = (float)base::constSqrt( 0.5 * (1.0 - t) );
        }
        return result;
    }() };

    // looking at you (again), Blackest Jammmmmmmmmmmmm, bless your heart
    if ( m_sampleCount <= xfadeWindowSize * 2 )
    {
        return;
    }

    {
        // very simple - read samples advancing forward from start and backward from end
        // and do a blend over that range
        const float startL = m_channel[0][0];
        const float startR = m_channel[1][0];
        for ( int i = 0; i < xfadeWindowSize; ++i )
        {
            const auto endSample = (m_sampleCount - 1) - i;

            const float endL = m_channel[0][endSample];
            const float endR = m_channel[1][endSample];

            const float newEndL = base::lerp( endL, startL, xfadeBlendCoeff[i] );
            const float newEndR = base::lerp( endR, startR, xfadeBlendCoeff[i] );

            m_channel[0][endSample] = newEndL;
            m_channel[1][endSample] = newEndR;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
bool Stem::analyse( const Processing& processing, StemAnalysisData& result ) const
{
    using namespace dsp;
    using namespace cycfi::q::literals;

    // such a small stem that we can't really do much? shout out to Blackest Jammmmmmmmmmm for unearthing this
    if ( m_sampleCount <= processing.m_fftWindowSize )
    {
        blog::stem( "bypassing stem processing, stem C:{} only has {} samples", m_data.couchID, m_sampleCount );
        return false;
    }

    // default spectrum data for normalising freq data; we could load this from disk potentially
    const config::Spectrum audioSpectrumConfig;

    const int32_t fftWindowSize = processing.m_fftWindowSize;
    const int32_t fftTimeSlices = m_sampleCount / fftWindowSize;

    // fft output working buffers
    complexf* fftOutputL  = mem::alloc16<complexf>( fftWindowSize );
    complexf* fftOutputR  = mem::alloc16<complexf>( fftWindowSize );

    // transient frequency band buffers that then get smoothed afterwards
    auto* fftOutLowBand   = mem::alloc16<float>( fftTimeSlices );
    auto* fftOutHighBand  = mem::alloc16<float>( fftTimeSlices );


    // prepare the analysis output
    result.resize( m_sampleCount );

    {
        base::instr::ScopedEvent wte( "Stem::analyse::fft", base::instr::PresetColour::Emerald );

        for ( int64_t sI = 0, fftBandLimit = 0; sI <= m_sampleCount - fftWindowSize; sI += fftWindowSize, fftBandLimit++ )
        {
            // perform FFT on each stereo channel
            pffft_transform_ordered( processing.m_pffftPlan, &(m_channel[0][sI]), reinterpret_cast<float*>(fftOutputL), nullptr, PFFFT_FORWARD );
            pffft_transform_ordered( processing.m_pffftPlan, &(m_channel[1][sI]), reinterpret_cast<float*>(fftOutputR), nullptr, PFFFT_FORWARD );

            std::array< float, 3 > frequencyBuckets;
            frequencyBuckets.fill( 0 );

            // sum the resulting spectrum into the precomputed buckets
            for ( std::size_t freqBin = 0; freqBin < fftWindowSize / 2; freqBin++ )
            {
                const float fftMagL = fftOutputL[freqBin].hypot();
                const float fftMagR = fftOutputR[freqBin].hypot();

                const float fftMag  = (fftMagL + fftMagR) * 0.5f; // #hdd average of magnitudes 'correct' here?

                frequencyBuckets[processing.m_octaves.getBucketForFFTIndex( freqBin )] += fftMag;
            }

            // reduce and normalise the buckets we're interested in
            {
                frequencyBuckets[0] *= processing.m_octaves.getRecpSizeOfBucketAt( 0 );
                frequencyBuckets[0]  = audioSpectrumConfig.headroomNormaliseDb( frequencyBuckets[0] );

                fftOutLowBand[fftBandLimit] = frequencyBuckets[0];

                frequencyBuckets[2] *= processing.m_octaves.getRecpSizeOfBucketAt( 2 );
                frequencyBuckets[2]  = audioSpectrumConfig.headroomNormaliseDb( frequencyBuckets[2] );

                fftOutHighBand[fftBandLimit] = frequencyBuckets[2];
            }
        }
    }

    mem::free16( fftOutputR );
    mem::free16( fftOutputL );

    const cycfi::q::duration beatFollowDuration( processing.m_tuning.m_beatFollowDuration );
    const cycfi::q::duration waveFollowDuration( processing.m_tuning.m_waveFollowDuration );

    cycfi::q::peak_envelope_follower     beatFollower(   beatFollowDuration, processing.m_sampleRateF );
    cycfi::q::fast_rms_envelope_follower waveFollower(   waveFollowDuration, processing.m_sampleRateF );
    cycfi::q::fast_rms_envelope_follower waveFollowerLF( waveFollowDuration, processing.m_sampleRateF );
    cycfi::q::fast_rms_envelope_follower waveFollowerHF( waveFollowDuration, processing.m_sampleRateF );
    cycfi::q::peak peakTracker(
        processing.m_tuning.m_trackerSensitivity,
        processing.m_tuning.m_trackerHysteresis );


    #define PSA_ENCODE( _v ) static_cast<uint8_t>( std::min( _v * 255.0f, 255.0f ) );

    {
        char scBuf[32];
        base::itoa::i32toa( m_sampleCount, scBuf );

        // ensure the beat bits are fully zeroed out, saves doing it bit-by-bit on the first cycle through
        std::fill( result.m_beatBitfield.begin(), result.m_beatBitfield.end(), 0 );

        // run two loops of the signal followers, ensuring that we get a good representation of the looping signal;
        // this is also when we run peak-finding to get some beats extracted 
        // NB. in profiling, this pair of loops through the samples is significantly more expensive than the FFT
        for ( auto cycle = 0; cycle < 2; cycle++ )
        {
            base::instr::ScopedEvent wte( "Stem::analyse::signal", scBuf, base::instr::PresetColour::Indigo );

            int64_t preheatIndexSkip = -1;

            if ( cycle == 0 && m_sampleCount > (int32_t)m_sampleRate )
            {
                preheatIndexSkip = m_sampleCount - (int32_t)m_sampleRate;
            }

            std::size_t fftBandIndex = 0;
            for ( int64_t sI = 0, fftI = 0; sI < m_sampleCount; sI++, fftI++ )
            {
                // increment [fftBandIndex] every [fftWindowSize] samples, matching how they were computed above
                if ( fftI == fftWindowSize )
                {
                    fftBandIndex++;
                    fftI = 0;

                    // as the last block of samples might not have the FFT process run (as we just dumbly fit to N x WindowSize)
                    // then allow this band index to lock to the top of the allowed limit, just copying the final values from the last bucket
                    if ( fftBandIndex == fftTimeSlices )
                        fftBandIndex = fftTimeSlices - 1;
                }

                
                if ( preheatIndexSkip > 0 && sI < preheatIndexSkip )
                    continue;

                // don't imagine max() here is terribly scientific
                const float signalInput     = std::max( m_channel[0][sI], m_channel[1][sI] );
                const float signalFollow    = waveFollower( signalInput );
                const float signalFollowLF  = waveFollowerLF( fftOutLowBand[fftBandIndex] );
                const float signalFollowHF  = waveFollowerHF( fftOutHighBand[fftBandIndex] );

                float beatPeak = 0;

                if ( cycle == 0 )
                {
                    // do nothing on the first pass, m_beatBitfield has been cleared before and will only be set
                    // once we've had the followers primed by the first pass
                }
                // run the tracker on second pass
                else
                {
                    result.m_psaWave[sI]     = PSA_ENCODE( signalFollow   );
                    result.m_psaLowFreq[sI]  = PSA_ENCODE( signalFollowLF );
                    result.m_psaHighFreq[sI] = PSA_ENCODE( signalFollowHF );

                    if ( peakTracker( signalInput, signalFollow ) )
                    {
                        result.setBeatAtSample( sI );
                        beatPeak = 1.0f;
                    }
                }

                result.m_psaBeat[sI] = PSA_ENCODE( beatFollower( beatPeak ) );
            }
        }
    }

    #undef PSA_ENCODE

    mem::free16( fftOutHighBand );
    mem::free16( fftOutLowBand );

    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
bool Stem::analyse( const Processing& processing )
{
    const bool result = analyse( processing, m_analysisData );
    m_analysisState = result ? AnalysisState::AnalysisValid : AnalysisState::AnalysisEmpty;

    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
Stem::RawAudioMemory::RawAudioMemory( size_t size )
    : m_rawLength( size )
    , m_rawReceived( 0 )
    , m_rawAudio( nullptr )
{
    allocate( size );
}

// ---------------------------------------------------------------------------------------------------------------------
Stem::RawAudioMemory::~RawAudioMemory()
{
    mem::free16( m_rawAudio );
    m_rawAudio      = nullptr;
    m_rawReceived   = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
void Stem::RawAudioMemory::allocate( size_t newSize )
{
    ABSL_ASSERT( m_rawReceived == 0 );

    if ( m_rawAudio == nullptr )
        mem::free16( m_rawAudio );

    m_rawLength = newSize;
    m_rawAudio = mem::alloc16To< uint8_t >( m_rawLength + 4, 0 );   // +4 supports header read check in worst case of empty buf
}

} // namespace live
} // namespace endlesss
