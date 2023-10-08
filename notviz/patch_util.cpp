#include <Windows.h>
#include <stdio.h>
#include <stdint.h>

static char* g_near_mem = nullptr;

void patch_bytes(void* dst, void* src, size_t len) {
    DWORD prot;
    VirtualProtect(dst, len, PAGE_READWRITE, &prot);
    memcpy(dst, src, len);
    VirtualProtect(dst, len, prot, &prot);
}

void patch_bytes(uint64_t dst, void* src, size_t len) {
    patch_bytes((void*)dst, src, len);
}

void patch_call(void* target, void* func) {
    if (!g_near_mem) {
        // Try to allocate some memory within 2GB of the executable
        // TODO: This sucks, try to make this more generalized in the future
        // Also maybe query memory pages instead of bruteforcing
        constexpr size_t NEAR_SIZE = 0x100000; // 1MB should be enough
        auto search = (char*)GetModuleHandle(NULL) + 0xD0000;
        while (!g_near_mem) {
            g_near_mem = (char*)VirtualAlloc(search, NEAR_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            search += 0x1000;
        }
    }

    uint64_t jmp_target = (uint64_t)g_near_mem;

    // mov rax, func
    *g_near_mem = 0x48;
    *(g_near_mem + 1) = 0xB8;
    *(void**)(g_near_mem + 2) = func;

    // jmp rax
    *(g_near_mem + 10) = 0xFF;
    *(g_near_mem + 11) = 0xE0;
    g_near_mem += 12;

    BYTE patch[5];
    patch[0] = 0xE8;
    *(DWORD*)(patch + 1) = jmp_target - (uint64_t)target - 5;
    patch_bytes(target, patch, sizeof(patch));
}

void patch_call(uint64_t target, void* func) {
    patch_call((void*)target, func);
}

void* iat_hook(LPCWSTR target, LPCSTR dll, LPCSTR func, void* hook) {
    // Get a pointer to the module
    auto pe = (char*)GetModuleHandleW(target);
    if (!pe)
        return nullptr;

    // Get the PE optional header
    auto nt_header = (PIMAGE_NT_HEADERS64)(pe + ((PIMAGE_DOS_HEADER)pe)->e_lfanew);
    auto optional_header = &nt_header->OptionalHeader;

    // Find the dll's import descriptor
    bool found = false;
    auto import_desc = (PIMAGE_IMPORT_DESCRIPTOR)(pe + optional_header->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; import_desc->Name; import_desc++) {
        if (!_stricmp(pe + import_desc->Name, dll)) {
            found = true;
            break;
        }
    }
    if (!found)
        return nullptr;

    // Get the target function's pointer
    auto dll_handle = GetModuleHandleA(dll);
    if (!dll_handle)
        return nullptr;
    auto target_ptr = (uint64_t)GetProcAddress(dll_handle, func);
    if (!target_ptr)
        return nullptr;

    // Find and replace the target function with the hook
    auto thunk = (PIMAGE_THUNK_DATA)(pe + import_desc->FirstThunk);
    for (; thunk->u1.Function != NULL; thunk++) {
        if (thunk->u1.Function == target_ptr) {
            printf("Overriding function %s from %s in %S\n", func, dll, target ? target : L"main executable");
            patch_bytes(&thunk->u1.Function, &hook, sizeof(hook));
        }
    }

    return (void*)target_ptr;
}