#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "vr_config.hpp"

namespace CullFix {

struct CullPatchInfo {
    const char* name;
    uint32_t fallbackRva;
    void* pFunc;
    BYTE origBytes[6];
    bool bPatched;
};

inline CullPatchInfo* GetPatches() {
    static CullPatchInfo s_CullPatches[] = {
        { "rend_box_visible",         0x1fe70, nullptr, {0}, false },
        { "rend_sphere_visible",      0x1fe10, nullptr, {0}, false },
        { "rend_sphere_visible_dist", 0x1ff10, nullptr, {0}, false }
    };
    return s_CullPatches;
}

inline void SetCullingDisabled(bool bDisable) {
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!base) return;

    // x86 machine code:
    // mov eax, 1 (B8 01 00 00 00)
    // ret        (C3)
    const BYTE patchBytes[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };

    CullPatchInfo* patches = GetPatches();
    for (int i = 0; i < 3; i++) {
        auto& p = patches[i];
        if (!p.pFunc) {
            void* fn = (void*)GetProcAddress((HMODULE)base, p.name);
            if (!fn && p.fallbackRva != 0) {
                fn = (void*)(base + p.fallbackRva);
            }
            if (fn) {
                // Verify signature: Prism3D rend_* functions start with 'a0 2c 0a 4a 00'
                if (memcmp(fn, "\xa0\x2c\x0a\x4a\x00", 5) == 0) {
                    p.pFunc = fn;
                    memcpy(p.origBytes, fn, sizeof(patchBytes));
                }
            }
        }

        if (!p.pFunc) continue;

        if (bDisable && !p.bPatched) {
            DWORD oldProt = 0;
            if (VirtualProtect(p.pFunc, sizeof(patchBytes), PAGE_EXECUTE_READWRITE, &oldProt)) {
                memcpy(p.pFunc, patchBytes, sizeof(patchBytes));
                VirtualProtect(p.pFunc, sizeof(patchBytes), oldProt, &oldProt);
                FlushInstructionCache(GetCurrentProcess(), p.pFunc, sizeof(patchBytes));
                p.bPatched = true;
            }
        } else if (!bDisable && p.bPatched) {
            DWORD oldProt = 0;
            if (VirtualProtect(p.pFunc, sizeof(patchBytes), PAGE_EXECUTE_READWRITE, &oldProt)) {
                memcpy(p.pFunc, p.origBytes, sizeof(patchBytes));
                VirtualProtect(p.pFunc, sizeof(patchBytes), oldProt, &oldProt);
                FlushInstructionCache(GetCurrentProcess(), p.pFunc, sizeof(patchBytes));
                p.bPatched = false;
            }
        }
    }
}

} // namespace CullFix
