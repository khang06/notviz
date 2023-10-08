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

struct MIDITrack {
    char pad0[0xE0];
    MIDIEvent** events_start;
    MIDIEvent** events_end;
};

struct MIDI {
	char pad0[0x90];
    MIDITrack** tracks_start;
    MIDITrack** tracks_end;
};

void __fastcall MIDI_ConnectNotes(MIDI* midi);

extern int(__fastcall* MIDITrack_ParseTrack_orig)(MIDITrack*, const unsigned char*, int, int);
int __fastcall MIDITrack_ParseTrack(MIDITrack* track, const unsigned char* pcData, int iMaxSize, int iTrack);
