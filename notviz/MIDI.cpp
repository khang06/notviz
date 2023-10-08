#include <vector>
#include <array>
#include <stack>
#include <ppl.h>
#include <intrin.h>
#include "MIDI.h"

void __fastcall MIDI_ConnectNotes(MIDI* midi) {
    size_t track_count = ((size_t)midi->tracks_end - (size_t)midi->tracks_start) / 8;
    std::vector<std::array<std::stack<MIDIChannelEvent*>, 128>> vStacks;
    vStacks.resize(track_count * 16);

    concurrency::parallel_for(size_t(0), track_count, [&](int track) {
        MIDITrack* track_ptr = midi->tracks_start[track];
        MIDIEvent** events = track_ptr->events_start;
        size_t event_count = ((size_t)track_ptr->events_end - (size_t)track_ptr->events_start) / 8;
        for (size_t i = 0; i < event_count; i++) {
            if (events[i]->GetEventType() == MIDIEvent::ChannelEvent) {
                MIDIChannelEvent* pEvent = reinterpret_cast<MIDIChannelEvent*>(events[i]);
                MIDIChannelEvent::ChannelEventType eEventType = pEvent->GetChannelEventType();
                int iChannel = pEvent->GetChannel();
                int iNote = pEvent->GetParam1();
                int iVelocity = pEvent->GetParam2();
                auto& sStack = vStacks[track * 16 + iChannel][iNote];

                if (eEventType == MIDIChannelEvent::NoteOn && iVelocity > 0) {
                    sStack.push(pEvent);
                } else if (eEventType == MIDIChannelEvent::NoteOff || eEventType == MIDIChannelEvent::NoteOn) {
                    if (!sStack.empty()) {
                        auto pTop = sStack.top();
                        sStack.pop();
                        pTop->SetSister(pEvent);
                    }
                }
            }
        }
    });
}

static int(__fastcall* MIDITrack_ParseTrack_orig)(MIDITrack*, const unsigned char*, int, int) = nullptr;
int __fastcall MIDITrack_ParseTrack(MIDITrack* track, const unsigned char* pcData, int iMaxSize, int iTrack) {
    if (iMaxSize < 8)
        return 0;
    MIDITrack_ParseTrack_orig(track, pcData, iMaxSize, iTrack);
    return _byteswap_ulong(*(uint32_t*)(pcData + 4)) + 8;
}
