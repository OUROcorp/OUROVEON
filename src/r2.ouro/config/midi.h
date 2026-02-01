//   _______ _______ ______ _______ ___ ___ _______ _______ _______ 
//  |       |   |   |   __ \       |   |   |    ___|       |    |  |
//  |   -   |   |   |      <   -   |   |   |    ___|   -   |       |
//  |_______|_______|___|__|_______|\_____/|_______|_______|__|____|
//  \\ harry denholm \\ ishani            ishani.org/shelf/ouroveon/
//
//  
//

#pragma once

#include "config/base.h"

#include "base/utils.h"
#include "base/metaenum.h"

namespace config {

// what method we use for binding a MIDI note -
// * take whatever key we see first, ignoring velocity
// * take the key and the velocity we see first
// * wait until we see a specific velocity before we consider a key
//
#define _MIDI_BIND_MODE(_action)                \
        _action(InitialKey)                     \
        _action(InitialKeyAndVelocity)          \
        _action(WaitForSpecificVelocity)
    REFLECT_ENUM( MidiBindMode, uint32_t, _MIDI_BIND_MODE );
#undef _MIDI_BIND_MODE

OURO_CONFIG( Midi )
{
    // data routing
    static constexpr auto StoragePath       = IPathProvider::PathFor::SharedConfig;
    static constexpr auto StorageFilename   = "midi.json";

    MidiBindMode::Enum  midiBindMode = MidiBindMode::InitialKey;
    int32_t             midiBindSpecificVelocity = 0;
    bool                enableCCMessages = false;
    bool                zeroVelocityNoteOnToNoteOff = true;

    template<class Archive>
    void serialize( Archive& archive )
    {
        archive( CEREAL_OPTIONAL_NVP( midiBindSpecificVelocity )
               , CEREAL_OPTIONAL_NVP( midiBindMode )
               , CEREAL_OPTIONAL_NVP( enableCCMessages )
               , CEREAL_OPTIONAL_NVP( zeroVelocityNoteOnToNoteOff )
        );
    }
};
using MidiOptional = std::optional< Midi >;

} // namespace config

