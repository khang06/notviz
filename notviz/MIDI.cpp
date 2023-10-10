#include <algorithm>
#include <vector>
#include <array>
#include <stack>
#include <ppl.h>
#include <intrin.h>
#include "simd.h"
#include "MIDI.h"

static void* (__fastcall* pfa_malloc)(size_t) = nullptr;
static void(__fastcall* pfa_free)(void*) = nullptr;

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

int MIDI_ParseVarNum(const unsigned char* pcData, size_t iMaxSize, int* piOut) {
    if (!pcData || !piOut || iMaxSize <= 0)
        return 0;

    *piOut = 0;
    int i = 0;
    do
    {
        *piOut = (*piOut << 7) | (pcData[i] & 0x7F);
        i++;
    } while (i < 4 && i < iMaxSize && (pcData[i - 1] & 0x80));

    return i;
}

int MIDI_Parse32Bit(const unsigned char* pcData, size_t iMaxSize, int* piOut) {
    if (!pcData || !piOut || iMaxSize < 4)
        return 0;

    *piOut = pcData[0];
    *piOut = (*piOut << 8) | pcData[1];
    *piOut = (*piOut << 8) | pcData[2];
    *piOut = (*piOut << 8) | pcData[3];

    return 4;
}

int MIDI_Parse24Bit(const unsigned char* pcData, size_t iMaxSize, int* piOut) {
    if (!pcData || !piOut || iMaxSize < 3)
        return 0;

    *piOut = pcData[0];
    *piOut = (*piOut << 8) | pcData[1];
    *piOut = (*piOut << 8) | pcData[2];

    return 3;
}

int MIDI_Parse16Bit(const unsigned char* pcData, size_t iMaxSize, int* piOut) {
    if (!pcData || !piOut || iMaxSize < 2)
        return 0;

    *piOut = pcData[0];
    *piOut = (*piOut << 8) | pcData[1];

    return 2;
}

int MIDI_ParseNChars(const unsigned char* pcData, size_t iNChars, size_t iMaxSize, char* pcOut) {
    if (!pcData || !pcOut || iMaxSize <= 0)
        return 0;

    size_t iSize = std::min(iNChars, iMaxSize);
    memcpy(pcOut, pcData, iSize);

    return iSize;
}

static void(__fastcall* sub_14000A030)(void*, void*, uint64_t, uint64_t) = nullptr;
static void(__fastcall* sub_14000AC60)(void*) = nullptr;
static void(__fastcall* MIDITrackInfo_AddEventInfo)(MIDITrackInfo*, MIDIEvent*) = nullptr;
static void(__fastcall* MIDITrack_clear)(MIDITrack*) = nullptr;
static void(__fastcall* MIDI_clear)(MIDI*) = nullptr;

MIDIEvent::EventType MIDIEvent::DecodeEventType(int iEventCode) {
    if (iEventCode < 0x80) return RunningStatus;
    if (iEventCode < 0xF0) return ChannelEvent;
    if (iEventCode < 0xFF) return SysExEvent;
    return MetaEvent;
}

int MIDIEvent::MakeNextEvent(const unsigned char* pcData, size_t iMaxSize, int iTrack, MIDIEvent** pOutEvent) {
    MIDIEvent* pPrevEvent = *pOutEvent;

    // Parse and check DT
    int iDT;
    int iTotal = MIDI_ParseVarNum(pcData, iMaxSize, &iDT);
    if (iTotal == 0 || iMaxSize - iTotal < 1) return 0;

    // Parse and decode event code
    int iEventCode = pcData[iTotal];
    EventType eEventType = DecodeEventType(iEventCode);
    iTotal++;

    // Use previous event code for running status
    if (eEventType == RunningStatus && pPrevEvent) {
        iEventCode = pPrevEvent->GetEventCode();
        eEventType = DecodeEventType(iEventCode);
        iTotal--;
    }

    // Make the object
    switch (eEventType)
    {
    case MIDIEvent::ChannelEvent: *(void**)pOutEvent = pfa_malloc(sizeof(MIDIChannelEvent)); new(*pOutEvent) MIDIChannelEvent(); break;
    case MIDIEvent::MetaEvent: *(void**)pOutEvent = pfa_malloc(sizeof(MIDIMetaEvent)); new(*pOutEvent) MIDIMetaEvent(); break;
    case MIDIEvent::SysExEvent: *(void**)pOutEvent = pfa_malloc(sizeof(MIDISysExEvent)); new(*pOutEvent) MIDISysExEvent(); break;
    default: break;
    }

    (*pOutEvent)->m_eEventType = eEventType;
    (*pOutEvent)->m_iEventCode = iEventCode;
    (*pOutEvent)->m_iTrack = iTrack;
    (*pOutEvent)->m_iAbsT = iDT;
    if (pPrevEvent) (*pOutEvent)->m_iAbsT += pPrevEvent->m_iAbsT;

    return iTotal;
}

int MIDIChannelEvent::ParseEvent(const unsigned char* pcData, size_t iMaxSize) {
    // Split up the event code
    m_eChannelEventType = static_cast<ChannelEventType>(m_iEventCode >> 4);
    m_cChannel = m_iEventCode & 0xF;

    // Parse one parameter
    if (m_eChannelEventType == ProgramChange || m_eChannelEventType == ChannelAftertouch) {
        if (iMaxSize < 1) return 0;
        m_cParam1 = pcData[0];
        m_cParam2 = 0;
        return 1;
    }
    // Parse two parameters
    else
    {
        if (iMaxSize < 2) return 0;
        m_cParam1 = pcData[0];
        m_cParam2 = pcData[1];
        return 2;
    }
}

int MIDIMetaEvent::ParseEvent(const unsigned char* pcData, size_t iMaxSize) {
    if (iMaxSize < 1) return 0;

    // Parse the code and the length
    m_eMetaEventType = static_cast<MetaEventType>(pcData[0]);
    int iCount = MIDI_ParseVarNum(pcData + 1, iMaxSize - 1, &m_iDataLen);
    if (iCount == 0 || iMaxSize < 1 + iCount + m_iDataLen) return 0;

    // Get the data
    if (m_iDataLen > 0) {
        m_pcData = (unsigned char*)pfa_malloc(m_iDataLen);
        memcpy(m_pcData, pcData + 1 + iCount, m_iDataLen);
    }

    return 1 + iCount + m_iDataLen;
}

int MIDISysExEvent::ParseEvent(const unsigned char* pcData, size_t iMaxSize) {
    if (iMaxSize < 1) return 0;

    // Parse the code and the length
    int iCount = MIDI_ParseVarNum(pcData, iMaxSize, &m_iDataLen);
    if (iCount == 0 || iMaxSize < iCount + m_iDataLen) return 0;

    // Get the data
    if (m_iDataLen > 0) {
        m_pcData = (unsigned char*)pfa_malloc(m_iDataLen);
        memcpy(m_pcData, pcData + iCount, m_iDataLen);
        if (m_iEventCode == 0xF0 && m_pcData[m_iDataLen - 1] != 0xF7)
            m_bHasMoreData = true;
    }

    return iCount + m_iDataLen;
}

size_t MIDITrack_ParseEvents(MIDITrack* track, const unsigned char* pcData, size_t iMaxSize, size_t iTrack) {
    int iDTCode = 0;
    size_t iTotal = 0, iCount = 0;
    MIDIEvent* pEvent = NULL;
    track->m_TrackInfo.iSequenceNumber = iTrack;

    do {
        // Create and parse the event
        iCount = 0;
        iDTCode = MIDIEvent::MakeNextEvent(pcData + iTotal, iMaxSize - iTotal, iTrack, &pEvent);
        if (iDTCode > 0)
        {
            iCount = pEvent->ParseEvent(pcData + iDTCode + iTotal, iMaxSize - iDTCode - iTotal);
            if (iCount > 0) {
                iTotal += iDTCode + iCount;
                if (track->m_vEventsEnd == track->m_vEventsCap)
                    sub_14000AC60(&track->m_vEventsStart);
                *track->m_vEventsEnd++ = pEvent;
                MIDITrackInfo_AddEventInfo(&track->m_TrackInfo, pEvent);
            } else {
                pfa_free(pEvent);
            }
        }
    }
    // Until we've parsed all the data, the last parse failed, or the event signals the end of track
    while (iMaxSize - iTotal > 0 && iCount > 0 &&
        (pEvent->GetEventType() != MIDIEvent::MetaEvent ||
            reinterpret_cast<MIDIMetaEvent*>(pEvent)->GetMetaEventType() != MIDIMetaEvent::EndOfTrack));

    return iTotal;
}

int MIDITrack_ParseTrack(MIDITrack* track, const unsigned char* pcData, size_t iMaxSize, int iTrack) {
    char pcBuf[4];
    size_t iTotal;
    int iTrkSize;

    // Reset first
    MIDITrack_clear(track);

    // Read header
    if (MIDI_ParseNChars(pcData, 4, iMaxSize, pcBuf) != 4) return 0;
    if (MIDI_Parse32Bit(pcData + 4, iMaxSize - 4, &iTrkSize) != 4) return 0;
    iTotal = 8;

    // Check header
    if (strncmp(pcBuf, "MTrk", 4) != 0) return 0;

    MIDITrack_ParseEvents(track, pcData + iTotal, iMaxSize - iTotal, iTrack);
    return iTotal + iTrkSize;
}

void MIDIInfo_AddTrackInfo(MIDIInfo* info, const MIDITrack* mTrack) {
    const MIDITrackInfo& mti = mTrack->m_TrackInfo;
    info->iTotalTicks = std::max(info->iTotalTicks, mti.iTotalTicks);
    info->iEventCount += mti.iEventCount;
    info->iNumChannels += mti.iNumChannels;
    info->iVolumeSum += mti.iVolumeSum;
    if (mti.iNoteCount) {
        if (!info->iNoteCount) {
            info->iMinNote = mti.iMinNote;
            info->iMaxNote = mti.iMaxNote;
            info->iMaxVolume = mti.iMaxVolume;
        } else {
            info->iMinNote = std::min(mti.iMinNote, info->iMinNote);
            info->iMaxNote = std::max(mti.iMaxNote, info->iMaxNote);
            info->iMaxVolume = std::max(mti.iMaxVolume, info->iMaxVolume);
        }
    }
    info->iNoteCount += mti.iNoteCount;
    if (!(info->iDivision & 0x8000) && info->iDivision > 0)
        info->iTotalBeats = info->iTotalTicks / info->iDivision;
}

size_t __fastcall MIDI_ParseTracks(MIDI* midi, const unsigned char* pcData, size_t iMaxSize) {
    size_t iTotal = 0, iCount = 0, iTrack = ((size_t)midi->m_vTracksEnd - (size_t)midi->m_vTracksStart) / 8;
    do {
        // Create and parse the track
        auto track = (MIDITrack*)pfa_malloc(0x100);
        memset(track, 0, 0x100);
        ((char*)track)[0x20] = 0xF;
        iCount = MIDITrack_ParseTrack(track, pcData + iTotal, iMaxSize - iTotal, iTrack++);

        // If Success, add it to the list
        if (iCount > 0) {
            if (midi->m_vTracksEnd == midi->m_vTracksCap)
                sub_14000AC60(&midi->m_vTracksStart);
            *midi->m_vTracksEnd++ = track;
            MIDIInfo_AddTrackInfo(&midi->m_Info, track);
        } else {
            // TODO: Proper destructor. This is leaking memory!
            pfa_free(track);
        }

        iTotal += iCount;
    } while (iMaxSize - iTotal > 0 && iCount > 0 && midi->m_Info.iFormatType != 2);

    // Some MIDIs lie about the amount of tracks
    midi->m_Info.iNumTracks = (int)(((size_t)midi->m_vTracksEnd - (size_t)midi->m_vTracksStart) / 8);

    return iTotal;
}

size_t __fastcall MIDI_ParseMIDI(MIDI* midi, const unsigned char* pcData, size_t iMaxSize) {
    char pcBuf[4];
    size_t iTotal;
    int iHdrSize;

    // Reset first. This is the only parsing function that resets/clears first.
    MIDI_clear(midi);

    // Read header info
    if (MIDI_ParseNChars(pcData, 4, iMaxSize, pcBuf) != 4) return 0;
    if (MIDI_Parse32Bit(pcData + 4, iMaxSize - 4, &iHdrSize) != 4) return 0;
    iTotal = 8;

    // Check header info
    if (strncmp(pcBuf, "MThd", 4) != 0) return 0;
    iHdrSize = std::max(iHdrSize, 6); // Allowing a bad header size. Some people ignore and hard code 6.

    //Read header
    iTotal += MIDI_Parse16Bit(pcData + iTotal, iMaxSize - iTotal, &midi->m_Info.iFormatType);
    iTotal += MIDI_Parse16Bit(pcData + iTotal, iMaxSize - iTotal, &midi->m_Info.iNumTracks);
    iTotal += MIDI_Parse16Bit(pcData + iTotal, iMaxSize - iTotal, &midi->m_Info.iDivision);

    // Check header
    if (iTotal != 14 || midi->m_Info.iFormatType < 0 || midi->m_Info.iFormatType > 2 || midi->m_Info.iDivision == 0) return 0;

    // Parse the rest of the file
    iTotal += iHdrSize - 6;
    return iTotal + MIDI_ParseTracks(midi, pcData + iTotal, iMaxSize - iTotal);
}

MIDI* __fastcall MIDI_MIDI(MIDI* midi, uint64_t* sFilename) {
    memset(midi, 0, 0xA8);
    FILE* stream;

    // Open the file
    auto filename = (wchar_t*)sFilename;
    if (sFilename[3] >= 8)
        filename = *(wchar_t**)filename;
    if (_wfopen_s(&stream, filename, L"rb") == 0) {
        // Go to the end of the file to get the max size
        _fseeki64(stream, 0, SEEK_END);
        size_t iSize = static_cast<size_t>(_ftelli64(stream));
        unsigned char* pcMemBlock = new unsigned char[iSize];

        // Go to the beginning of the file to prepare for parsing
        _fseeki64(stream, 0, SEEK_SET);

        // Parse the entire MIDI to memory
        fread(reinterpret_cast<char*>(pcMemBlock), 1, iSize, stream);

        // Close the stream, since it's not needed anymore
        fclose(stream);

        // Parse it
        MIDI_ParseMIDI(midi, pcMemBlock, iSize);
        sub_14000A030(midi, sFilename, 0, ~0);

        // Clean up
        delete[] pcMemBlock;
    }

    return midi;
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
