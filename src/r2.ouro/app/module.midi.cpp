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
#include "app/module.midi.h"
#include "app/module.frontend.fonts.h"
#include "app/imgui.ext.h"

#include "RtMidi.h"

namespace app {
namespace module {

// ---------------------------------------------------------------------------------------------------------------------
struct Midi::State : public Midi::InputControl
{
    using MidiMessageQueue = mcc::ReaderWriterQueue< app::midi::Message >;


    State( base::EventBusClient&& eventBusClient )
        : m_eventBusClient( std::move(eventBusClient) )
    {
        Init();
    }

    ~State()
    {
        Term();
    }

    void Init()
    {
        m_midiIn = std::make_unique<RtMidiIn>();

        blog::core( "initialising RtMidi " RTMIDI_VERSION " ({}) ...", m_midiIn->getApiName( m_midiIn->getCurrentApi() ) );

        m_inputPortCount = m_midiIn->getPortCount();

        m_inputPortNames.reserve( m_inputPortCount );
        for ( uint32_t i = 0; i < m_inputPortCount; i++ )
        {
            const auto portName = m_midiIn->getPortName( i );
            m_inputPortNames.emplace_back( portName );
            blog::core( " -> [{}] {}", i, portName );
        }

        m_midiIn->setCallback( &onMidiData, this );
    }

    void Term()
    {
        blog::core( "shutting down RtMidi ..." );

        try
        {
            m_midiIn->cancelCallback();

            if ( m_midiIn->isPortOpen() )
                m_midiIn->closePort();

            m_inputPortCount = 0;
            m_inputPortNames.clear();

            m_midiIn = nullptr;
        }
        catch ( RtMidiError& error )
        {
            blog::error::core( "RtMidi error during shutdown ({})", error.getMessage() );
        }
    }

    void Restart()
    {
        Term();
        Init();
    }

    // decode midi message and enqueue anything we understand into our threadsafe pile of messages
    static void onMidiData( double timeStamp, std::vector<unsigned char>* message, void* userData )
    {
        Midi::State* state = (Midi::State*)userData;
        
        // https://www.midi.org/specifications-old/item/table-1-summary-of-midi-message
        if ( message && message->size() <= 4 )
        {
            const uint8_t controlMessage = message->at( 0 );
            const uint8_t channelNumber  = ( controlMessage & 0x0F );
            const uint8_t channelMessage = ( controlMessage & 0xF0 );

            const MidiDeviceID& activeDeviceUID = state->m_inputPortNames[state->m_inputPortOpenedIndex].getUID();
           
            if ( channelMessage == midi::NoteOn::u7Type )
            {
                const uint8_t u7OnKey = message->at( 1 ) & 0x7F;
                const uint8_t u7OnVel = message->at( 2 ) & 0x7F;
                const ::events::MidiEvent midiMsg( { timeStamp, midi::Message::Type::NoteOn, u7OnKey, u7OnVel }, activeDeviceUID );

                blog::core( "midi::NoteOn  [ {:>18} ] (#{}) [{}] [{}]", activeDeviceUID, channelNumber, u7OnKey, u7OnVel );

                state->m_eventBusClient.Send< ::events::MidiEvent >( midiMsg );
            }
            else
            if ( channelMessage == midi::NoteOff::u7Type )
            {
                const uint8_t u7OffKey = message->at( 1 ) & 0x7F;
                const uint8_t u7OffVel = message->at( 2 ) & 0x7F;
                const ::events::MidiEvent midiMsg( { timeStamp, midi::Message::Type::NoteOff, u7OffKey, u7OffVel }, activeDeviceUID );

                blog::core( "midi::NoteOff [ {:>18} ] (#{}) [{}] [{}]", activeDeviceUID, channelNumber, u7OffKey, u7OffVel );

                state->m_eventBusClient.Send< ::events::MidiEvent >( midiMsg );
            }
            else
            if ( state->m_enableCCMessages && channelMessage == midi::ControlChange::u7Type )
            {
                const uint8_t u7CtrlNum = message->at( 1 ) & 0x7F;
                const uint8_t u7CtrlVal = message->at( 2 ) & 0x7F;
                const ::events::MidiEvent midiMsg( { timeStamp, midi::Message::Type::ControlChange, u7CtrlNum, u7CtrlVal }, activeDeviceUID );

                blog::core( "midi::ControlChange(#{}) [{}] = {} (f:{})", channelNumber, u7CtrlNum, u7CtrlVal, ((midi::ControlChange)midiMsg.m_msg).valueF01() );

                state->m_eventBusClient.Send< ::events::MidiEvent >( midiMsg );
            }
        }
    }

    bool openInputPort( const uint32_t index ) override
    {
        try
        {
            m_midiIn->openPort( index );
        }
        catch ( RtMidiError& error )
        {
            blog::error::core( "RtMidi could not open port {} ({})", index, error.getMessage() );
            return false;
        }
        m_inputPortOpenedIndex = index;
        return true;
    }

    bool getOpenPortIndex( uint32_t& result ) override
    {
        try
        {
            if ( m_midiIn->isPortOpen() )
            {
                result = m_inputPortOpenedIndex;
                return true;
            }
        }
        catch ( RtMidiError& )
        {
            blog::error::core( "getOpenPortIndex() but no open port" );
            return false;
        }
        return false;
    }

    bool closeInputPort() override
    {
        try
        {
            if ( m_midiIn->isPortOpen() )
                m_midiIn->closePort();
        }
        catch ( RtMidiError& error )
        {
            blog::error::core( "RtMidi could not close port {} ({})", m_inputPortOpenedIndex, error.getMessage() );
            return false;
        }
        return true;
    }


    void imgui( app::CoreGUI& coreGUI );


    std::unique_ptr< RtMidiIn >     m_midiIn;
    uint32_t                        m_inputPortCount = 0;
    std::vector< MidiDevice >       m_inputPortNames;
    uint32_t                        m_inputPortOpenedIndex = 0;

    bool                            m_enableCCMessages = false;

    base::EventBusClient            m_eventBusClient;
};

// ---------------------------------------------------------------------------------------------------------------------
void Midi::State::imgui( app::CoreGUI& coreGUI )
{
    if ( ImGui::Begin( ICON_FA_PLUG " MIDI Devices###midimodule_main" ) )
    {
        if ( ImGui::Button( " Refresh " ) )
        {
            Restart();
        }
        ImGui::RightAlignSameLine( 160.0f );
        ImGui::SetNextItemWidth( 160.0f );
        if ( ImGui::Checkbox( " Enable CC ", &m_enableCCMessages ) )
        {
            blog::core( "midi::ControlChange stream {}", m_enableCCMessages ? "enabled" : "disabled" );
        }

        if ( m_inputPortCount > 0 )
        {
            ImGui::Spacing();
            ImGui::TextUnformatted( "Known Devices :" );
            ImGui::Scoped::AutoIndent autoIndent( 12.0f );

            const bool anyPortsOpen = m_midiIn->isPortOpen();

            for ( uint32_t portIndex = 0U; portIndex < m_inputPortCount; portIndex++ )
            {
                const bool isActive = anyPortsOpen && (portIndex == m_inputPortOpenedIndex);
                if ( ImGui::RadioButton( m_inputPortNames[portIndex].getName().c_str(), isActive ) )
                {
                    closeInputPort();
                    const bool wasOpened = openInputPort( portIndex );

                    blog::core( "activating midi device [{}] = {}",
                        m_inputPortNames[portIndex].getName(),
                        wasOpened ? "successful" : "failed" );
                }
            }
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------------
Midi::Midi()
{
}

// ---------------------------------------------------------------------------------------------------------------------
Midi::~Midi()
{
}

// ---------------------------------------------------------------------------------------------------------------------
absl::Status Midi::create( app::Core* appCore )
{
    const auto baseStatus = Module::create( appCore );
    if ( !baseStatus.ok() )
        return baseStatus;

    m_state = std::make_unique<State>( appCore->getEventBusClient() );
    return absl::OkStatus();
}

// ---------------------------------------------------------------------------------------------------------------------
void Midi::destroy()
{
    m_state.reset();

    Module::destroy();
}

// ---------------------------------------------------------------------------------------------------------------------
Midi::InputControl* Midi::getInputControl()
{
    if ( m_state != nullptr )
    {
        return m_state.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
void Midi::imgui( app::CoreGUI& coreGUI )
{
    if ( m_state != nullptr )
    {
        return m_state->imgui( coreGUI );
    }
}

} // namespace module
} // namespace app
