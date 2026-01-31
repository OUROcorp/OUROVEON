//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#define OURO_FRAMEWORK_VERSION    "1.2.2"
#define OURO_FRAMEWORK_CREDIT     "ishani.org 2026"
#define OURO_FRAMEWORK_URL        "https://ishani.org/shelf/ouroveon/"

// ---------------------------------------------------------------------------------------------------------------------

// std
#include <bitset>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <optional>
#include <regex>
#include <array>
#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <source_location>
#include <concepts>

// abseil
#include "absl/base/optimization.h"
#include "absl/memory/memory.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/container/fixed_array.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/cleanup/cleanup.h"

// ---------------------------------------------------------------------------------------------------------------------

// maximum number of threads to allow taskflow to spin up
#if OURO_DEBUG
#define OURO_THREAD_LIMIT   (30U)
#else
#define OURO_THREAD_LIMIT   (10U)
#endif


// ---------------------------------------------------------------------------------------------------------------------

#if ABSL_HAVE_CPP_ATTRIBUTE(nodiscard) || _MSC_VER >= 1911
#define ouro_nodiscard  [[nodiscard]]
#else
#define ouro_nodiscard  
#endif


// ---------------------------------------------------------------------------------------------------------------------

// rpm
#include "rpmalloc.h"

// thread scope functions, used to name them for instrumentation and ensure rpmalloc per-thread init happens
extern void ouroveonThreadEntry( const char* name );
extern void ouroveonThreadExit();

// the above, but automatic via RAII
struct OuroveonThreadScope
{
    OuroveonThreadScope( const char* threadName );
    ~OuroveonThreadScope();
private:
    char* m_name = nullptr;
};


// ---------------------------------------------------------------------------------------------------------------------

// nb we include windows here as part of the order-dependent mess that is gl/glad/glfw/et/al
// similarly, pushing APIENTRY as it gets undeffed (ok, fine) and then restored once glfw is done
#if OURO_PLATFORM_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>
#pragma push_macro("APIENTRY")
#endif // OURO_PLATFORM_WIN

// GLFW
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"
#include "GLFW/glfw3ext.h"

// pull in per-platform native functions
#if OURO_PLATFORM_WIN
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#pragma pop_macro("APIENTRY")
#endif // OURO_PLATFORM_WIN


// ---------------------------------------------------------------------------------------------------------------------

// github.com/HowardHinnant/date
#include "date/date.h"
#include "date/tz.h"

// fs
#include <filesystem>
namespace fs = std::filesystem;

// cereal
#include "cereal/cereal.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/unordered_set.hpp"
#include "cereal/types/unordered_map.hpp"
#include "cereal/archives/json.hpp"
#include "cereal/archives/binary.hpp"
#include "cereal/archives/adapters.hpp"

// cereal optional add-ins
#include "optional_json.hpp"
#include "optional_nvp.hpp"


// nlohmann JSON
#include "nlohmann/json.hpp"


// ---------------------------------------------------------------------------------------------------------------------

// {fmt}
#include "fmt/core.h"
#include "fmt/color.h"
#include "fmt/chrono.h"
#include "fmt/xchar.h"

// disable log colouring on Mac, never seems to work
#if OURO_PLATFORM_OSX
#define OURO_ENABLE_COLOURED_LOGGING    0
#else
#define OURO_ENABLE_COLOURED_LOGGING    1
#endif 

// we want the power of {fmt}'s formatting but with a bit of boilerplate to establish common
// colours and output formatting per subsystem; this exposes an arbitrarily coloured blog::<system> and blog::error::<system>
// that standardize on the column prefixes and other details whilst otherwise behaving exactly like fmt::print
namespace blog {
namespace detail {

    template < bool emit_in_release, const fmt::color _fg1, const fmt::color _fg2, typename S, typename... Args, FMT_ENABLE_IF( fmt::detail::is_string<S>::value )>
    void _printer( const std::string_view prefix, const S& format_str, const Args&... args ) noexcept
    {
        if constexpr ( ( emit_in_release && OURO_RELEASE ) || OURO_DEBUG )  // allow masking of output based on template arg and build config
        {
            const auto& vargs = fmt::make_format_args( args... );

            static constexpr std::string_view midsep = " | ";
            static constexpr std::string_view suffix = "\n";

#if OURO_ENABLE_COLOURED_LOGGING
            static constexpr auto foreground1 = fmt::detail::make_foreground_color<char>( fmt::detail::color_type( _fg1 ) );
            static constexpr auto foreground2 = fmt::detail::make_foreground_color<char>( fmt::detail::color_type( _fg2 ) );
#endif // OURO_ENABLE_COLOURED_LOGGING

            // format the input into our first buffer
            static thread_local fmt::basic_memory_buffer<char> formatBuffer;
            {
                formatBuffer.try_reserve( 2048 );
                formatBuffer.clear();
                fmt::detail::vformat_to( formatBuffer, fmt::detail::to_string_view( format_str ), vargs, {} );
            }

            // build the final output by appending components
            static thread_local fmt::basic_memory_buffer<char> outputBuffer;
            {
                outputBuffer.try_reserve( 2048 );
                outputBuffer.clear();

#if OURO_ENABLE_COLOURED_LOGGING
                outputBuffer.append( foreground1.begin(), foreground1.end() );
#endif // OURO_ENABLE_COLOURED_LOGGING

                outputBuffer.append( prefix );

#if OURO_ENABLE_COLOURED_LOGGING
                fmt::detail::reset_color( outputBuffer );
#endif // OURO_ENABLE_COLOURED_LOGGING

                outputBuffer.append( midsep );

#if OURO_ENABLE_COLOURED_LOGGING
                outputBuffer.append( foreground2.begin(), foreground2.end() );
#endif // OURO_ENABLE_COLOURED_LOGGING

                outputBuffer.append( formatBuffer );
                outputBuffer.append( suffix );

#if OURO_ENABLE_COLOURED_LOGGING
                fmt::detail::reset_color( outputBuffer );
#endif // OURO_ENABLE_COLOURED_LOGGING
                outputBuffer.push_back( '\0' );
            }

            std::fwrite( outputBuffer.data(), 1, outputBuffer.size(), stdout );
        }
    }
}

#define ADD_BLOG( _name, _colour, _nameBold )                                                                                                                                               \
        template <typename S, typename... Args, FMT_ENABLE_IF( fmt::detail::is_string<S>::value )>                                                                                          \
        void _name( const S& format_str, const Args&... args ) { detail::_printer<true, fmt::color::white, (fmt::color)_colour, S, Args...>( _nameBold, format_str, args...); }             \
        namespace error {                                                                                                                                                                   \
        template <typename S, typename... Args, FMT_ENABLE_IF( fmt::detail::is_string<S>::value )>                                                                                          \
        void _name( const S& format_str, const Args&... args ) { detail::_printer<true, fmt::color::red, fmt::color::orange_red, S, Args...>( _nameBold, format_str, args... ); }           \
        }                                                                                                                                                                                   \
        namespace debug {                                                                                                                                                                   \
        template <typename S, typename... Args, FMT_ENABLE_IF( fmt::detail::is_string<S>::value )>                                                                                          \
        void _name( const S& format_str, const Args&... args ) { detail::_printer<false, fmt::color::hot_pink, fmt::color::light_pink, S, Args...>( _nameBold, format_str, args... ); }     \
        }

ADD_BLOG( core,     0xFD971F,    "CORE" )
ADD_BLOG( gfx,      0xb05279,    " GFX" )
ADD_BLOG( app,      0xA6E22E,    " APP" )
ADD_BLOG( instr,    0xAEEF1A,    "PERF" )
ADD_BLOG( cfg,      0xe6a637,    " CFG" )
ADD_BLOG( cache,    0xe6d738,    "  C$" )
ADD_BLOG( api,      0xa2e65a,    " API" )
ADD_BLOG( database, 0x55e52d,    "  DB" )
ADD_BLOG( plug,     0x78dce8,    "PLUG" )
ADD_BLOG( discord,  0x885de6,    "DISC" )
ADD_BLOG( audio,    0xe65d88,    " AUD" )

ADD_BLOG( mix,      0x5bcce6,    " MIX" )
ADD_BLOG( jam,      0x5c7ee6,    " JAM" )
ADD_BLOG( riff,     0xe458e9,    "RIFF" )
ADD_BLOG( stem,     0xe65ea9,    "STEM" )

} // namespace blog

// shortened FMT_STRING
#define FMTX(s) FMT_STRING_IMPL(s, fmt::detail::compile_string, )

// ---------------------------------------------------------------------------------------------------------------------

// imgui
#define IM_VEC2_CLASS_EXTRA                                                                                         \
        inline ImVec2 operator*( const float rhs ) const { return ImVec2( this->x * rhs, this->y * rhs ); }         \
        inline ImVec2 operator/( const float rhs ) const { return ImVec2( this->x / rhs, this->y / rhs ); }         \
        inline ImVec2 operator+( const ImVec2& rhs ) const { return ImVec2( this->x + rhs.x, this->y + rhs.y ); }   \
        inline ImVec2 operator-( const ImVec2& rhs ) const { return ImVec2( this->x - rhs.x, this->y - rhs.y ); }   \
        inline ImVec2 operator*( const ImVec2& rhs ) const { return ImVec2( this->x * rhs.x, this->y * rhs.y ); }   \
        inline ImVec2 operator/( const ImVec2& rhs ) const { return ImVec2( this->x / rhs.x, this->y / rhs.y ); }   \
        inline ImVec2& operator*=( const float rhs ) { this->x *= rhs; this->y *= rhs; return *this; }              \
        inline ImVec2& operator/=( const float rhs ) { this->x /= rhs; this->y /= rhs; return *this; }              \
        inline ImVec2& operator+=( const ImVec2& rhs ) { this->x += rhs.x; this->y += rhs.y; return *this; }        \
        inline ImVec2& operator-=( const ImVec2& rhs ) { this->x -= rhs.x; this->y -= rhs.y; return *this; }        \
        inline ImVec2& operator*=( const ImVec2& rhs ) { this->x *= rhs.x; this->y *= rhs.y; return *this; }        \
        inline ImVec2& operator/=( const ImVec2& rhs ) { this->x /= rhs.x; this->y /= rhs.y; return *this; }        \
        template<class Archive>                                                                                     \
        void serialize( Archive& archive )                                                                          \
        {                                                                                                           \
            archive( ::cereal::make_nvp( "x", this->x ), ::cereal::make_nvp( "y", this->y ) );                      \
        }

// imgui
#define IM_VEC4_CLASS_EXTRA                                                                                                                                             \
        inline ImVec4(const std::array< float, 4 >& arf)  { x = arf[0]; y = arf[1]; z = arf[2]; w = arf[3]; }                                                           \
        inline friend ImVec4 operator*( const float lhs, const ImVec4& rhs ) { return ImVec4( rhs.x * lhs, rhs.y * lhs, rhs.z * lhs, rhs.w * lhs ); }                   \
        inline ImVec4 operator*( const float rhs ) const { return ImVec4( this->x * rhs, this->y * rhs, this->z * rhs, this->w * rhs ); }                               \
        inline ImVec4 operator+( const ImVec4& rhs ) const { return ImVec4( this->x + rhs.x, this->y + rhs.y, this->z + rhs.z, this->w + rhs.w ); }                     \
        inline ImVec4 operator*( const ImVec4& rhs ) const { return ImVec4( this->x * rhs.x, this->y * rhs.y, this->z * rhs.z, this->w * rhs.w ); }                     \
        inline ImVec4& operator*=( const float rhs ) { this->x *= rhs; this->y *= rhs; this->z *= rhs; this->w *= rhs; return *this; }                                  \
        inline ImVec4& operator+=( const ImVec4& rhs ) { this->x += rhs.x; this->y += rhs.y; this->z += rhs.z; this->w += rhs.w; return *this; }                        \
        inline ImVec4& operator*=( const ImVec4& rhs ) { this->x *= rhs.x; this->y *= rhs.y; this->z *= rhs.z; this->w *= rhs.w; return *this; }                        \
        template<class Archive>                                                                                                                                         \
        void serialize( Archive& archive )                                                                                                                              \
        {                                                                                                                                                               \
            archive( ::cereal::make_nvp( "x", this->x ), ::cereal::make_nvp( "y", this->y ), ::cereal::make_nvp( "z", this->z ), ::cereal::make_nvp( "w", this->w ) );  \
        }


#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_stdlib_ex.h"
#include "imgui_markdown.h"
#include "implot.h"
#include "imbezier.h"
#include "imnodes.h"
#include "ImGuiFileDialog.h"


// sqlite
#include "sqlite3.h"
#include "SQLiteWrapper.h"

// utf8 handling
#include "utf8.h"

// fast hasing
#include "komihash.h"

// moodycamel concurrency
#include "atomicops.h"
#include "blockingconcurrentqueue.h"
#include "concurrentqueue.h"
#include "lightweightsemaphore.h"
#include "readerwritercircularbuffer.h"
#include "readerwriterqueue.h"
namespace mcc = moodycamel;

// STB
#include "stb_image.h"
#include "stb_image_write.h"

// taskflow
#include "taskflow.hpp"

// http
#include "httplib.h"

// qlib dsp
#include <q/support/base.hpp>
#include <q/support/literals.hpp>

// lua via sol
#define SOL_ALL_SAFETIES_ON     1
#include <sol/sol.hpp>

// htrie
#include <tsl/htrie_set.h>

// CLAP plugins
#include "clap/clap.h"


// https://keisan.casio.com/calculator
namespace constants
{
    static constexpr auto d_pi          = 3.1415926535897932384626433832795028841971693993751058209749445923;
    static constexpr auto f_pi          = 3.14159265358979323846264338f;

    static constexpr auto d_2pi         = 6.2831853071795864769252867665590057683943387987502116419498891846;
    static constexpr auto f_2pi         = 6.28318530717958647692528676f;

    static constexpr auto d_half_pi     = 1.5707963267948966192313216916397514420985846996875529104874722961;
    static constexpr auto f_half_pi     = 1.57079632679489661923132169f;
                                          
    static constexpr auto d_third_pi    = 1.0471975511965977461542144610931676280657231331250352736583148641;
    static constexpr auto f_third_pi    = 1.04719755119659774615421446f;

    static constexpr auto d_quarter_pi  = 0.7853981633974483096156608458198757210492923498437764552437361481;
    static constexpr auto f_quarter_pi  = 0.78539816339744830961566084f;
    
} // namespace constants
