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
#include "config/midi.h"

#include "RtMidi.h"

namespace app {
namespace module {

// ---------------------------------------------------------------------------------------------------------------------
struct Midi::State : public Midi::InputControl
{
    using MidiMessageQueue = mcc::ReaderWriterQueue< app::midi::Message >;


    State( const config::Midi& configMidi, base::EventBusClient&& eventBusClient )
        : m_configMidi( configMidi )
        , m_eventBusClient( std::move(eventBusClient) )
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

        m_recentMidiMessages.clear();

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
                
                midi::Message::Type msgType = midi::Message::Type::NoteOn;
                // a common translation (apparently); NoteOn with a 0 velocity can be considered a NoteOff
                if ( state->m_configMidi.zeroVelocityNoteOnToNoteOff && u7OnVel == 0 )
                {
                    msgType = midi::Message::Type::NoteOff;
                }

                const ::events::MidiEvent midiMsg( state->m_configMidi, { timeStamp, msgType, u7OnKey, u7OnVel }, activeDeviceUID );

                state->addRecentMessage( fmt::format( "{:14} |  ch:(#{})  key:{:<3}  vel:{:<4}",
                    midi::Message::typeAsString(msgType),
                    channelNumber,
                    u7OnKey,
                    u7OnVel ) );

                state->m_eventBusClient.Send< ::events::MidiEvent >( midiMsg );
            }
            else
            if ( channelMessage == midi::NoteOff::u7Type )
            {
                const uint8_t u7OffKey = message->at( 1 ) & 0x7F;
                const uint8_t u7OffVel = message->at( 2 ) & 0x7F;
                const midi::Message::Type msgType = midi::Message::Type::NoteOff;

                const ::events::MidiEvent midiMsg( state->m_configMidi, { timeStamp, msgType, u7OffKey, u7OffVel }, activeDeviceUID );

                state->addRecentMessage( fmt::format( "{:14} |  ch:(#{})  key:{:<3}  vel:{:<4}",
                    midi::Message::typeAsString(msgType),
                    channelNumber,
                    u7OffKey,
                    u7OffVel ) );

                state->m_eventBusClient.Send< ::events::MidiEvent >( midiMsg );
            }
            else
            if ( state->m_configMidi.enableCCMessages &&
                 channelMessage == midi::ControlChange::u7Type )
            {
                const uint8_t u7CtrlNum = message->at( 1 ) & 0x7F;
                const uint8_t u7CtrlVal = message->at( 2 ) & 0x7F;
                const midi::Message::Type msgType = midi::Message::Type::ControlChange;

                const ::events::MidiEvent midiMsg( state->m_configMidi, { timeStamp, msgType, u7CtrlNum, u7CtrlVal }, activeDeviceUID );

                state->addRecentMessage( fmt::format( "{:14} |  ch:(#{})  ctrl:{:3}  =  {} (f:{})",
                    midi::Message::typeAsString(msgType),
                    channelNumber,
                    u7CtrlNum,
                    u7CtrlVal,
                    ((midi::ControlChange)midiMsg.m_msg).valueF01() ) );

                state->m_eventBusClient.Send< ::events::MidiEvent >( midiMsg );
            }
        }
    }

    void addRecentMessage( std::string_view newMessage )
    {
        m_recentMidiMessages.emplace_front( newMessage );
        while ( m_recentMidiMessages.size() > 10 )
            m_recentMidiMessages.pop_back();
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

    config::Midi                    m_configMidi;

    std::unique_ptr< RtMidiIn >     m_midiIn;
    uint32_t                        m_inputPortCount = 0;
    std::vector< MidiDevice >       m_inputPortNames;
    uint32_t                        m_inputPortOpenedIndex = 0;

    std::deque< std::string >       m_recentMidiMessages;

    base::EventBusClient            m_eventBusClient;
};

// ---------------------------------------------------------------------------------------------------------------------
void Midi::State::imgui( app::CoreGUI& coreGUI )
{
    if ( ImGui::Begin( ICON_FA_PLUG " MIDI Devices###midimodule_main" ) )
    {
        const bool anyPortsOpen = m_midiIn->isPortOpen();

        std::string comboPreviewString = "";
        if ( anyPortsOpen )
            comboPreviewString = m_inputPortNames[m_inputPortOpenedIndex].getName();

        // refreshing the devices just involves shutting down rtMidi and restarting it
        if ( ImGui::Button( " Refresh " ) )
        {
            Restart();
        }

        // if we have any input ports at all...
        if ( m_inputPortCount > 0 )
        {
            ImGui::SameLine( 0.0f, 12.0f );

            // a list of MIDI devices known to us; choosing one switches the current (single) open port
            ImGui::SetNextItemWidth( 325.0f );
            if ( ImGui::BeginCombo( "##ActiveDeviceSelector", comboPreviewString.c_str(), 0 ) )
            {
                for ( uint32_t portIndex = 0U; portIndex < m_inputPortCount; portIndex++ )
                {
                    const bool isActive = anyPortsOpen && (portIndex == m_inputPortOpenedIndex);

                    if ( ImGui::Selectable( m_inputPortNames[portIndex].getName().c_str(), isActive ) )
                    {
                        closeInputPort();
                        const bool wasOpened = openInputPort( portIndex );

                        blog::core( "activating midi device [{}] = {}",
                            m_inputPortNames[portIndex].getName(),
                            wasOpened ? "successful" : "failed" );
                    }
                    if ( isActive )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SeparatorBreak();
        {
            bool settingsChanged = false;
            settingsChanged |= ImGui::Checkbox( "Translate [NoteOn + Velocity=0] => [NoteOff]", &m_configMidi.zeroVelocityNoteOnToNoteOff );
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( "Key Binding Mode :" );
                ImGui::SameLine( 0, 12.0f );
                ImGui::SetNextItemWidth( 260.0f );
                settingsChanged |= config::MidiBindMode::ImGuiCombo( "###bind_mode", m_configMidi.midiBindMode );
            }
            if ( m_configMidi.midiBindMode == config::MidiBindMode::WaitForSpecificVelocity )
            {
                ImGui::SameLine( 0, 12.0f );
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( " Velocity = " );
                ImGui::SameLine( 0, 2.0f );
                ImGui::SetNextItemWidth( 130.0f );
                settingsChanged |= ImGui::InputInt( "###specific_vel", &m_configMidi.midiBindSpecificVelocity, 1, ImGuiInputTextFlags_None );
                m_configMidi.midiBindSpecificVelocity = std::clamp( m_configMidi.midiBindSpecificVelocity, 0, 127 );
            }
            // bake out settings if we changed anything
            if ( settingsChanged )
            {
                std::ignore = config::save( coreGUI, m_configMidi );
            }
        }
        ImGui::SeparatorBreak();
        {
            ImGui::Scoped::Enabled ed( anyPortsOpen );
            ImGui::TextUnformatted( "Recent MIDI Messages" );

            ImGui::Scoped::AutoIndent autoIndent( 12.0f );
            for ( const std::string& msg : m_recentMidiMessages )
                ImGui::TextColoredUnformatted( colour::shades::lime.neutral(), msg.data() );
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

    config::Midi configMidi = {};
    std::ignore = config::load( *appCore, configMidi );

    m_state = std::make_unique<State>( configMidi, appCore->getEventBusClient() );
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
