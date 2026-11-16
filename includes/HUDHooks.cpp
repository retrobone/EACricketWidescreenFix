#include "stdafx.h"
#include <windows.h>
#include <psapi.h>
#include "HUDHooks.h"
#include <spdlog/spdlog.h>
#include "injector/injector.hpp"
#include "injector/assembly.hpp"
#include "Hooking.Patterns.h"

float fHUDWidth = 640.0f;
float fHUDHeight = 480.0f;
float fHUDOffsetX = 0.0f;
float fAspectRatio = 1.3333333f;
float fVideoQuadWidth = 640.00f;
float fVideoUVScale = 0.0015625f;
float fRadarAnchorX = 486.0f;
float fGaugeShift = 0.0f;

void* GetElement_Original = nullptr;
void* FindWindow_Original = nullptr;
void* ContextWindow_Original = nullptr;
DWORD jmpBack_2D_Unified = 0;

void __declspec(naked) Hook_HUD_Unified() {
    __asm {
        push[fHUDWidth]
        push 0
        push[fHUDOffsetX]
        jmp[jmpBack_2D_Unified]
    }
}

void* __cdecl Hook_GetElement_Global(int param1, int elementID, int funcType) {
    void* pElement = nullptr;
    if (funcType == 1) pElement = ((void* (__cdecl*)(int, int))GetElement_Original)(param1, elementID);
    else if (funcType == 2) pElement = ((void* (__cdecl*)(int))FindWindow_Original)(elementID);
    else if (funcType == 3) pElement = ((void* (__cdecl*)(int, int))ContextWindow_Original)(param1, elementID);

    if (pElement) {
        float* pX = (float*)((uintptr_t)pElement + 0x8);
        float currentX = *pX;

        if (elementID == 50) { *pX = fRadarAnchorX; }
        else if (elementID == 20) {
            if (currentX >= 0.0f) {
                float newX = 32.0f + fHUDOffsetX;
                if (currentX != newX) { fGaugeShift = 32.0f - currentX; *pX = newX; }
            }
        }
        else if (elementID == 70) {
            if (currentX >= 0.0f) *pX = 30.0f + fHUDOffsetX;
            else if (currentX > 30.0f) *pX = currentX + fHUDOffsetX;
        }
        else if ((elementID >= 1000 && elementID <= 1005) || elementID == 30 || elementID == 40 || elementID == 60 || elementID == 310 || elementID == 320 || elementID == 330 || elementID == 340) {
            if (currentX > -10.0f) *pX = currentX + fHUDOffsetX;
        }
        else if (elementID == 10 || elementID == 100 || elementID == 730 || elementID == 765 || elementID == 390) {
            if (currentX < 650.0f) *pX = currentX - fHUDOffsetX;
        }
    }
    return pElement;
}

static void* (__cdecl* FindElem_004120B0)(void*, int) = (void* (__cdecl*)(void*, int))0x004120B0;
void* __cdecl Hook_BallIconLookup(void* list, int id) {
    void* p = FindElem_004120B0(list, id);
    if (!p) return p;
    static float baseX = 0.0f, lastSet = 0.0f;
    static bool have = false;
    uintptr_t e = (uintptr_t)p;
    float* pX = (float*)(e + 0x8);

    if (!have || *pX != lastSet) { baseX = *pX; have = true; }

    float bIconpos = baseX + fHUDOffsetX + fGaugeShift;
    if (*pX != bIconpos) {
        *pX = bIconpos;
        *(uint8_t*)(e + 0x1C) = 1;
        *(uint8_t*)0x00A013E8 = 1;
    }
    lastSet = bIconpos;
    return p;
}

void* __cdecl Hook_Radar(int screenID, int elementID) { return Hook_GetElement_Global(screenID, elementID, 1); }
void* __cdecl Hook_FindWindow(int id) { return Hook_GetElement_Global(0, id, 2); }
void* __cdecl Hook_ContextWindow(int param1, int id) { return Hook_GetElement_Global(param1, id, 3); }

void InitHUDHooks(int ResX, int ResY) {
    float fResX = (float)ResX;
    float fResY = (float)ResY;
    fAspectRatio = fResX / fResY;

    spdlog::info("Widescreen Fix Init: {:.2f} ({}x{})", fAspectRatio, ResX, ResY);

    if (fAspectRatio < 1.334f) {
        spdlog::info("Standard 4:3 detected. Skipping patches.");
        return;
    }

    MODULEINFO modInfo = { 0 };
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &modInfo, sizeof(MODULEINFO));
    uintptr_t start = (uintptr_t)modInfo.lpBaseOfDll;
    uintptr_t end = start + modInfo.SizeOfImage;

    fHUDWidth = 480.0f * fAspectRatio;
    fHUDOffsetX = (640.0f - fHUDWidth) / 2.0f;
    fVideoQuadWidth = fHUDWidth;
    fVideoUVScale = fHUDWidth / 409920.00f;
    float fPiPCam = fHUDOffsetX + 45.0f;
    float fPiPFrame = fHUDOffsetX + 26.0f;
    static float fThrowMeter = 25.0f + fHUDOffsetX;
    fRadarAnchorX = 486.0f + (fHUDOffsetX * -1.0f);


    // 3D Aspect Ratio
    auto pattern_ar = hook::pattern("C7 44 24 6C AB AA AA 3F");
    if (!pattern_ar.empty()) injector::WriteMemory((uintptr_t)pattern_ar.get_first(4), fAspectRatio, true);

    auto pattern_ar_1 = hook::pattern("C7 41 0C AB AA AA 3F");
    if (!pattern_ar_1.empty()) injector::WriteMemory((uintptr_t)pattern_ar_1.get_first(3), fAspectRatio, true);

    // Mouse Boundary Limit
    auto mouse_limit_x = hook::pattern("DD D8 C7 46 08 00 00 18 44");
    if (!mouse_limit_x.empty()) {
        injector::WriteMemory<float>(0x00794210, fHUDWidth, true);
        injector::WriteMemory<float>((uintptr_t)mouse_limit_x.get_first(5), fHUDWidth, true);
    }
    auto mouse_limit_y = hook::pattern("DD D8 C7 46 0C 00 00 E0 43");
    if (!mouse_limit_y.empty()) {
        injector::WriteMemory<float>((uintptr_t)mouse_limit_y.get_first(5), fHUDHeight, true);
        injector::WriteMemory<float>(0x00794214, fHUDHeight, true);
    }
    auto mouse_limit_x_left = hook::pattern("7A 07 C7 46 08 00 00 00 00");
    if (!mouse_limit_x_left.empty()) injector::MakeNOP((uintptr_t)mouse_limit_x_left.get_first(2), 7, true);


    // Ball Indicator
    auto pattern_icon_ball = hook::pattern("6A 1E 50 E8 19 0B F6 FF 8B F8 8B 47 34");
    if (pattern_icon_ball.size() == 1) injector::MakeCALL((uintptr_t)pattern_icon_ball.get_first(3), Hook_BallIconLookup, true);

    // Picture-in-picture Camera
    auto pattern_pip_cam = hook::pattern("34 00 00 34 42");
    auto pattern_pip_frame = hook::pattern("BD 00 00 D0 41");
    if (!pattern_pip_cam.empty()) {
        injector::WriteMemory<float>((uintptr_t)pattern_pip_cam.get_first(1), fPiPCam, true);
        injector::WriteMemory<float>((uintptr_t)pattern_pip_frame.get_first(1), fPiPFrame, true);
    }

    if (!pattern_pip_frame.empty()) {
        auto pattern_pip_text = hook::pattern("08 00 00 D8 41");
        if (!pattern_pip_text.empty()) injector::WriteMemory<float>((uintptr_t)pattern_pip_text.get_first(1), fHUDOffsetX + 27.0f, true);
    }

    // Throw Meter
    auto pattern_field_circle = hook::pattern("D8 05 ? ? ? ? 83 C4 08 D9 5F 08 D9 44");
    if (!pattern_field_circle.empty()) injector::WriteMemory((uintptr_t)pattern_field_circle.get_first(2), &fThrowMeter, true);


    // Stretching in-game video to fit to screen (ik this is a bad implementation) 
    uint32_t uvHex = 0x3ACCCCCD;
    for (uintptr_t i = start; i < end; i++) {
        if (*(uint32_t*)i == uvHex) {
            injector::WriteMemory(i, fVideoUVScale, true);
            for (int k = 0; k < 512; k++) {
                uintptr_t backAddr = i - k;
                if (*(uint32_t*)backAddr == 0x44200000 && (*(uint32_t*)(backAddr + 10) == 0x43F00000 || *(uint32_t*)(backAddr - 10) == 0x43F00000)) {
                    injector::WriteMemory(backAddr, fVideoQuadWidth, true);
                }
            }
        }
    }

    // 2D Aspect Ratio and Resolution Hooks
    auto pattern_hud = hook::pattern("68 00 00 F0 43 68 00 00 20 44 6A 00 6A 00");
    if (!pattern_hud.empty()) {
        uintptr_t matchAddr = (uintptr_t)pattern_hud.get_first(0);
        uintptr_t hookAddr = matchAddr + 5;
        jmpBack_2D_Unified = hookAddr + 9;
        injector::MakeJMP(hookAddr, Hook_HUD_Unified, true);
        injector::MakeNOP(hookAddr + 5, 4, true);
    }

    auto w_addr = hook::pattern("C7 45 4C 80 02 00 00 C7 45 50 E0 01 00 00");
    if (!w_addr.empty()) {
        injector::WriteMemory<int>((uintptr_t)w_addr.get_first(3), ResX, true);
        injector::WriteMemory<int>((uintptr_t)w_addr.get_first(10), ResY, true);
    }

    auto p_load_a = hook::pattern("C7 44 24 1C 00 00 20 44");
    if (!p_load_a.empty()) {
        uintptr_t addr = (uintptr_t)p_load_a.get_first(0);
        injector::WriteMemory<float>(addr + 4, fResX, true);
        injector::WriteMemory<float>(addr + 12, fResY, true);
    }

    auto p_load_b = hook::pattern("C7 44 24 08 00 00 20 44");
    if (!p_load_b.empty()) {
        uintptr_t addr = (uintptr_t)p_load_b.get_first(0);
        injector::WriteMemory<float>(addr + 4, fResX, true);
        injector::WriteMemory<float>(addr + 12, fResY, true);
    }

    auto p_overlay = hook::pattern("6A 00 68 00 00 F0 43 68 00 00 20 44");
    if (!p_overlay.empty()) {
        uintptr_t addr = (uintptr_t)p_overlay.get_first(0);
        injector::WriteMemory<float>(addr + 3, fResY, true);
        injector::WriteMemory<float>(addr + 8, fResX, true);
    }

    auto p_frontend = hook::pattern("53 68 00 00 F0 43 8B E9 68 00 00 20 44 53");
    if (!p_frontend.empty()) {
        uintptr_t addr = (uintptr_t)p_frontend.get_first(0);
        injector::WriteMemory<float>(addr + 2, fResY, true);
        injector::WriteMemory<float>(addr + 9, fResX, true);
    }

    //Field Radar
    auto pattern_radar = hook::pattern("E8 8C 09 FD FF");
    if (!pattern_radar.empty()) {
        uintptr_t matchAddr = (uintptr_t)pattern_radar.get_first(0);
        GetElement_Original = injector::GetBranchDestination(matchAddr).get();
        injector::MakeCALL(matchAddr, Hook_Radar, true);
    }

    FindWindow_Original = (void*)injector::GetBranchDestination(0x004B1B8E).get();
    injector::MakeCALL(0x004B1B8E, Hook_FindWindow, true);
    injector::MakeCALL(0x004B1C65, Hook_FindWindow, true);

    ContextWindow_Original = (void*)injector::GetBranchDestination(0x004B1993).get();
    injector::MakeCALL(0x004B1993, Hook_ContextWindow, true);


    // Bypass Resolution Limits that can be selected in settings
    auto p1 = hook::pattern("81 F9 80 02 00 00 8B B7 08 03 00 00 75 0D");
    if (!p1.empty()) {
        uintptr_t addr = (uintptr_t)p1.get_first(0);
        injector::WriteMemory<uint8_t>(addr + 12, 0xEB, true);
        injector::WriteMemory<uint8_t>(addr + 13, 0x64, true);
    }

    auto p2 = hook::pattern("C7 44 24 2C 40 06 00 00");
    if (!p2.empty()) {
        uintptr_t addr = (uintptr_t)p2.get_first(0);
        injector::WriteMemory<uint16_t>(addr + 4, 0xFFFF, true);
    }

    auto p3 = hook::pattern("C7 00 01 00 00 00 E8");
    if (!p3.empty()) injector::MakeNOP((uintptr_t)p3.get_first(0), 6, true);

    auto p4 = hook::pattern("39 5C 84 54 8D 44 84 54 0F 85");
    if (!p4.empty()) injector::MakeNOP((uintptr_t)p4.get_first(0) + 8, 6, true);

    // Set desktop resolution as default resolution when someone plays first time or when someone hasn't saved profile
    auto pattern_res_force = hook::pattern("BF 80 02 00 00 89 78 30 E8 CE 29 00 00 BE E0 01 00 00");
    if (!pattern_res_force.empty()) {
        uintptr_t addr = (uintptr_t)pattern_res_force.get_first(0);
        injector::WriteMemory<int>(addr + 1, ResX, true);
        injector::WriteMemory<int>(addr + 14, ResY, true);
    }

    /*to pattern_res_2 = hook::pattern("BA E0 01 00 00 2B 50 04 B9 80 02 00 00");
    if (!pattern_res_2.empty()) {
        uintptr_t addr = (uintptr_t)pattern_res_2.get_first(0);
        injector::WriteMemory<int>(addr + 1, ResY, true);
        injector::WriteMemory<int>(addr + 9, ResX, true);
    }*/
}