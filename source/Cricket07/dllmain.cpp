//  CRICKET 07 WIDESCREEN FIX

#include "stdafx.h"
#include <spdlog/spdlog.h>
#include <psapi.h>
#include <tuple>

#include "injector/injector.hpp"
#include "injector/assembly.hpp"
#include "injector/hooking.hpp"
#include "Hooking.Patterns.h"

using namespace injector;

// GLOBAL VARIABLES

// HUD & Aspect Ratio
float fNewHUDWidth = 853.33333f;
float fHUDOffsetX = -106.66666f;
float fNewAspect = 1.7777777f;
int   iNewHUDWidthInt = 853;

// Mouse Limits
float fMouseLimitX = 853.33333f;
float fMouseLimitY = 480.0f;

// Video Logic
float fVideoQuadWidth = 640.00f;
float fVideoUVScale = 0.0015625f;

// Field Radar & UI Globals
float fRadarAnchorX = 486.0f;
float fRadarFinalX = 344.0f;
void* GetElement_Original = nullptr;

DWORD jmpBack_2D_Unified = 0;

// ASSEMBLY HOOK

// Unified 2D HUD Hook
void __declspec(naked) Hook_HUD_Unified() {
    __asm {
        push[fNewHUDWidth]      // Width
        push 0                  // Y Offset
        push[fHUDOffsetX]       // X Offset

        jmp[jmpBack_2D_Unified]
    }
}

// Global Element Hook (Fixes UI Positions)
// This intercepts calls to GetElement to force the position of specific UI items.
typedef void* (__cdecl* GetElement_t)(int screenID, int elementID);

void* __cdecl Hook_GetElement_Global(int screenID, int elementID) {
    // Find the element
    void* pElement = ((GetElement_t)GetElement_Original)(screenID, elementID);

    if (pElement) {
        float* pX = (float*)((uintptr_t)pElement + 0x8);
        float currentX = *pX;

        // FIELD RADAR
        if (elementID == 50) { // Anchor
            *pX = fRadarAnchorX;
        }
        else if (elementID == 720) { // Radar 
            *pX = fRadarFinalX;
        }

        else if ((elementID >= 1000 && elementID <= 1005) ||
            elementID == 30 || elementID == 70 || elementID == 330) {

            if (currentX > -10.0f) {
                *pX = currentX + fHUDOffsetX;
            }
        }

        else if (elementID == 730 || elementID == 765) {

            if (currentX < 650.0f) {
                *pX = currentX - fHUDOffsetX;
            }
        }
    }

    return pElement;
}

void Init()
{
    // Resolution & Aspect Ratio Detection
    CIniReader iniReader("Cricket07WidescreenFix.ini");
    int ResX = iniReader.ReadInteger("MAIN", "ResX", 0);
    int ResY = iniReader.ReadInteger("MAIN", "ResY", 0);

    if (!ResX || !ResY)
        std::tie(ResX, ResY) = GetDesktopRes();

    float aspectRatio = (float)ResX / (float)ResY;

    // Standard (4:3) Fallback
    if (aspectRatio <= 1.334f) {
        spdlog::info("Standard 4:3 detected. Skipping patches.");
        return;
    }

    // Calculate Widescreen Values

    // Main HUD (Centers 4:3 content)
    fNewHUDWidth = 480.0f * aspectRatio;
    fHUDOffsetX = (640.0f - fNewHUDWidth) / 2.0f;
    fNewAspect = aspectRatio;

    fMouseLimitX = fNewHUDWidth;
    fVideoQuadWidth = fNewHUDWidth;
    fVideoUVScale = fNewHUDWidth / 409920.00f;

    // Radar Logic (Push to Right Edge)
    float fRadarShift = fHUDOffsetX * -1.0f;
    fRadarAnchorX = 486.0f + fRadarShift;
    fRadarFinalX = 344.0f + fRadarShift;

    MODULEINFO modInfo = { 0 };
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &modInfo, sizeof(MODULEINFO));
    uintptr_t start = (uintptr_t)modInfo.lpBaseOfDll;
    uintptr_t end = start + modInfo.SizeOfImage;

    spdlog::info("Widescreen Fix Init: {:.2f} ({}x{})", aspectRatio, ResX, ResY);
    spdlog::info("Radar Fix: Anchor(50) -> {:.2f}, Radar(720) -> {:.2f}", fRadarAnchorX, fRadarFinalX);

    // APPLY PATCHES

    // 3D Aspect Ratio
    auto pattern_ar = hook::pattern("C7 44 24 6C AB AA AA 3F");
    if (!pattern_ar.empty()) {
        uintptr_t address_of_float = (uintptr_t)pattern_ar.get_first(4);
        injector::WriteMemory(address_of_float, fNewAspect, true);
        spdlog::info("Patched 3D Aspect Ratio");
    }

    // Mouse Limits
    for (uintptr_t i = start; i < end; i += 4) {
        uint32_t val = *(uint32_t*)i;
        if (val == 0x44180000) { // 608.0
            injector::WriteMemory(i, fMouseLimitX, true);
        }
        else if (val == 0x43E00000) { // 448.0
            injector::WriteMemory(i, fMouseLimitY, true);
        }
    }
    spdlog::info("Patched Mouse Limits");

    // PiP Frame & Camera
    auto pattern_pip_cam = hook::pattern("34 00 00 34 42");
    auto pattern_pip_frame = hook::pattern("BD 00 00 D0 41");

    if (!pattern_pip_cam.empty()) {
        uintptr_t addrCam = (uintptr_t)pattern_pip_cam.get_first(1);
        uintptr_t addrFrame = (uintptr_t)pattern_pip_frame.get_first(1);

        float fPiPCam = fHUDOffsetX + 45.0f;
        float fPiPFrame = fHUDOffsetX + 26.0f;

        injector::WriteMemory<float>(addrCam, fPiPCam, true);
        injector::WriteMemory<float>(addrFrame, fPiPFrame, true);
        spdlog::info("Patched PiP Frame & Camera");
    }

    if (!pattern_pip_frame.empty()) {
        auto pattern_pip_text = hook::pattern("08 00 00 D8 41");
        if (!pattern_pip_text.empty()) {
            uintptr_t addr_text = (uintptr_t)pattern_pip_text.get_first(1);
            float fCorrectedPiPText = fHUDOffsetX + 27.0f;
            injector::WriteMemory<float>(addr_text, fCorrectedPiPText, true);
            spdlog::info("Patched PiP Text");
        }
    }

    // Video Scaler
    uint32_t uvHex = 0x3ACCCCCD;
    for (uintptr_t i = start; i < end; i++) {
        if (*(uint32_t*)i == uvHex) {
            injector::WriteMemory(i, fVideoUVScale, true);
            for (int k = 0; k < 512; k++) {
                uintptr_t backAddr = i - k;
                if (*(uint32_t*)backAddr == 0x44200000) {
                    if (*(uint32_t*)(backAddr + 10) == 0x43F00000 ||
                        *(uint32_t*)(backAddr - 10) == 0x43F00000) {
                        injector::WriteMemory(backAddr, fVideoQuadWidth, true);
                    }
                }
            }
        }
    }
    spdlog::info("Patched Video Scaler");

    // 2D Unified Hook (Main HUD)
    auto pattern_hud = hook::pattern("68 00 00 F0 43 68 00 00 20 44 6A 00 6A 00");
    if (!pattern_hud.empty()) {
        uintptr_t matchAddr = (uintptr_t)pattern_hud.get_first(0);
        uintptr_t hookAddr = matchAddr + 5; // Skip PUSH 480

        jmpBack_2D_Unified = hookAddr + 9;

        injector::MakeJMP(hookAddr, Hook_HUD_Unified, true);
        injector::MakeNOP(hookAddr + 5, 4, true);

        spdlog::info("Hooked 2D HUD Logic at: {:X}", hookAddr);
    }
    else {
        spdlog::error("2D Hook Failed: Pattern not found.");
    }

    // Other overlay resolutions

    float fResX = (float)ResX;
    float fResY = (float)ResY;


    auto p_load_a = hook::pattern("C7 44 24 1C 00 00 20 44");
    if (!p_load_a.empty())
    {
        uintptr_t addr = (uintptr_t)p_load_a.get_first(0);
        injector::WriteMemory<float>(addr + 4, fResX, true);


        injector::WriteMemory<float>(addr + 8 + 4, fResY, true);

        spdlog::info("Fixed Resolutions at (00412d68) -> {:.0f}x{:.0f}", fResX, fResY);
    }


    auto p_load_b = hook::pattern("C7 44 24 08 00 00 20 44");
    if (!p_load_b.empty())
    {
        uintptr_t addr = (uintptr_t)p_load_b.get_first(0);
        injector::WriteMemory<float>(addr + 4, fResX, true);

        injector::WriteMemory<float>(addr + 8 + 4, fResY, true);

        spdlog::info("Fixed Resolutions at (0047fd0f) -> {:.0f}x{:.0f}", fResX, fResY);
    }

    auto p_overlay = hook::pattern("6A 00 68 00 00 F0 43 68 00 00 20 44");
    if (!p_overlay.empty())
    {
        uintptr_t addr = (uintptr_t)p_overlay.get_first(0);


        injector::WriteMemory<float>(addr + 3, fResY, true);


        injector::WriteMemory<float>(addr + 8, fResX, true);

        spdlog::info("Fixed Resolutions at -> {:.0f}x{:.0f}", fResX, fResY);
    }

    auto p_frontend = hook::pattern("53 68 00 00 F0 43 8B E9 68 00 00 20 44 53");
    if (!p_frontend.empty())
    {
        uintptr_t addr = (uintptr_t)p_frontend.get_first(0);

        injector::WriteMemory<float>(addr + 2, fResY, true);

        injector::WriteMemory<float>(addr + 9, fResX, true);

        spdlog::info("Fixed Resolutions at -> {:.0f}x{:.0f}", fResX, fResY);
    }

    // GetElement (FUN_0048e8b0) inside FieldRadar.
    // Pattern: E8 8C 09 FD FF (CALL 0048e8b0) near 004bdf1f
    auto pattern_radar = hook::pattern("E8 8C 09 FD FF");

    if (!pattern_radar.empty()) {
        uintptr_t matchAddr = (uintptr_t)pattern_radar.get_first(0);

        // Target = NextInstruction + RelativeOffset
        uintptr_t relativeOffset = *(uintptr_t*)(matchAddr + 1);
        uintptr_t nextInstruction = matchAddr + 5;
        GetElement_Original = (void*)(nextInstruction + relativeOffset);

        injector::MakeCALL(matchAddr, Hook_GetElement_Global, true);

        spdlog::info("Hooked Global GetElement at: {:X} | Original Func: {:X}", matchAddr, (uintptr_t)GetElement_Original);
    }
    else {
        spdlog::error("Field Radar Pattern not found!");
    }

    //Resolution Hack
    auto p1 = hook::pattern("81 F9 80 02 00 00 8B B7 08 03 00 00 75 0D");
    if (!p1.empty())
    {
        uintptr_t addr = (uintptr_t)p1.get_first(0);

        injector::WriteMemory<uint8_t>(addr + 12, 0xEB, true);
        injector::WriteMemory<uint8_t>(addr + 13, 0x64, true);

        spdlog::info("Resolution filter removed");
    }

    auto p2 = hook::pattern("C7 44 24 2C 40 06 00 00");
    if (!p2.empty())
    {
        uintptr_t addr = (uintptr_t)p2.get_first(0);

        injector::WriteMemory<uint16_t>(addr + 4, 0xFFFF, true);

        spdlog::info("Max resolution limit removed");
    }

    auto p3 = hook::pattern("C7 00 01 00 00 00 E8");
    if (!p3.empty())
    {
        uintptr_t addr = (uintptr_t)p3.get_first(0);

        injector::MakeNOP(addr, 6, true);

        spdlog::info("Bucket write disabled");
    }

    auto p4 = hook::pattern("39 5C 84 54 8D 44 84 54 0F 85");
    if (!p4.empty())
    {
        uintptr_t addr = (uintptr_t)p4.get_first(0);

        injector::MakeNOP(addr + 8, 6, true);

        spdlog::info("Bucket skip disabled");
    }
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        Init();
    }
    return TRUE;
}
