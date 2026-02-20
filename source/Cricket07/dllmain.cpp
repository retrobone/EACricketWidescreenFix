//  CRICKET 07 WIDESCREEN FIX

#include "stdafx.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <psapi.h>
#include <tuple>

#include "injector/injector.hpp"
#include "injector/assembly.hpp"
#include "injector/hooking.hpp"
#include "Hooking.Patterns.h"

using namespace injector;

// GLOBAL VARIABLES

// HUD & Aspect Ratio Variables
float fHUDWidth = 640.0f;
float fHUDHeight = 480.0f;
float fHUDOffsetX = 0.0f;
float fAspectRatio = 1.3333333f;


// Video Logic
float fVideoQuadWidth = 640.00f;
float fVideoUVScale = 0.0015625f;

// Field Radar & UI Globals
float fRadarAnchorX = 486.0f;
void* GetElement_Original = nullptr;

DWORD jmpBack_2D_Unified = 0;

// ASSEMBLY HOOK

// Unified 2D HUD Hook
void __declspec(naked) Hook_HUD_Unified() {
    __asm {
        push[fHUDWidth]      // Width
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

void InitLogger()
{
    // Create a file logger that writes to "Cricket07WidescreenFix.log"
    auto logger = spdlog::basic_logger_mt("file_logger", "Cricket07WidescreenFix.log", true);

    logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");

    spdlog::set_default_logger(logger);

    spdlog::set_level(spdlog::level::info);

    spdlog::flush_on(spdlog::level::info);

    spdlog::info("Logger Initialized Successfully.");
}

void Init()
{
    // Resolution & Aspect Ratio Detection
    CIniReader iniReader("Cricket07WidescreenFix.ini");
    int ResX = iniReader.ReadInteger("MAIN", "ResX", 0);
    int ResY = iniReader.ReadInteger("MAIN", "ResY", 0);

    if (!ResX || !ResY)
        std::tie(ResX, ResY) = GetDesktopRes();

    float fResX = (float)ResX;
    float fResY = (float)ResY;

    float aspectRatio = fResX / fResY;

    spdlog::info("Widescreen Fix Init: {:.2f} ({}x{})", aspectRatio, ResX, ResY);


    // Standard (4:3) Fallback
    if (aspectRatio < 1.334f) {
        spdlog::info("Standard 4:3 detected. Skipping patches.");
        return;
    }

    MODULEINFO modInfo = { 0 };
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &modInfo, sizeof(MODULEINFO));
    uintptr_t start = (uintptr_t)modInfo.lpBaseOfDll;
    uintptr_t end = start + modInfo.SizeOfImage;

    // CALCULATE VALUES AS PER RESOLUTION

    // Main HUD
    fHUDWidth = 480.0f * aspectRatio;
    fHUDOffsetX = (640.0f - fHUDWidth) / 2.0f;

    fVideoQuadWidth = fHUDWidth;
    fVideoUVScale = fHUDWidth / 409920.00f;

    // Picture-in-Picture
    float fPiPCam = fHUDOffsetX + 45.0f;
    float fPiPFrame = fHUDOffsetX + 26.0f;

    // Throw Meter
    static float fThrowMeter = 25.0f;
    fThrowMeter = 25.0f + fHUDOffsetX;

    // Field Radar
    float fRadarShift = fHUDOffsetX * -1.0f;
    fRadarAnchorX = 486.0f + fRadarShift;
    spdlog::info("Radar Fix: Anchor(50) -> {:.2f}", fRadarAnchorX);

    // APPLY PATCHES

    // 3D Aspect Ratio
    auto pattern_ar = hook::pattern("C7 44 24 6C AB AA AA 3F");
    if (!pattern_ar.empty()) {
        uintptr_t addr_ar1 = (uintptr_t)pattern_ar.get_first(4);
        injector::WriteMemory(addr_ar1, aspectRatio, true);
        spdlog::info("Patched 1st address of Aspect Ratio");
    }

    auto pattern_ar_1 = hook::pattern("C7 41 0C AB AA AA 3F");
    if (!pattern_ar_1.empty()) {
        uintptr_t addr_ar2 = (uintptr_t)pattern_ar_1.get_first(3);
        injector::WriteMemory(addr_ar2, aspectRatio, true);
        spdlog::info("Patched 2nd address of Aspect Ratio");
    }

    // Unlock Mouse Limits
    
        // Mouse X Limit
        auto mouse_limit_x = hook::pattern("dd d8 c7 46 08 00 00 18 44");
        if (!mouse_limit_x.empty()) {
            injector::WriteMemory<float>(0x00794210, fHUDWidth, true);
            injector::WriteMemory<float>(mouse_limit_x.get_first(5), fHUDWidth, true);
            spdlog::info("Increased horizontal mouse limit");
        }
    
        // Mouse Y Limit
        auto mouse_limit_y = hook::pattern("dd d8 c7 46 0c 00 00 e0 43");
        if (!mouse_limit_y.empty()) {
            injector::WriteMemory<float>(mouse_limit_y.get_first(5), fHUDHeight, true);
            injector::WriteMemory<float>(0x00794214, fHUDHeight, true);
            spdlog::info("Increased vertical mouse limit");
        }

        // Remove left side limit for mouse
        auto mouse_limit_x_left = hook::pattern("7a 07 c7 46 08 00 00 00 00");
        if (!mouse_limit_x_left.empty()) {
            injector::MakeNOP(mouse_limit_x_left.get_first(2), 7, true);
            spdlog::info("Unlocked left side for mouse");
        }

    // PiP Frame & Camera
    auto pattern_pip_cam = hook::pattern("34 00 00 34 42");
    auto pattern_pip_frame = hook::pattern("BD 00 00 D0 41");

    if (!pattern_pip_cam.empty()) {
        uintptr_t addrCam = (uintptr_t)pattern_pip_cam.get_first(1);
        uintptr_t addrFrame = (uintptr_t)pattern_pip_frame.get_first(1);

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

    // Throw Meter

    auto pattern_field_circle = hook::pattern("D8 05 ? ? ? ? 83 C4 08 D9 5F 08 D9 44");

    if (!pattern_field_circle.empty()) {
        uintptr_t instruction_operand = (uintptr_t)pattern_field_circle.get_first(2);

        injector::WriteMemory(instruction_operand, &fThrowMeter, true);

        spdlog::info("Patched throw meter position. Value: {:.2f}", fThrowMeter);
    }
    else {
        spdlog::error("Throw meter x-pos not found!");
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

    // Hook for HUD
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

    // Window resolution at startup

    auto w_addr = hook::pattern("c7 45 4c 80 02 00 00 c7 45 50 e0 01 00 00");
    if (!w_addr.empty())
    {
        injector::WriteMemory<int>(w_addr.get_first(3), fResX, true);

        injector::WriteMemory<int>(w_addr.get_first(10), fResY, true);

        spdlog::info("Fixed Window Resolution at Startup", fResX, fResY);
    }

    // Other overlay resolutions

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

    // Resolution Hack
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

    // Override Default Resolution 

    auto pattern_res_force = hook::pattern("BF 80 02 00 00 89 78 30");

    if (!pattern_res_force.empty()) {
        uintptr_t addr = (uintptr_t)pattern_res_force.get_first(0);

        injector::WriteMemory<int>(addr + 1, ResX, true);

        injector::WriteMemory<int>(addr + 14, ResY, true);

        spdlog::info("Patched Internal Res Force at {:X} -> {}x{}", addr, ResX, ResY);
    }
    else {
        spdlog::error("Internal Resolution Pattern not found!");
    }
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {

        InitLogger();
        Init();
    }
    return TRUE;
}
