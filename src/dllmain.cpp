#include "stdafx.h"
#include "helper.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>

#define spdlog_confparse(var) spdlog::info("Config Parse: {}: {}", #var, var)

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Fix details
std::string sFixName = "FateSamuraiRemnantFix";
std::string sFixVersion = "0.0.1";
std::filesystem::path sFixPath;

// Ini
inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";

// Logger
std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path sExePath;
std::string sExeName;

// Aspect ratio / FOV / HUD
std::pair DesktopDimensions = { 0,0 };
const float fPi = 3.1415926535f;
const float fNativeAspect = 1.7777778f;
float fAspectRatio;
float fAspectMultiplier;
float fHUDWidth;
float fHUDWidthOffset;
float fHUDHeight;
float fHUDHeightOffset;

// Ini variables
bool bCustomRes;
int iCustomResX;
int iCustomResY;
bool bFixHUD;

// Variables
int iCurrentResX;
int iCurrentResY;
std::string sHUDObjectName;
short iHUDObjectX;
short iHUDObjectY;
std::uint8_t* MovieCapturePlane = nullptr;

void Logging()
{
    // Get path to DLL
    WCHAR dllPath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    sFixPath = dllPath;
    sFixPath = sFixPath.remove_filename();

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(exeModule, exePath, MAX_PATH);
    sExePath = exePath;
    sExeName = sExePath.filename().string();
    sExePath = sExePath.remove_filename();

    // Spdlog initialisation
    try {
        logger = spdlog::basic_logger_st(sFixName.c_str(), sExePath.string() + sLogFile, true);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::debug);

        spdlog::info("----------");
        spdlog::info("{:s} v{:s} loaded.", sFixName.c_str(), sFixVersion.c_str());
        spdlog::info("----------");
        spdlog::info("Log file: {}", sFixPath.string() + sLogFile);
        spdlog::info("----------");
        spdlog::info("Module Name: {0:s}", sExeName.c_str());
        spdlog::info("Module Path: {0:s}", sExePath.string());
        spdlog::info("Module Address: 0x{0:x}", (uintptr_t)exeModule);
        spdlog::info("Module Timestamp: {0:d}", Memory::ModuleTimestamp(exeModule));
        spdlog::info("----------");
    }
    catch (const spdlog::spdlog_ex& ex) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "Log initialisation failed: " << ex.what() << std::endl;
        FreeLibraryAndExitThread(thisModule, 1);
    }  
}

void Configuration()
{
    // Inipp initialisation
    std::ifstream iniFile(sFixPath / sConfigFile);
    if (!iniFile) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "" << sFixName.c_str() << " v" << sFixVersion.c_str() << " loaded." << std::endl;
        std::cout << "ERROR: Could not locate config file." << std::endl;
        std::cout << "ERROR: Make sure " << sConfigFile.c_str() << " is located in " << sFixPath.string().c_str() << std::endl;
        spdlog::error("ERROR: Could not locate config file {}", sConfigFile);
        spdlog::shutdown();
        FreeLibraryAndExitThread(thisModule, 1);
    }
    else {
        spdlog::info("Config file: {}", sFixPath.string() + sConfigFile);
        ini.parse(iniFile);
    }

    // Parse config
    ini.strip_trailing_comments();
    spdlog::info("----------");

    // Load settings from ini
    inipp::get_value(ini.sections["Custom Resolution"], "Enabled", bCustomRes);
    inipp::get_value(ini.sections["Custom Resolution"], "Width", iCustomResX);
    inipp::get_value(ini.sections["Custom Resolution"], "Height", iCustomResY);
    inipp::get_value(ini.sections["Fix HUD"], "Enabled", bFixHUD);

    // Log ini parse
    spdlog_confparse(bCustomRes);
    spdlog_confparse(iCustomResX);
    spdlog_confparse(iCustomResY);
    spdlog_confparse(bFixHUD);

    spdlog::info("----------");
}

void CalculateAspectRatio(bool bLog)
{
    // Check if resolution is invalid
    if (iCurrentResX <= 0 || iCurrentResY <= 0)
        return;

    // Calculate aspect ratio
    fAspectRatio = (float)iCurrentResX / (float)iCurrentResY;
    fAspectMultiplier = fAspectRatio / fNativeAspect;

    // HUD 
    fHUDWidth = (float)iCurrentResY * fNativeAspect;
    fHUDHeight = (float)iCurrentResY;
    fHUDWidthOffset = (float)(iCurrentResX - fHUDWidth) / 2.00f;
    fHUDHeightOffset = 0.00f;
    if (fAspectRatio < fNativeAspect) {
        fHUDWidth = (float)iCurrentResX;
        fHUDHeight = (float)iCurrentResX / fNativeAspect;
        fHUDWidthOffset = 0.00f;
        fHUDHeightOffset = (float)(iCurrentResY - fHUDHeight) / 2.00f;
    }

    // Log details about current resolution
    if (bLog) {
        spdlog::info("----------");
        spdlog::info("Current Resolution: Resolution: {:d}x{:d}", iCurrentResX, iCurrentResY);
        spdlog::info("Current Resolution: fAspectRatio: {}", fAspectRatio);
        spdlog::info("Current Resolution: fAspectMultiplier: {}", fAspectMultiplier);
        spdlog::info("Current Resolution: fHUDWidth: {}", fHUDWidth);
        spdlog::info("Current Resolution: fHUDHeight: {}", fHUDHeight);
        spdlog::info("Current Resolution: fHUDWidthOffset: {}", fHUDWidthOffset);
        spdlog::info("Current Resolution: fHUDHeightOffset: {}", fHUDHeightOffset);
        spdlog::info("----------");
    }
}

void Resolution()
{
    // Grab desktop resolution
    DesktopDimensions = Util::GetPhysicalDesktopDimensions();

    if (bCustomRes) {
        // Set custom resolution as desktop resolution if set to 0 or invalid
        if (iCustomResX <= 0 || iCustomResY <= 0) {
            iCustomResX = DesktopDimensions.first;
            iCustomResY = DesktopDimensions.second;
        }

        // Resolution lists
        std::uint8_t* ResolutionListScanResult = Memory::PatternScan(exeModule, "C0 03 00 00 00 04 00 00 60 04 00 00 00 05 00 00");
        if (ResolutionListScanResult) {
            spdlog::info("Resolution List: Address is {:s}+{:x}", sExeName.c_str(), ResolutionListScanResult - (std::uint8_t*)exeModule);

            // Overwrite 3840x2160
            Memory::Write(ResolutionListScanResult + 0x24, iCustomResX);
            Memory::Write(ResolutionListScanResult + 0x54, iCustomResY);
        }
        else {
            spdlog::error("Resolution List: Pattern scan failed.");
        }

        // Resolution string
        std::uint8_t* ResolutionStringScanResult = Memory::PatternScan(exeModule, "48 8B ?? 45 33 ?? 4D ?? ?? 49 ?? ?? 41 ?? ?? ?? E8 ?? ?? ?? ?? 45 33 ??");
        if (ResolutionStringScanResult) {
            spdlog::info("Resolution String: Address is {:s}+{:x}", sExeName.c_str(), ResolutionStringScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid ResolutionStringMidHook{};
            ResolutionStringMidHook = safetyhook::create_mid(ResolutionStringScanResult,
                [](SafetyHookContext& ctx) {
                    const std::string oldRes = "3840x2160";
                    std::string newRes = std::to_string(iCustomResX) + "x" + std::to_string(iCustomResY);

                    char* currentString = (char*)ctx.rax;
                    if (strncmp(currentString, oldRes.c_str(), oldRes.size()) == 0) {
                        if (newRes.size() <= oldRes.size()) {
                            std::memcpy(currentString, newRes.c_str(), newRes.size() + 1);
                            spdlog::info("Resolution String: Replaced 3840x2160 with {}", newRes);
                        }
                    }
                });
        }
        else {
            spdlog::error("Resolution String: Pattern scan failed.");
        }
    }
}

void HUD()
{
    if (bFixHUD) {
        // HUD Size 
        std::uint8_t* HUDSizeScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? 48 83 ?? ?? E8 ?? ?? ?? ??");
        std::uint8_t* StartupHUDSizeScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? E8 ?? ?? ?? ?? 48 8B ?? E8 ?? ?? ?? ??");
        if (HUDSizeScanResult) {
            spdlog::info("HUD: Size: Address is {:s}+{:x}", sExeName.c_str(), HUDSizeScanResult - (std::uint8_t*)exeModule);
            spdlog::info("HUD: Size: Startup: Address is {:s}+{:x}", sExeName.c_str(), StartupHUDSizeScanResult - (std::uint8_t*)exeModule);

            // Get HUD X and HUD Y
            static std::uint8_t* HUDSizeXAddr = Memory::GetAbsolute(StartupHUDSizeScanResult + 0xC);
            static std::uint8_t* HUDSizeYAddr = Memory::GetAbsolute(StartupHUDSizeScanResult + 0x4);

            auto HUDSizeMidHook = [](SafetyHookContext& ctx) {
                // Write defaults
                Memory::Write(HUDSizeXAddr, 1920.00f);
                Memory::Write(HUDSizeYAddr, 1080.00f);

                ctx.xmm6.f32[0] = 1080.00f;
                ctx.xmm7.f32[0] = 1920.00f;

                int iResX = static_cast<int>(ctx.xmm0.f32[0]);
                int iResY = static_cast<int>(ctx.xmm1.f32[0]);

                // Log resolution
                if (iResX != iCurrentResX || iResY != iCurrentResY) {
                    iCurrentResX = iResX;
                    iCurrentResY = iResY;
                    CalculateAspectRatio(true);
                }

                if (fAspectRatio > fNativeAspect) {
                    Memory::Write(HUDSizeXAddr, 1080.00f * fAspectRatio);
                    Memory::Write(HUDSizeYAddr, 1080.00f);

                    ctx.xmm7.f32[0] = 1080.00f * fAspectRatio;
                    ctx.xmm6.f32[0] = 1080.00f;
                }
                else if (fAspectRatio < fNativeAspect) {
                    Memory::Write(HUDSizeXAddr, 1920.00f);
                    Memory::Write(HUDSizeYAddr, 1920.00f / fAspectRatio);

                    ctx.xmm7.f32[0] = 1920.00f;
                    ctx.xmm6.f32[0] = 1920.00f / fAspectRatio;
                }
                };

            // Apply hooks
            static SafetyHookMid HUDSizeHook = safetyhook::create_mid(HUDSizeScanResult, HUDSizeMidHook);
            static SafetyHookMid StartupHUDSizeHook = safetyhook::create_mid(StartupHUDSizeScanResult, HUDSizeMidHook);
        }
        else {
            spdlog::error("HUD: Size: Pattern scan failed.");
        }

        // Health Bars 
        std::uint8_t* HealthBars1ScanResult = Memory::PatternScan(exeModule, "0F 29 ?? ?? ?? 48 8B ?? ?? ?? 48 83 ?? ?? 74 ?? F3 0F ?? ?? ?? ?? ?? ?? 0F ?? ?? 77 ??");
        std::uint8_t* HealthBars2ScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? F3 0F ?? ?? 66 0F ?? ?? ?? 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? E8 ?? ?? ?? ?? 84 ?? 74 ??");
        if (HealthBars1ScanResult && HealthBars2ScanResult) {
            spdlog::info("HUD: Health Bars: 1: Address is {:s}+{:x}", sExeName.c_str(), HealthBars1ScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid HealthBars1MidHook{};
            HealthBars1MidHook = safetyhook::create_mid(HealthBars1ScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect)
                        ctx.xmm6.f32[0] = 1920.00f;
                    else if (fAspectRatio < fNativeAspect)
                        ctx.xmm5.f32[0] = 1080.00f;
                });

            spdlog::info("HUD: Health Bars: 2: Address is {:s}+{:x}", sExeName.c_str(), HealthBars2ScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid HealthBars2MidHook{};
            HealthBars2MidHook = safetyhook::create_mid(HealthBars2ScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect)
                        ctx.xmm3.f32[0] += ((1080.00f * fAspectRatio) - 1920.00f) / 2.00f;
                    else if (fAspectRatio < fNativeAspect)
                        ctx.xmm4.f32[0] += ((1920.00f / fAspectRatio) - 1080.00f) / 2.00f;
                });
        }
        else {
            spdlog::error("HUD: Health Bars: Pattern scan(s) failed.");
        }

        // HUD Objects
        std::uint8_t* HUDObjectsScanResult = Memory::PatternScan(exeModule, "41 8B ?? ?? ?? F3 0F ?? ?? ?? ?? ?? ?? 41 ?? 01 00 00 00 89 ?? ?? ?? ?? ?? ??");
        if (HUDObjectsScanResult) {
            static int iCapCount = 0;

            spdlog::info("HUD: Objects: Address is {:s}+{:x}", sExeName.c_str(), HUDObjectsScanResult - (std::uint8_t*)exeModule);
            static SafetyHookMid HUDObjectsMidHook{};
            HUDObjectsMidHook = safetyhook::create_mid(HUDObjectsScanResult + 0x5,
                [](SafetyHookContext& ctx) {
                    if (ctx.r12) {
                        sHUDObjectName = (char*)ctx.r12;
                        iHUDObjectX = *reinterpret_cast<short*>(ctx.r12 + 0x60);
                        iHUDObjectY = *reinterpret_cast<short*>(ctx.r12 + 0x62);

                        // Grab capture plane for movies, luckily it's always the first one
                        if (sHUDObjectName.contains("capture_plane_full_rgba8") && !MovieCapturePlane)
                            MovieCapturePlane = (std::uint8_t*)ctx.r12;

                        // Non-movie capture planes
                        if (sHUDObjectName.contains("capture_plane_full_rgba8") && (std::uint8_t*)ctx.r12 != MovieCapturePlane) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Capture Plane: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(iHUDObjectY * fAspectRatio);
                            }
                            else if (fAspectRatio < fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>((short)ceilf(iHUDObjectX / fAspectRatio)) << 16) | iHUDObjectX;
                            }
                        }

                        // Damage frame
                        if (sHUDObjectName.contains("PIC_bg_frame_damage_")) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Damage Frame: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (sHUDObjectName.contains("PIC_bg_frame_damage_l")) {
                                if (fAspectRatio > fNativeAspect) {
                                    ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(540.00f * fAspectRatio);
                                    *reinterpret_cast<float*>(ctx.r12 + 0x50) = -ceilf(540.00f * fAspectRatio);
                                }
                            }
                            else if (sHUDObjectName.contains("PIC_bg_frame_damage_r")) {
                                if (fAspectRatio > fNativeAspect) {
                                    ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(540.00f * fAspectRatio);
                                    *reinterpret_cast<float*>(ctx.r12 + 0x50) = ceilf(540.00f * fAspectRatio);
                                }
                            }
                        }

                        // Pause menu
                        if (sHUDObjectName.contains("PIC_common_square_bl") || sHUDObjectName.contains("PIC_parts_header_bg_tab")) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Pause Menu: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(iHUDObjectX * fAspectMultiplier);
                            }
                        }

                        // Base background
                        if (iHUDObjectX == 2600 && sHUDObjectName.contains("WIN_base_system_bg")) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Base BG: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(1463 * fAspectRatio);
                            }
                            else if (fAspectRatio < fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>((short)ceilf(iHUDObjectX / fAspectRatio)) << 16) | iHUDObjectX;
                            }
                        }

                        // Fades/masks
                        if ((sHUDObjectName.contains("PIC_bg_rect_window") || sHUDObjectName.contains("PIC_mask_bg") || sHUDObjectName.contains("PIC_square_w") || sHUDObjectName.contains("PIC_black")) && iHUDObjectX >= 1920 && iHUDObjectY >= 1080) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Fades: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(iHUDObjectX * fAspectMultiplier);
                            }
                            else if (fAspectRatio < fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>((short)ceilf(iHUDObjectX / fAspectRatio) << 16) | iHUDObjectX);
                            }
                        }

                        // Menu letterboxing
                        if (iHUDObjectX == 1920 && sHUDObjectName.contains("PIC_square_w")) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Menu Letterboxing: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(iHUDObjectX * fAspectMultiplier);
                            }
                        }

                        // Cutscene letterboxing
                        if (sHUDObjectName.contains("letterbox") && iHUDObjectX >= 1920) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Letterboxing: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(iHUDObjectX * fAspectMultiplier);
                            }
                        }

                        // Gradient background
                        if (iHUDObjectX == 2880 && sHUDObjectName.contains("PIC_bottomGradation")) {
                            #ifdef _DEBUG
                            spdlog::info("HUD Objects: Gradient Background: sHUDObjectName = {:x} - {} - {}x{}", ctx.r12, sHUDObjectName, iHUDObjectX, iHUDObjectY);
                            #endif
                            if (fAspectRatio > fNativeAspect) {
                                ctx.rax = (static_cast<uintptr_t>(iHUDObjectY) << 16) | (short)ceilf(1620 * fAspectRatio);
                            }
                        }
                    }
                });
        }
        else {
            spdlog::error("HUD Objects: Pattern scan failed.");
        }
    }
}

std::mutex mainThreadFinishedMutex;
std::condition_variable mainThreadFinishedVar;
bool mainThreadFinished = false;

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    Resolution();
    HUD();

    {
        std::lock_guard lock(mainThreadFinishedMutex);
        mainThreadFinished = true;
        mainThreadFinishedVar.notify_all();
    }

    return true;
}

std::mutex multiByteToWideCharHookMutex;
bool multiByteToWideCharHookCalled = false;
int(__stdcall* MultiByteToWideChar_Fn)(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);

int __stdcall MultiByteToWideChar_Hook(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
{
    {
        std::lock_guard lock(multiByteToWideCharHookMutex);
        if (!multiByteToWideCharHookCalled)
        {
            multiByteToWideCharHookCalled = true;
            Memory::HookIAT(exeModule, "KERNEL32.dll", MultiByteToWideChar_Hook, MultiByteToWideChar_Fn);

            if (!mainThreadFinished)
            {
                std::unique_lock finishedLock(mainThreadFinishedMutex);
                mainThreadFinishedVar.wait(finishedLock, [] { return mainThreadFinished; });
            }
        }
    }

    return MultiByteToWideChar_Fn(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        thisModule = hModule;

        HMODULE kernel32 = GetModuleHandleA("KERNEL32.dll");
        if (kernel32)
        {
            MultiByteToWideChar_Fn = decltype(MultiByteToWideChar_Fn)(GetProcAddress(kernel32, "MultiByteToWideChar"));
            Memory::HookIAT(exeModule, "KERNEL32.dll", MultiByteToWideChar_Fn, MultiByteToWideChar_Hook);
        }

        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle) {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
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