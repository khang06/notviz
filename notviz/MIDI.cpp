#include <vector>
#include <array>
#include <stack>
#include <ppl.h>
#include <intrin.h>
#include "simd.h"
#include "MIDI.h"

void __fastcall MIDI_ConnectNotes(MIDI* midi) {
    size_t track_count = ((size_t)midi->m_vTracksEnd - (size_t)midi->m_vTracksStart) / 8;
    std::vector<std::array<std::stack<MIDIChannelEvent*>, 128>> vStacks;
    vStacks.resize(track_count * 16);

    concurrency::parallel_for(size_t(0), track_count, [&](int track) {
        MIDITrack* track_ptr = midi->m_vTracksStart[track];
        MIDIEvent** events = track_ptr->m_vEventsStart;
        size_t event_count = ((size_t)track_ptr->m_vEventsEnd - (size_t)track_ptr->m_vEventsStart) / 8;
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

int* g_pTrackTime = nullptr;
static void(__fastcall* MIDIPos_MIDIPos_orig)(MIDIPos*, MIDI*) = nullptr;
void __fastcall MIDIPos_MIDIPos(MIDIPos* midiPos, MIDI* midi) {
    MIDIPos_MIDIPos_orig(midiPos, midi);

    size_t iTracks = ((size_t)midi->m_vTracksEnd - (size_t)midi->m_vTracksStart) / 8;
    size_t iTracksRounded = (iTracks + 8) & ~7; // Need to round up to 32 bytes, each int is 4 bytes
    if (g_pTrackTime)
        _aligned_free(g_pTrackTime);
    g_pTrackTime = (int*)_aligned_malloc(iTracksRounded * sizeof(int), 32);
    for (size_t i = 0; i < iTracksRounded; i++)
        g_pTrackTime[i] = INT_MAX;

    // This happens at the start of MIDI::PostProcess in viz
    for (size_t i = 0; i < iTracks; i++) {
        auto track = midi->m_vTracksStart[i];
        if (track->m_vEventsStart != track->m_vEventsEnd)
            g_pTrackTime[i] = midi->m_vTracksStart[i]->m_vEventsStart[0]->GetAbsT();
    }
}

int MIDI_Parse24Bit(const unsigned char* pcData, size_t iMaxSize, int* piOut) {
    if (!pcData || !piOut || iMaxSize < 3)
        return 0;

    *piOut = pcData[0];
    *piOut = (*piOut << 8) | pcData[1];
    *piOut = (*piOut << 8) | pcData[2];

    return 3;
}

template<bool AVX>
int __fastcall MIDIPos_GetNextEvent(MIDIPos* midiPos, MIDIEvent** pOutEvent) {
    if (!pOutEvent) return 0;
    *pOutEvent = NULL;

    size_t iTracks = ((size_t)midiPos->m_vTrackPosEnd - (size_t)midiPos->m_vTrackPosStart) / 8;
    int iMinPos;
    if constexpr (AVX)
        iMinPos = (int)min_index_avx2(g_pTrackTime, (iTracks + 8) & ~7);
    else
        iMinPos = (int)min_index_sse(g_pTrackTime, (iTracks + 8) & ~7);

    if (g_pTrackTime[iMinPos] == INT_MAX)
        return 0;

    MIDITrack* pTrack = midiPos->m_MIDI->m_vTracksStart[iMinPos];
    MIDIEvent* pMinEvent = pTrack->m_vEventsStart[midiPos->m_vTrackPosStart[iMinPos]];

    // How many micro seconds did we just process?
    *pOutEvent = pMinEvent;
    int iSpan = pMinEvent->GetAbsT() - midiPos->m_iCurrTick;
    if (midiPos->m_bIsStandard)
        iSpan = (static_cast<long long>(midiPos->m_iMicroSecsPerBeat) * iSpan) / midiPos->m_iTicksPerBeat - midiPos->m_iCurrMicroSec;
    else
        iSpan = (1000000LL * iSpan) / midiPos->m_iTicksPerSecond - midiPos->m_iCurrMicroSec;
    midiPos->m_iCurrTick = pMinEvent->GetAbsT();
    midiPos->m_iCurrMicroSec = 0;
    midiPos->m_vTrackPosStart[iMinPos]++;
    g_pTrackTime[iMinPos] = midiPos->m_vTrackPosStart[iMinPos] == ((size_t)pTrack->m_vEventsEnd - (size_t)pTrack->m_vEventsStart) / 8 ?
        INT_MAX :
        midiPos->m_MIDI->m_vTracksStart[iMinPos]->m_vEventsStart[midiPos->m_vTrackPosStart[iMinPos]]->GetAbsT();

    // Change the tempo going forward if we're at a SetTempo event
    if (pMinEvent->GetEventType() == MIDIEvent::MetaEvent) {
        MIDIMetaEvent* pMetaEvent = reinterpret_cast<MIDIMetaEvent*>(pMinEvent);
        if (pMetaEvent->GetMetaEventType() == MIDIMetaEvent::SetTempo && pMetaEvent->GetDataLen() == 3)
            MIDI_Parse24Bit(pMetaEvent->GetData(), 3, &midiPos->m_iMicroSecsPerBeat);
    }

    return iSpan;
}

template int MIDIPos_GetNextEvent<true>(MIDIPos* midiPos, MIDIEvent** pOutEvent);
template int MIDIPos_GetNextEvent<false>(MIDIPos* midiPos, MIDIEvent** pOutEvent);
