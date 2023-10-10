#pragma once

#include <stdint.h>

class MIDIEvent {
public:
    //Event types
    enum EventType : uint32_t { ChannelEvent, MetaEvent, SysExEvent, RunningStatus };
    static EventType DecodeEventType(int iEventCode);

    //Parsing functions that load data into the instance
    static int MakeNextEvent(const unsigned char* pcData, size_t iMaxSize, int iTrack, MIDIEvent** pOutEvent);
    virtual int ParseEvent(const unsigned char* pcData, size_t iMaxSize) = 0;

    //Accessors
    EventType GetEventType() const { return m_eEventType; }
    int GetEventCode() const { return m_iEventCode; }
    int GetTrack() const { return m_iTrack; }
    int GetDT() const { return m_iDT; }
    int GetAbsT() const { return m_iAbsT; }
    long long GetAbsMicroSec() const { return m_llAbsMicroSec; }
    void SetAbsMicroSec(long long llAbsMicroSec) { m_llAbsMicroSec = llAbsMicroSec; }

protected:
    EventType m_eEventType;
    int m_iEventCode;
    int m_iTrack;
    int m_iDT;
    int m_iAbsT;
    long long m_llAbsMicroSec;
};

class MIDIChannelEvent : public MIDIEvent {
public:
    MIDIChannelEvent() : m_pSister(nullptr), m_iSimultaneous(0), m_sLabel(nullptr) { }

    enum ChannelEventType : uint32_t { NoteOff = 0x8, NoteOn, NoteAftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend };
    enum InputQuality : uint32_t { OnRadar, Waiting, Missed, Ok, Good, Great, Ignore };
    int ParseEvent(const unsigned char* pcData, size_t iMaxSize);

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
    void* m_sLabel;
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
    int ParseEvent(const unsigned char* pcData, size_t iMaxSize);

    //Accessors
    MetaEventType GetMetaEventType() const { return m_eMetaEventType; }
    int GetDataLen() const { return m_iDataLen; }
    unsigned char* GetData() const { return m_pcData; }

private:
    MetaEventType m_eMetaEventType;
    int m_iDataLen;
    unsigned char* m_pcData;
};

class MIDISysExEvent : public MIDIEvent {
public:
    MIDISysExEvent() : m_pcData(0) { }
    ~MIDISysExEvent() { if (m_pcData) delete[] m_pcData; }

    int ParseEvent(const unsigned char* pcData, size_t iMaxSize);

private:
    int m_iSysExCode;
    int m_iDataLen;
    unsigned char* m_pcData;
    bool m_bHasMoreData;
    MIDISysExEvent* prevEvent;
};

struct MIDITrackInfo {
    int iSequenceNumber;
    uint8_t gap4[44];
    int iMinNote;
    int iMaxNote;
    int iNoteCount;
    int iEventCount;
    int iMaxVolume;
    int iVolumeSum;
    int iTotalTicks;
    uint8_t gap4C[4];
    long long llTotalMicroSecs;
    int aNoteCount[16];
    int aProgram[16];
    int iNumChannels;
};

struct MIDITrack {
    MIDITrackInfo m_TrackInfo;
    MIDIEvent** m_vEventsStart;
    MIDIEvent** m_vEventsEnd;
    MIDIEvent** m_vEventsCap;
};

struct MIDIInfo {
    uint8_t gap0[80];
    int iFormatType;
    int iNumTracks;
    int iNumChannels;
    int iDivision;
    int iMinNote;
    int iMaxNote;
    int iNoteCount;
    int iEventCount;
    int iMaxVolume;
    int iVolumeSum;
    int iTotalTicks;
    int iTotalBeats;
    long long llTotalMicroSecs;
    long long llFirstNote;
};


struct MIDI {
    MIDIInfo m_Info;
    MIDITrack** m_vTracksStart;
    MIDITrack** m_vTracksEnd;
    MIDITrack** m_vTracksCap;
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

extern void*(__fastcall* pfa_malloc)(size_t);
extern void(__fastcall* pfa_free)(void*);

void __fastcall MIDI_ConnectNotes(MIDI* midi);

template<bool AVX>
int __fastcall MIDIPos_GetNextEvent(MIDIPos* midiPos, MIDIEvent** pOutEvent);

extern void(__fastcall* MIDIPos_MIDIPos_orig)(MIDIPos*, MIDI*);
void __fastcall MIDIPos_MIDIPos(MIDIPos* midiPos, MIDI* midi);

extern void(__fastcall* sub_14000A030)(void*, void*, uint64_t, uint64_t);
extern void(__fastcall* sub_14000AC60)(void*);
extern void(__fastcall* MIDITrackInfo_AddEventInfo)(MIDITrackInfo*, MIDIEvent*);
extern void(__fastcall* MIDITrack_clear)(MIDITrack*);
extern void(__fastcall* MIDI_clear)(MIDI*);

MIDI* __fastcall MIDI_MIDI(MIDI* midi, uint64_t* sFilename);
