//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#pragma once

namespace app {
namespace midi {

// a simple distillation of MIDI 1.0 messages I care about into a data block that can be decoded / given-context
// by casting to an appropriate type based on the Type enum
struct Message
{
    static constexpr float cRecp127 = 1.0f / 127.0f;

    enum class Type
    {
        Invalid,
        NoteOn,
        NoteOff,
        ControlChange
    };

    static constexpr uint8_t typeAsByte( const Type t );
    static constexpr const char* typeAsString( const Type t );

    Message() = default;
    Message( const double time, const Type ty, const uint8_t data0, const uint8_t data1 )
        : m_time( time )
        , m_type( ty )
        , m_data0_u7( data0 )
        , m_data1_u7( data1 )
    {}

    double      m_time = 0;
    Type        m_type = Type::Invalid;
    uint8_t     m_data0_u7;
    uint8_t     m_data1_u7;
};

struct NoteOn : public Message
{
    static constexpr uint8_t u7Type = 0b1001'0000;

    constexpr uint8_t key()         const { return m_data0_u7; }
    constexpr uint8_t velocity()    const { return m_data1_u7; }
    constexpr float   velocityF01() const { return (float)velocity() * cRecp127; }
};

struct NoteOff : public Message
{
    static constexpr uint8_t u7Type = 0b1000'0000;

    constexpr uint8_t key()         const { return m_data0_u7; }
    constexpr uint8_t velocity()    const { return m_data1_u7; }
    constexpr float   velocityF01() const { return (float)velocity() * cRecp127; }
};

struct ControlChange : public Message
{
    static constexpr uint8_t u7Type = 0b1011'0000;

    constexpr uint8_t number()      const { return m_data0_u7; }
    constexpr uint8_t value()       const { return m_data1_u7; }
    constexpr float   valueF01()    const { return (float)value() * cRecp127; }

    // great news, relative CC is seemingly completely unregulated awesome love it
    enum RelativeMode
    {
        Mapping1,       // -ve 63  <- | -> 65 +ve
        Mapping2,       // -ve 127 <- | -> 1  +ve
        Mapping3,       // -ve 15  <- | -> 17 +ve
    };

    // based entirely on testing with my beatstep, fuck knows what other stuff does
    constexpr float   relativeF( const RelativeMode rm )
    {
        const auto v = value();
        switch ( rm )
        {
            case Mapping1:
                switch ( v )
                {
                    case 63: return -1.0f;
                    case 62: return -1.3f;
                    case 61: return -1.6f;
                    case 60: return -2.0f;
                    case 59: return -2.3f;
                    case 58: return -2.6f;

                    case 65: return +1.0f;
                    case 66: return +1.3f;
                    case 67: return +1.3f;
                    case 68: return +2.0f;
                    case 69: return +2.3f;
                    case 70: return +2.3f;
                }
                if ( v < 58 )
                    return -3.0f;
                if ( v > 70 )
                    return +3.0f;
                break;

            case Mapping2:
                switch ( v )
                {
                    case 127: return -1.0f;
                    case 126: return -1.3f;
                    case 125: return -1.6f;
                    case 124: return -2.0f;
                    case 123: return -2.3f;
                    case 122: return -2.6f;

                    case 1:   return +1.0f;
                    case 2:   return +1.3f;
                    case 3:   return +1.6f;
                    case 4:   return +2.0f;
                    case 5:   return +2.3f;
                    case 6:   return +2.6f;
                }
                if ( v < 122 && v > 64 )
                    return -3.0f;
                if ( v > 6 && v < 64 )
                    return +3.0f;
                break;

            case Mapping3:
                switch ( v )
                {
                    case 15: return -1.0f;
                    case 14: return -1.3f;
                    case 13: return -1.6f;
                    case 12: return -2.0f;
                    case 11: return -2.3f;
                    case 10: return -2.6f;

                    case 17: return +1.0f;
                    case 18: return +1.3f;
                    case 19: return +1.3f;
                    case 20: return +2.0f;
                    case 21: return +2.3f;
                    case 22: return +2.3f;
                }
                if ( v < 10 )
                    return -3.0f;
                if ( v > 22 )
                    return +3.0f;
                break;
        }

        return 0.0f;
    }
};

constexpr uint8_t Message::typeAsByte( const Type t )
{
    switch ( t )
    {
    case Type::NoteOn:        return NoteOn::u7Type;
    case Type::NoteOff:       return NoteOff::u7Type;
    case Type::ControlChange: return ControlChange::u7Type;
    default:
        break;
    }
    return 0;
}

constexpr const char* Message::typeAsString( const Type t )
{
    switch ( t )
    {
    case Type::NoteOn:        return "NoteOn";
    case Type::NoteOff:       return "NoteOff";
    case Type::ControlChange: return "ControlChange";
    default:
        break;
    }
    return "[unknown]";
}


} // namespace midi
} // namespace app
