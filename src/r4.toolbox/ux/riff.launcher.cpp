//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#include "pch.h"

#include "base/paging.h"
#include "base/eventbus.h"
#include "base/text.h"

#include "config/midi.h"

#include "app/imgui.ext.h"
#include "app/module.frontend.fonts.h"
#include "app/module.midi.h"
#include "app/module.midi.msg.h"
#include "colour/preset.h"
#include "colour/gradient.h"

#include "endlesss/core.types.h"

#include "mix/common.h"

#include "ux/riff.launcher.h"


namespace ux {
namespace pad {

// ---------------------------------------------------------------------------------------------------------------------
struct LaunchRiff
{
    endlesss::types::RiffIdentity               m_identity;
    endlesss::types::RiffPlaybackPermutation    m_permutation;

    void clear()
    {
        m_identity = {};
        m_permutation = {};
    }

    template<class Archive>
    inline void serialize( Archive& archive )
    {
        archive( CEREAL_NVP( m_identity )
               , CEREAL_NVP( m_permutation )
        );
    }
};

// ---------------------------------------------------------------------------------------------------------------------
struct LaunchMidi
{
    int32_t                                     m_midiKey = -1;
    uint8_t                                     m_midiVel = 0;

    void clear()
    {
        m_midiKey = -1;
        m_midiVel = 0;
    }

    template<class Archive>
    inline void serialize( Archive& archive )
    {
        archive( CEREAL_NVP( m_midiKey )
               , CEREAL_OPTIONAL_NVP( m_midiVel )
        );
    }
};

// ---------------------------------------------------------------------------------------------------------------------
struct Constants
{
    static constexpr uint32_t       PadCountX = 14;
    static constexpr uint32_t       PadCountY = 6;
    static constexpr uint32_t       PadCountTotal = (PadCountX * PadCountY);
};

template< typename _GridType >
struct Grid
{
    template<class Archive>
    inline void serialize( Archive& archive )
    {
        archive( CEREAL_NVP( m_data )
        );
    }

    _GridType& at( const uint32_t index )
    {
        ABSL_ASSERT( index < Constants::PadCountTotal );
        return m_data[index];
    }
    _GridType& at( const uint32_t x, const uint32_t y )
    {
        ABSL_ASSERT( x < Constants::PadCountX );
        ABSL_ASSERT( y < Constants::PadCountY );
        return m_data[(y * Constants::PadCountX) + x];
    }

    std::array< _GridType, Constants::PadCountTotal > m_data{};
};

using GridLaunchRiff = Grid<LaunchRiff>;
using GridLaunchMidi = Grid<LaunchMidi>;
using GridFloat = Grid<float>;

// ---------------------------------------------------------------------------------------------------------------------
struct LaunchGrid
{
    template<class Archive>
    inline void serialize( Archive& archive )
    {
        archive( CEREAL_NVP( m_gridRiff )
               , CEREAL_NVP( m_gridMidi )
        );
    }

    GridLaunchRiff      m_gridRiff;
    GridLaunchMidi      m_gridMidi;
    GridFloat           m_gridHighlight;

    void triggerHighlight( uint32_t index )
    {
        m_gridHighlight.at( index ) = 1.0f;
    }

    void decayHighlights( float deltaTime )
    {
        for ( size_t idx = 0; idx < Constants::PadCountTotal; idx++ )
        {
            m_gridHighlight.m_data[idx] -= deltaTime * 2.0f;
            if ( m_gridHighlight.m_data[idx] < 0 )
                m_gridHighlight.m_data[idx] = 0;
        }
    }
};

} // namespace pad

// ---------------------------------------------------------------------------------------------------------------------
struct RiffLauncher::State
{
    State( base::EventBusClient eventBus )
        : m_eventBusClient( std::move( eventBus ) )
    {
        APP_EVENT_BIND_TO( MidiEvent );
        APP_EVENT_BIND_TO( MixerRiffChange );
    }

    ~State()
    {
        APP_EVENT_UNBIND( MixerRiffChange );
        APP_EVENT_UNBIND( MidiEvent );
    }

    enum class CurrentPadOperation
    {
        None,
        AddCurrentRiff,
        LearnNextMidi,
        ClearPad,
    };
    // colour tints per operation type
    static constexpr colour::Preset ColourOperationAdd{     "bfd200", "aacc00", "80b918" };
    static constexpr colour::Preset ColourOperationLearn{   "b5e48c", "99d98c", "52b69a" };
    static constexpr colour::Preset ColourOperationLearnOn{ "d9ed92", "d9ed92", "99d98c" };
    static constexpr colour::Preset ColourOperationClear{   "e01e37", "c71f37", "b21e35" };


    void event_MixerRiffChange( const events::MixerRiffChange* eventData );
    void event_MidiEvent( const events::MidiEvent* eventData );


    void imgui( app::CoreGUI& coreGUI, const endlesss::live::RiffAndPermutation& currentRiff );


    // IO per layer
    void LoadGridMidi( app::CoreGUI& coreGUI );
    void SaveGridMidi( app::CoreGUI& coreGUI );
    void LoadGridRiff( app::CoreGUI& coreGUI );
    void SaveGridRiff( app::CoreGUI& coreGUI );

    template< typename _TypeLoad >
    void LoadData(
        app::CoreGUI& coreGUI,
        std::string_view dialogTitle,
        std::string_view fileExtension,
        std::function< void( std::shared_ptr<_TypeLoad> ) > handleDataLoad )
    {
        const app::StoragePaths* storagePaths = coreGUI.getStoragePaths();
        ABSL_ASSERT( storagePaths != nullptr );
        if ( storagePaths == nullptr )
            return;

        auto fileDialog = std::make_unique< ImGuiFileDialog >();

        fileDialog->OpenDialog(
            "SharedLoadDlg",
            dialogTitle.data(),
            fileExtension.data(),
            storagePaths->outputApp.string(),
            1,
            nullptr,
            ImGuiFileDialogFlags_Modal );

        std::ignore = coreGUI.activateFileDialog( std::move( fileDialog ), [this, &coreGUI, handleDataLoad]( ImGuiFileDialog& dlg )
            {
                if ( !dlg.IsOk() )
                    return;

                const fs::path fullPathToReadFrom = dlg.GetFilePathName();

                auto newData = std::make_shared<_TypeLoad>();
                try
                {
                    std::ifstream is( fullPathToReadFrom );
                    cereal::JSONInputArchive archive( is );

                    newData->serialize( archive );

                    handleDataLoad( newData );
                }
                catch ( cereal::Exception& cEx )
                {
                    blog::error::cfg( "cannot parse [{}] : {}", fullPathToReadFrom.string(), cEx.what() );

                    coreGUI.getEventBusClient().Send<::events::AddErrorPopup>(
                        "Failed To Load",
                        "File is either damaged or the format is unrecognised."
                    );
                    return;
                }
            } );
    }

    template< typename _TypeLoad >
    void SaveData(
        app::CoreGUI& coreGUI,
        std::string_view dialogTitle,
        std::string_view fileExtension,
        _TypeLoad& dataToSave )
    {
        const app::StoragePaths* storagePaths = coreGUI.getStoragePaths();
        ABSL_ASSERT( storagePaths != nullptr );
        if ( storagePaths == nullptr )
            return;

        auto fileDialog = std::make_unique< ImGuiFileDialog >();

        fileDialog->OpenDialog(
            "SharedSaveDlg",
            dialogTitle.data(),
            fileExtension.data(),
            storagePaths->outputApp.string(),
            1,
            nullptr,
            ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite );

        std::ignore = coreGUI.activateFileDialog( std::move( fileDialog ), [this, &coreGUI, &dataToSave]( ImGuiFileDialog& dlg )
            {
                if ( !dlg.IsOk() )
                    return;

                const fs::path fullPathToWriteTo = dlg.GetFilePathName();

                auto newData = std::make_shared<_TypeLoad>();
                try
                {
                    std::ofstream is( fullPathToWriteTo, std::ofstream::out );
                    is.exceptions( std::ofstream::failbit | std::ofstream::badbit );

                    cereal::JSONOutputArchive archive( is );

                    dataToSave.serialize( archive );
                }
                catch ( cereal::Exception& cEx )
                {
                    blog::error::cfg( "cannot write to [{}] : {}", fullPathToWriteTo.string(), cEx.what() );

                    coreGUI.getEventBusClient().Send<::events::AddErrorPopup>(
                        "Failed To Save",
                        "Ensure file is not read-only or in use by another application."
                    );
                    return;
                }
            } );
    }


    base::EventBusClient            m_eventBusClient;

    base::EventListenerID           m_eventLID_MixerRiffChange = base::EventListenerID::invalid();
    base::EventListenerID           m_eventLID_MidiEvent = base::EventListenerID::invalid();

    pad::LaunchGrid                 m_grid;
    CurrentPadOperation             m_currentPadOperation = CurrentPadOperation::None;

    int32_t                         m_learnMidiCurrentTarget = -1;

    endlesss::types::RiffCouchID    m_currentlyPlayingRiffID;
};


// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::event_MixerRiffChange( const events::MixerRiffChange* eventData )
{
    // might be a empty riff, only track actual riffs
    if ( eventData->m_riff != nullptr )
    {
        const auto changedToRiffID = eventData->m_riff->m_riffData.riff.couchID;
        m_currentlyPlayingRiffID = changedToRiffID;
    }
    else
    {
        // nothing playing
        m_currentlyPlayingRiffID = {};
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::event_MidiEvent( const events::MidiEvent* eventData )
{
    if ( eventData->m_msg.m_type == app::midi::Message::Type::NoteOn )
    {
        const app::midi::NoteOn* midiNoteOn = static_cast<const app::midi::NoteOn*>(&eventData->m_msg);

        // are we waiting to learn an input to assign to a pad?
        if ( m_learnMidiCurrentTarget != -1 )
        {
            pad::LaunchMidi& launchMidi = m_grid.m_gridMidi.at(m_learnMidiCurrentTarget);

            // are we waiting for a specific input velocity?
            if ( eventData->m_midiConfig.midiBindMode == config::MidiBindMode::WaitForSpecificVelocity )
            {
                // no match, abort this binding
                if ( midiNoteOn->velocity() != eventData->m_midiConfig.midiBindSpecificVelocity )
                    return;
            }

            // save both, even if we don't end up matching against velocity
            launchMidi.m_midiKey = midiNoteOn->key();
            launchMidi.m_midiVel = midiNoteOn->velocity();

            blog::app( FMTX( "MIDI key {} @ velocity {} assigned to pad {}" ),
                launchMidi.m_midiKey,
                launchMidi.m_midiVel,
                m_learnMidiCurrentTarget );

            m_grid.triggerHighlight(m_learnMidiCurrentTarget);
            m_learnMidiCurrentTarget = -1;
        }
        else
        {
            for ( uint32_t idx = 0; idx < pad::Constants::PadCountTotal; idx ++ )
            {
                const pad::LaunchMidi& launchMidi = m_grid.m_gridMidi.at(idx);

                bool shouldTrigger = false;
                // only care about the key matching?
                if ( eventData->m_midiConfig.midiBindMode == config::MidiBindMode::InitialKey )
                {
                    shouldTrigger = launchMidi.m_midiKey == midiNoteOn->key();
                }
                else
                {
                    shouldTrigger = launchMidi.m_midiKey == midiNoteOn->key() &&
                                    launchMidi.m_midiVel == midiNoteOn->velocity();
                }

                if ( shouldTrigger )
                {
                    pad::LaunchRiff& launchRiff = m_grid.m_gridRiff.at( idx );
                    m_grid.triggerHighlight( idx );

                    blog::app( FMTX( "MIDI key {} @ velocity {} matched to pad" ),
                        launchMidi.m_midiKey,
                        launchMidi.m_midiVel );

                    const bool padHasData = launchRiff.m_identity.hasData();
                    if ( padHasData )
                    {
                        m_eventBusClient.Send< ::events::EnqueueRiffPlayback >(
                            launchRiff.m_identity,
                            &launchRiff.m_permutation
                        );
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::imgui( app::CoreGUI& coreGUI, const endlesss::live::RiffAndPermutation& currentRiff )
{
    const float deltaTime = ImGui::GetIO().DeltaTime;

    #define SWAP_PAD_OP( _openum ) \
        if ( m_currentPadOperation == CurrentPadOperation::_openum ) \
            m_currentPadOperation = CurrentPadOperation::None; \
        else \
            m_currentPadOperation = CurrentPadOperation::_openum;

    static const ImVec2 ControlIconButtonSize       = ImVec2( 138.0f, 48.0f );
    static const ImVec2 SubControlIconButtonSize    = ImVec2( 65.0f,  32.0f );
    static const ImVec2 PadIconButtonSize           = ImVec2( 48.0f,  48.0f );

    // turn off Add op if there are no live riffs to avoid the Add button being locked on as disabled
    if ( currentRiff.isEmpty() && m_currentPadOperation == CurrentPadOperation::AddCurrentRiff )
        m_currentPadOperation = CurrentPadOperation::None;

    if ( ImGui::Begin( ICON_FA_ROCKET " Launcher###riff_launcher" ) )
    {
        ImGui::Spacing();

        if ( ImGui::BeginChild( "###rackControls", ImVec2( ControlIconButtonSize.x, -1.0f ) ) )
        {
            {
                ImGui::Scoped::ColourButton tb( ColourOperationAdd, m_currentPadOperation == CurrentPadOperation::AddCurrentRiff );
                ImGui::Scoped::Enabled se( currentRiff.isNotEmpty() );
                if ( ImGui::Button( " Add ", ControlIconButtonSize ) )
                {
                    SWAP_PAD_OP( AddCurrentRiff );
                }
            }
            {
                ImGui::Scoped::ColourButton tb( ColourOperationLearn, m_currentPadOperation == CurrentPadOperation::LearnNextMidi );
                if ( ImGui::Button( "Learn", ControlIconButtonSize ) )
                {
                    SWAP_PAD_OP( LearnNextMidi );
                    m_learnMidiCurrentTarget = -1;
                }
            }
            {
                ImGui::Scoped::ColourButton tb( ColourOperationClear, m_currentPadOperation == CurrentPadOperation::ClearPad );
                if ( ImGui::Button( "Clear", ControlIconButtonSize ) )
                {
                    SWAP_PAD_OP( ClearPad );
                }
            }

            ImGui::Dummy( { 0,24.0f } );

            if ( ImGui::BeginChild( "###rackControlMidi", ImVec2( SubControlIconButtonSize.x, -1.0f ) ) )
            {
                ImGui::TextUnformatted( ICON_FA_CHEVRON_UP " MIDI" );

                if ( ImGui::Button( "Clear", SubControlIconButtonSize ) )
                {
                    m_grid.m_gridMidi = {};
                }
                if ( ImGui::Button( "Load", SubControlIconButtonSize ) )
                {
                    LoadGridMidi( coreGUI );
                }
                if ( ImGui::Button( "Save", SubControlIconButtonSize ) )
                {
                    SaveGridMidi( coreGUI );
                }
            }
            ImGui::EndChild();
            ImGui::SameLine( 0, 8.0f );
            if ( ImGui::BeginChild( "###rackControlRiff", ImVec2( SubControlIconButtonSize.x, -1.0f ) ) )
            {
                ImGui::TextUnformatted( " RIFF " ICON_FA_CIRCLE );

                if ( ImGui::Button( "Clear", SubControlIconButtonSize ) )
                {
                    m_grid.m_gridRiff = {};
                }
                if ( ImGui::Button( "Load", SubControlIconButtonSize ) )
                {
                    LoadGridRiff( coreGUI );
                }
                if ( ImGui::Button( "Save", SubControlIconButtonSize ) )
                {
                    SaveGridRiff( coreGUI );
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::SameLine(0, 8.0f);

        const float rackIconWidth  = (PadIconButtonSize.x + 4.0f) * static_cast<float>( pad::Constants::PadCountX );
        const float rackIconHeight = (PadIconButtonSize.y + 4.0f) * static_cast<float>( pad::Constants::PadCountY );

        if ( ImGui::BeginChild( "###rackLayout", ImVec2( rackIconWidth, rackIconHeight ) ) )
        {
            for ( uint32_t rY = 0; rY < pad::Constants::PadCountY; rY++ )
            {
                for ( uint32_t rX = 0; rX < pad::Constants::PadCountX; rX++ )
                {
                    if ( rX > 0 )
                        ImGui::SameLine(0, 4.0f);

                    // choose a button override colour if a particular mode is active
                    colour::Preset activePadColour = ColourOperationAdd;
                    bool useActivePadColour = true;
                    switch ( m_currentPadOperation )
                    {
                        case CurrentPadOperation::None:             useActivePadColour = false;
                        case CurrentPadOperation::AddCurrentRiff:   activePadColour = ColourOperationAdd;   break;
                        case CurrentPadOperation::LearnNextMidi:    activePadColour = ColourOperationLearn; break;
                        case CurrentPadOperation::ClearPad:         activePadColour = ColourOperationClear; break;
                    }

                    // fetch which index into the list of launchables we are and use it as a differentiator index for imgui
                    const uint32_t launcherIndex = (rY * pad::Constants::PadCountX) + rX;
                    ImGui::PushID( launcherIndex );

                    // figure out what this launchable has to offer us
                    pad::LaunchRiff& launcherRiffData = m_grid.m_gridRiff.at(launcherIndex);
                    pad::LaunchMidi& launcherMidiData = m_grid.m_gridMidi.at( launcherIndex );
                    const bool padHasData = launcherRiffData.m_identity.hasData();
                    const bool padHasMidi = ( launcherMidiData.m_midiKey != -1 );

                    // pick a button label based on the state of it
                    const char* padLabel = " ";
                    if ( padHasData )
                        padLabel = ICON_FA_CIRCLE;
                    if ( padHasMidi && !padHasData)
                        padLabel = ICON_FA_CHEVRON_UP;
                    if ( padHasMidi && padHasData )
                        padLabel = ICON_FA_CIRCLE_CHEVRON_UP;
                    if ( m_learnMidiCurrentTarget == launcherIndex )
                    {
                        padLabel = ICON_FA_PLUG;
                        activePadColour = ColourOperationLearnOn;
                    }

                    const float padHighlight = m_grid.m_gridHighlight.at(launcherIndex);
                    if ( padHighlight > 0 )
                    {
                        const uint32_t highlightPulse = colour::map::Spectral_r( padHighlight ).bgrU32();

                        ImGui::PushStyleColor( ImGuiCol_Button, highlightPulse );
                        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, highlightPulse );
                        ImGui::PushStyleColor( ImGuiCol_ButtonActive, highlightPulse );
                        ImGui::PushStyleColor( ImGuiCol_Text, IM_COL32_BLACK );

                        useActivePadColour = false;
                    }

                    ImGui::Scoped::ColourButton tb( activePadColour, useActivePadColour );
                    if ( ImGui::Button( padLabel, PadIconButtonSize) )
                    {
                        switch ( m_currentPadOperation )
                        {
                            case CurrentPadOperation::None:
                            {
                                if ( padHasData )
                                {
                                    m_eventBusClient.Send< ::events::EnqueueRiffPlayback >(
                                        launcherRiffData.m_identity,
                                        &launcherRiffData.m_permutation
                                    );
                                }
                            }
                            break;

                            case CurrentPadOperation::AddCurrentRiff:
                            {
                                if ( currentRiff.isNotEmpty() )
                                {
                                    launcherRiffData.m_identity = endlesss::types::RiffIdentity(
                                        currentRiff.m_riffPtr->m_riffData.jam.couchID,
                                        currentRiff.m_riffPtr->m_riffData.riff.couchID );

                                    launcherRiffData.m_permutation = currentRiff.m_permutation;
                                }
                            }
                            break;

                            case CurrentPadOperation::LearnNextMidi:
                            {
                                m_learnMidiCurrentTarget = launcherIndex;
                                blog::app( FMTX( "MIDI learning for pad {}" ), m_learnMidiCurrentTarget );
                            }
                            break;

                            case CurrentPadOperation::ClearPad:
                            {
                                launcherRiffData.clear();
                                launcherMidiData.clear();
                            }
                            break;
                        }
                    }

                    if ( padHighlight > 0 )
                    {
                        ImGui::PopStyleColor( 4 );
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    // decay the highlights per pad
    m_grid.decayHighlights( deltaTime );

    #undef SWAP_PAD_OP
}


// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::LoadGridMidi( app::CoreGUI& coreGUI )
{
    LoadData<pad::GridLaunchMidi>( coreGUI, "Load MIDI layer...", ".grid_midi", [this]( std::shared_ptr<pad::GridLaunchMidi> newData )
        {
            m_grid.m_gridMidi = *newData;
        });
}

// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::SaveGridMidi( app::CoreGUI& coreGUI )
{
    SaveData<pad::GridLaunchMidi>( coreGUI, "Save MIDI layer...", ".grid_midi", m_grid.m_gridMidi );
}

// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::LoadGridRiff( app::CoreGUI& coreGUI )
{
    LoadData<pad::GridLaunchRiff>( coreGUI, "Load Riff layer...", ".grid_riff", [this]( std::shared_ptr<pad::GridLaunchRiff> newData )
        {
            m_grid.m_gridRiff = *newData;
        });
}

// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::State::SaveGridRiff( app::CoreGUI& coreGUI )
{
    SaveData<pad::GridLaunchRiff>( coreGUI, "Save Riff layer...", ".grid_riff", m_grid.m_gridRiff );
}

// ---------------------------------------------------------------------------------------------------------------------
RiffLauncher::RiffLauncher( base::EventBusClient eventBus )
    : m_state( std::make_unique<State>( std::move( eventBus ) ) )
{
}

// ---------------------------------------------------------------------------------------------------------------------
RiffLauncher::~RiffLauncher()
{
}

// ---------------------------------------------------------------------------------------------------------------------
void RiffLauncher::imgui( app::CoreGUI& coreGUI, const endlesss::live::RiffAndPermutation& currentRiff )
{
    m_state->imgui( coreGUI, currentRiff );
}

} // namespace ux
