#pragma once
#include <windows.h>

enum class DisplayMode : int {
    Fullscreen = 0,
    Windowed = 1,
    Borderless = 2
};

// Core Globals
extern DisplayMode g_DisplayMode;
extern int g_TargetWidth;
extern int g_TargetHeight;

// Shared HUD Globals (Needed by both dllmain and HUD hooks)
extern float fHUDWidth;
extern float fHUDHeight;
extern float fHUDOffsetX;