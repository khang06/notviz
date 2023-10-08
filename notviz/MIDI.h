#pragma once

#include <stdint.h>

class MIDIEvent {
public:
    //Event types
    enum EventType : uint32_t { ChannelEvent, MetaEvent, SysExEvent, RunningStatus };

    //Accessors
    EventType GetEventType() const { return m_eEventType; }
    int GetEventCode() const { return m_iEventCode; }
    int GetTrack() const { return m_iTrack; }
    int GetDT() const { return m_iDT; }
    int GetAbsT() const { return m_iAbsT; }
    long long GetAbsMicroSec() const { return m_llAbsMicroSec; }
    void SetAbsMicroSec(long long llAbsMicroSec) { m_llAbsMicroSec = llAbsMicroSec; }

protected:
    void* fake_vtbl;
    EventType m_eEventType;
    int m_iEventCode;
    int m_iTrack;
    int m_iDT;
    int m_iAbsT;
    long long m_llAbsMicroSec;
};

class MIDIChannelEvent : public MIDIEvent {
public:
    MIDIChannelEvent() : m_pSister(nullptr), m_iSimultaneous(0) { }

    enum ChannelEventType : uint32_t { NoteOff = 0x8, NoteOn, NoteAftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend };
    enum InputQuality : uint32_t { OnRadar, Waiting, Missed, Ok, Good, Great, Ignore };

    //Accessors
    ChannelEventType GetChannelEventType() const { return m_eChannelEventType; }
    unsigned char GetChannel() const { return m_cChannel; }
    unsigned char GetParam1() const { return m_cParam1; }
    unsigned char GetParam2() const { return m_cParam2; }
    InputQuality GetInputQuality() const { return m_eInputQuality; }
    MIDIChannelEvent* GetSister() const { return m_pSister; }
    int GetSimultaneous() const { return m_iSimultaneous; }

    void SetInputQuality(InputQuality eInputQuality) { m_eInputQuality = eInputQuality; }
    void SetSister(MIDIChannelEvent* pSister) { m_pSister = pSister; pSister->m_pSister = this; }
    void SetSimultaneous(int iSimultaneous) { m_iSimultaneous = iSimultaneous; }

    ChannelEventType m_eChannelEventType;
    InputQuality m_eInputQuality;
    unsigned char m_cChannel;
    unsigned char m_cParam1;
    unsigned char m_cParam2;
    MIDIChannelEvent* m_pSister;
    int m_iSimultaneous;
};

class MIDIMetaEvent : public MIDIEvent {
public:
    MIDIMetaEvent() : m_pcData(0) { }
    ~MIDIMetaEvent() { if (m_pcData) delete[] m_pcData; }

    enum MetaEventType : uint32_t {
        SequenceNumber, TextEvent, Copyright, SequenceName, InstrumentName, Lyric, Marker,
        CuePoint, ChannelPrefix = 0x20, PortPrefix = 0x21, EndOfTrack = 0x2F, SetTempo = 0x51,
        SMPTEOffset = 0x54, TimeSignature = 0x58, KeySignature = 0x59, Proprietary = 0x7F
    };

    //Accessors
    MetaEventType GetMetaEventType() const { return m_eMetaEventType; }
    int GetDataLen() const { return m_iDataLen; }
    unsigned char* GetData() const { return m_pcData; }

private:
    MetaEventType m_eMetaEventType;
    int m_iDataLen;
    unsigned char* m_pcData;
};

struct MIDITrack {
    char pad0[0xE0];
    MIDIEvent** m_vEventsStart;
    MIDIEvent** m_vEventsEnd;
};

struct MIDI {
	char pad0[0x90];
    MIDITrack** m_vTracksStart;
    MIDITrack** m_vTracksEnd;
};

struct MIDIPos {
    MIDI* m_MIDI;
    size_t* m_vTrackPosStart;
    size_t* m_vTrackPosEnd;
    uint8_t gap18[16];
    bool m_bIsStandard;
    int m_iTicksPerBeat;
    int m_iMicroSecsPerBeat;
    int m_iTicksPerSecond;
    int m_iCurrTick;
    int m_iCurrMicroSec;
};

void __fastcall MIDI_ConnectNotes(MIDI* midi);

extern int(__fastcall* MIDITrack_ParseTrack_orig)(MIDITrack*, const unsigned char*, int, int);
int __fastcall MIDITrack_ParseTrack(MIDITrack* track, const unsigned char* pcData, int iMaxSize, int iTrack);

template<bool AVX>
int __fastcall MIDIPos_GetNextEvent(MIDIPos* midiPos, MIDIEvent** pOutEvent);

extern void(__fastcall* MIDIPos_MIDIPos_orig)(MIDIPos*, MIDI*);
void __fastcall MIDIPos_MIDIPos(MIDIPos* midiPos, MIDI* midi);
