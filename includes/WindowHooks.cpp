#include "stdafx.h"
#include "WindowHooks.h"
#include <spdlog/spdlog.h>
#include "injector/injector.hpp"
#include <tuple>

enum class DisplayMode : int {
    Fullscreen = 0,
    Windowed = 1,
    Borderless = 2
};

static DisplayMode g_DisplayMode = DisplayMode::Borderless;
static int g_TargetWidth = 0;
static int g_TargetHeight = 0;

HWND& g_hWnd = *(HWND*)0x009E452C;
DWORD& g_WindowModeFlag = *(DWORD*)0x009F2110;
WNDPROC g_OrigWndProc = nullptr;

// Fix z-fighting issues, fixes dgVoodoo2 issue when 3D accelerated card is selected 
int __fastcall Hook_FUN_00610290(void* thisPtr, void* edx, DWORD adapter, DWORD* devType, int* pDepthBits, int* pStencilBits, DWORD* pFormat) {
    if (pFormat) *pFormat = 75; // D3DFMT_D24S8
    if (pDepthBits) *pDepthBits = 24;
    if (pStencilBits) *pStencilBits = 8;
    return 1;
}

void CalcWindowRectAndStyle(DWORD& outStyle, DWORD& outExStyle, int& outX, int& outY, int& outW, int& outH) {
    if (g_DisplayMode == DisplayMode::Borderless) {
        outStyle = WS_POPUP | WS_VISIBLE;
        outExStyle = WS_EX_APPWINDOW;
        outX = 0; outY = 0;
        outW = GetSystemMetrics(SM_CXSCREEN);
        outH = GetSystemMetrics(SM_CYSCREEN);
    }
    else {
        outStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        outExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
        RECT workArea; SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
        int workW = workArea.right - workArea.left;
        int workH = workArea.bottom - workArea.top;
        RECT borderRc = { 0, 0, 0, 0 }; AdjustWindowRectEx(&borderRc, outStyle, FALSE, outExStyle);
        int frameW = borderRc.right - borderRc.left;
        int frameH = borderRc.bottom - borderRc.top;
        int clientW = g_TargetWidth; int clientH = g_TargetHeight;
        if (clientW + frameW > workW || clientH + frameH > workH) {
            float scaleW = (float)(workW - frameW) / (float)clientW;
            float scaleH = (float)(workH - frameH) / (float)clientH;
            float scale = (scaleW < scaleH) ? scaleW : scaleH;
            clientW = (int)(clientW * scale); clientH = (int)(clientH * scale);
        }
        outW = clientW + frameW; outH = clientH + frameH;
        outX = workArea.left + (workW - outW) / 2; outY = workArea.top + (workH - outH) / 2;
        if (outX < workArea.left) outX = workArea.left; if (outY < workArea.top) outY = workArea.top;
    }
}

void ApplyWindowStyle(HWND hWnd);

LRESULT CALLBACK CustomWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_DisplayMode != DisplayMode::Fullscreen) {
        switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && GetForegroundWindow() == hWnd) { SetCursor(NULL); return TRUE; }
            break;
        case WM_NCHITTEST:
            if (g_DisplayMode == DisplayMode::Windowed) return DefWindowProcA(hWnd, msg, wParam, lParam);
            break;
        case WM_SYSCOMMAND: {
            WPARAM cmd = wParam & 0xFFF0;
            if (cmd == SC_MOVE || cmd == SC_MINIMIZE || cmd == SC_RESTORE || cmd == SC_KEYMENU) return DefWindowProcA(hWnd, msg, wParam, lParam);
            break;
        }
        case WM_SYSKEYDOWN:
            if (wParam == VK_RETURN && (lParam & (1 << 29))) {
                g_DisplayMode = (g_DisplayMode == DisplayMode::Borderless) ? DisplayMode::Windowed : DisplayMode::Borderless;
                ApplyWindowStyle(hWnd);
                return 0;
            }
            break;
        case WM_ACTIVATEAPP:
            if (wParam == FALSE) { while (ShowCursor(TRUE) < 0); }
            else { SetCursor(NULL); }
            break;
        }
    }
    return CallWindowProcA(g_OrigWndProc, hWnd, msg, wParam, lParam);
}

void ApplyWindowStyle(HWND hWnd) {
    if (!hWnd || g_DisplayMode == DisplayMode::Fullscreen) return;
    DWORD style, exStyle; int x, y, w, h;
    CalcWindowRectAndStyle(style, exStyle, x, y, w, h);
    SetWindowLongA(hWnd, GWL_STYLE, style);
    SetWindowLongA(hWnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hWnd, HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    g_WindowModeFlag = 0;
}

HWND WINAPI Hook_CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    if (g_DisplayMode != DisplayMode::Fullscreen) CalcWindowRectAndStyle(dwStyle, dwExStyle, X, Y, nWidth, nHeight);
    HWND hWnd = CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hWnd && g_DisplayMode != DisplayMode::Fullscreen) {
        g_OrigWndProc = (WNDPROC)SetWindowLongA(hWnd, GWL_WNDPROC, (LONG)CustomWndProc);
        g_WindowModeFlag = 0;
    }
    return hWnd;
}

BOOL WINAPI Hook_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (g_DisplayMode != DisplayMode::Fullscreen) { ApplyWindowStyle(hWnd); return TRUE; }
    return SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

struct D3DPRESENT_PARAMETERS_DX8 {
    UINT BackBufferWidth; UINT BackBufferHeight; DWORD BackBufferFormat; UINT BackBufferCount;
    DWORD MultiSampleType; DWORD SwapEffect; HWND hDeviceWindow; BOOL Windowed;
    BOOL EnableAutoDepthStencil; DWORD AutoDepthStencilFormat; DWORD Flags;
    UINT FullScreen_RefreshRateInHz; UINT FullScreen_PresentationInterval;
};

typedef int(__fastcall* InitWindowed_t)(void* thisPtr);
InitWindowed_t Orig_FUN_00612be0 = (InitWindowed_t)0x00612BE0;
int __fastcall Call_FUN_00612be0(void* thisPtr) {
    if (thisPtr) {
        uintptr_t screenStruct = *(uintptr_t*)((uintptr_t)thisPtr + 0x0C);
        if (screenStruct) {
            *(int*)(screenStruct + 0x4C) = g_TargetWidth;
            *(int*)(screenStruct + 0x50) = g_TargetHeight;
        }
    }
    int res = Orig_FUN_00612be0(thisPtr);
    if (thisPtr) {
        uintptr_t screenStruct = *(uintptr_t*)((uintptr_t)thisPtr + 0x0C);
        if (screenStruct) {
            *(int*)(screenStruct + 0x4C) = g_TargetWidth;
            *(int*)(screenStruct + 0x50) = g_TargetHeight;
        }
        *(int*)((uintptr_t)thisPtr + 0xB4) = 2;
    }
    return res;
}

typedef int(__fastcall* InitDevice_t)(void* thisPtr, void* edx, int skipCapsCheck);
typedef int(__fastcall* ResetDevice_t)(void* thisPtr);
InitDevice_t Orig_FUN_00612570 = (InitDevice_t)0x00612570;
ResetDevice_t Orig_FUN_00612190 = (ResetDevice_t)0x00612190;

void SanitizePresentParams() {
    auto* pp = (D3DPRESENT_PARAMETERS_DX8*)0x009E454C;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->SwapEffect = 1;
    pp->BackBufferWidth = g_TargetWidth;
    pp->BackBufferHeight = g_TargetHeight;
    pp->BackBufferFormat = 22;
    pp->EnableAutoDepthStencil = TRUE;
    pp->AutoDepthStencilFormat = 77;
}

int __fastcall Call_FUN_00612570(void* thisPtr, void* edx, int skipCapsCheck) {
    SanitizePresentParams();
    int hr = Orig_FUN_00612570(thisPtr, edx, skipCapsCheck);
    *(BYTE*)0x009E4BCD = 1;
    return hr;
}

int __fastcall Call_FUN_00612190(void* thisPtr) {
    SanitizePresentParams();
    int hr = Orig_FUN_00612190(thisPtr);
    *(BYTE*)0x009E4BCD = 1;
    ApplyWindowStyle(g_hWnd);
    return hr;
}

void InitWindowHooks(int resX, int resY, int windowMode) {
    g_TargetWidth = resX;
    g_TargetHeight = resY;
    g_DisplayMode = static_cast<DisplayMode>(windowMode);

    injector::MakeJMP(0x00610290, Hook_FUN_00610290, true);

    if (g_DisplayMode != DisplayMode::Fullscreen) {
        injector::MakeNOP(0x0061276A, 2, true);
        injector::WriteMemory<uint8_t>(0x00613BF6, 0xB8, true);
        injector::WriteMemory<uint32_t>(0x00613BF7, 1, true);
        injector::WriteMemory<uint8_t>(0x00613BFB, 0x90, true);

        injector::MakeCALL(0x00613C15, Call_FUN_00612be0, true);

        injector::WriteMemory<uint8_t>(0x00612613, 0x40, true);
        injector::WriteMemory<uint8_t>(0x0061269F, 0x40, true);

        injector::MakeCALL(0x00612D24, Hook_CreateWindowExA, true);
        injector::MakeNOP(0x00612D29, 1, true);

        injector::MakeCALL(0x00612D9B, Hook_SetWindowPos, true);
        injector::MakeNOP(0x00612DA0, 1, true);

        injector::MakeCALL(0x00612DDE, Call_FUN_00612190, true);
        injector::MakeCALL(0x00612DE7, Call_FUN_00612570, true);

        spdlog::info("Applied Windowed/Borderless patches (WindowMode = {})", windowMode);
    }
}
