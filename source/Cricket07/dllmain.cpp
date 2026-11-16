#include "stdafx.h"
#include "WindowHooks.h"
#include "HUDHooks.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <tuple>

std::tuple<int32_t, int32_t> GetDesktopRes();

void InitLogger()
{
    auto logger = spdlog::basic_logger_mt("file_logger", "Cricket07WidescreenFix.log", true);
    logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
    spdlog::info("Logger Initialized Successfully.");
}

void Init()
{
    CIniReader iniReader("Cricket07WidescreenFix.ini");
    int ResX = iniReader.ReadInteger("MAIN", "ResX", 0);
    int ResY = iniReader.ReadInteger("MAIN", "ResY", 0);

    if (!ResX || !ResY)
        std::tie(ResX, ResY) = GetDesktopRes();

    int windowMode = iniReader.ReadInteger("MAIN", "WindowMode", 0);
    if (windowMode < 0 || windowMode > 2) windowMode = 0;

    InitWindowHooks(ResX, ResY, windowMode);
    InitHUDHooks(ResX, ResY);
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