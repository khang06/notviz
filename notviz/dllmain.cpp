#include <Windows.h>
#include <PathCch.h>
#include <stdio.h>
#include "MIDI.h"
#include "patch_util.h"

FARPROC D3DXCreateSprite_orig = NULL;
FARPROC D3DXCreateFontW_orig = NULL;

HWND WINAPI CreateWindowExW_hook(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X,
    int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    return CreateWindowExW(dwExStyle, lpClassName, L"PianoFromAbove (notviz " __DATE__ ")", dwStyle, X, Y, nWidth,
        nHeight, hWndParent, hMenu, hInstance, lpParam);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        // Resolve the actual d3dx9_43 imports
        wchar_t d3dx_path[MAX_PATH] = {};
        GetWindowsDirectory(d3dx_path, MAX_PATH);
        PathCchAppend(d3dx_path, MAX_PATH, L"System32\\d3dx9_43.dll");

        auto d3dx = LoadLibrary(d3dx_path);
        if (!d3dx) {
            MessageBox(NULL, L"Failed to load the original d3dx9_43.dll.", NULL, MB_ICONERROR);
            exit(1);
        }
        D3DXCreateSprite_orig = GetProcAddress(d3dx, "D3DXCreateSprite");
        D3DXCreateFontW_orig = GetProcAddress(d3dx, "D3DXCreateFontW");

        // Install hooks
        auto base = (char*)GetModuleHandle(NULL);
        {
            // Change window title
            uint8_t patch[] = { 0x90 };
            patch_call(base + 0x2D1C5, (void*)CreateWindowExW_hook);
            patch_bytes(base + 0x2D1C5 + 5, patch, sizeof(patch));
        }
        {
            // Faster MIDI::ConnectNotes
            patch_call(base + 0x16BB3, (void*)MIDI_ConnectNotes);
        }
        {
            // Faster MIDIPos::GetNextEvent
            *(char**)&MIDIPos_MIDIPos_orig = base + 0x2A660;
            patch_call(base + 0x2B4B3, (void*)MIDIPos_MIDIPos);

            auto GetNextEvent = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) ?
                (void*)MIDIPos_GetNextEvent<true> :
                (void*)MIDIPos_GetNextEvent<false>;
            patch_call(base + 0x2B4FD, GetNextEvent);
            patch_call(base + 0x2B61F, GetNextEvent);
        }
        {
            // >2GB MIDI loading
            *(char**)&pfa_malloc = base + 0x41380;
            *(char**)&pfa_free = base + 0x3CBC4;
            *(char**)&sub_14000A030 = base + 0xA030;
            *(char**)&sub_14000AC60 = base + 0xAC60;
            *(char**)&MIDITrackInfo_AddEventInfo = base + 0x2BBB0;
            *(char**)&MIDITrack_clear = base + 0x2B8B0;
            *(char**)&MIDI_clear = base + 0x2AEE0;
            patch_call(base + 0x16855, MIDI_MIDI);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

__attribute__((naked)) void D3DXCreateSprite() {
    __asm {
        jmp qword ptr [D3DXCreateSprite_orig];
    }
}

__attribute__((naked)) void D3DXCreateFontW() {
    __asm {
        jmp qword ptr [D3DXCreateFontW_orig];
    }
}
