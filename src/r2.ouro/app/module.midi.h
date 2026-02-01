//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#pragma once
#include "app/module.h"
#include "app/module.midi.msg.h"

#include "base/id.couch.h"
#include "base/text.h"

namespace app {

struct CoreGUI;

namespace module {

// ---------------------------------------------------------------------------------------------------------------------
struct _midi_device_id {};
using MidiDeviceID = base::id::StringWrapper<_midi_device_id>;


// represent a MIDI device by name; this is then translated to a low-level device index at the point of Open'ing -- 
// which can fail if the device already got unplugged
struct MidiDevice
{
    MidiDevice() = delete;
    MidiDevice( const std::string& name )
        : m_name( name )
        , m_uid( name )
    {}

    constexpr bool operator == ( const MidiDevice& rhs ) const { return rhs.m_uid == m_uid; }
    constexpr bool operator != ( const MidiDevice& rhs ) const { return rhs.m_uid != m_uid; }

    ouro_nodiscard constexpr const std::string& getName() const { return m_name; }
    ouro_nodiscard constexpr const MidiDeviceID& getUID() const { return m_uid; }

private:
    std::string     m_name;
    MidiDeviceID    m_uid;
};


// ---------------------------------------------------------------------------------------------------------------------
struct Midi : public Module
{
    DECLARE_NO_COPY_NO_MOVE( Midi );

    Midi();
    ~Midi();

    // Module
    absl::Status create( app::Core* appCore ) override;
    void destroy() override;
    virtual std::string getModuleName() const override { return "Midi"; };


    struct InputControl
    {
        virtual ~InputControl() {}
        virtual bool openInputPort( uint32_t index ) = 0;
        virtual bool getOpenPortIndex( uint32_t& result ) = 0;
        virtual bool closeInputPort() = 0;
    };
    // request api for controlling midi input; or nullptr if this MIDI boot process failed
    InputControl* getInputControl();



    void imgui( app::CoreGUI& coreGUI );

protected:

    struct State;
    std::unique_ptr< State >    m_state;
};

using MidiModule = std::unique_ptr<module::Midi>;

} // namespace module
} // namespace app

Gen_StringWrapperFormatter( app::module::MidiDeviceID )

namespace config { struct Midi; }

// ---------------------------------------------------------------------------------------------------------------------
CREATE_EVENT_BEGIN( MidiEvent )

    MidiEvent( const config::Midi& midiConfig, const app::midi::Message& msg, const app::module::MidiDeviceID& deviceUID )
        : m_midiConfig( midiConfig )
        , m_msg( msg )
        , m_deviceUID( deviceUID )
    {}

    const config::Midi&         m_midiConfig;   // immutable path back to the current midi config block
    app::midi::Message          m_msg;
    app::module::MidiDeviceID   m_deviceUID;

CREATE_EVENT_END()
