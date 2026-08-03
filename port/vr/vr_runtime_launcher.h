// vr_runtime_launcher.h
// Automatic detection and launch of the default OpenXR runtime (Windows)
// Compatible with the vr_openxr.cpp file of Perfect Dark PC port
// Supports: SteamVR, Meta/Oculus Link, Virtual Desktop (VDXR)
// Author: generated for Alex Le Tux (Updated)

#pragma once
#ifndef ANDROID // If PC

#include <windows.h>
#include <string>
#include <shellapi.h>
#include <tlhelp32.h>
#include "vr_log.h"
#include "vr_openxr.h"

// ─────────────────────────────────────────────────────────────────────────────
// TYPES
// ─────────────────────────────────────────────────────────────────────────────

enum class XrDefaultRuntime {
    Unknown,
    SteamVR,
    MetaOculusLink,
    VirtualDesktop,
    Other
};

// ─────────────────────────────────────────────────────────────────────────────
// DEFAULT RUNTIME DETECTION
// Reads HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime (standard Khronos OpenXR)
// ─────────────────────────────────────────────────────────────────────────────

static std::string vrGetActiveRuntimeJsonPath() {
    const char* KEY = "SOFTWARE\\Khronos\\OpenXR\\1";
    HKEY hKey;

    REGSAM views[] = { KEY_READ | KEY_WOW64_64KEY, KEY_READ | KEY_WOW64_32KEY };
    for (REGSAM view : views) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, KEY, 0, view, &hKey) == ERROR_SUCCESS) {
            char path[MAX_PATH] = {};
            DWORD size = sizeof(path);
            DWORD type = REG_SZ;
            LSTATUS st = RegQueryValueExA(hKey, "ActiveRuntime", nullptr, &type, (LPBYTE)path, &size);
            RegCloseKey(hKey);
            if (st == ERROR_SUCCESS && strlen(path) > 0) {
                return std::string(path);
            }
        }
    }
    return "";
}

static XrDefaultRuntime vrIdentifyRuntime(const std::string& jsonPath) {
    if (jsonPath.empty()) return XrDefaultRuntime::Unknown;

    std::string lower = jsonPath;
    for (char& c : lower) c = (char)tolower((unsigned char)c);

    if (lower.find("steamvr") != std::string::npos || lower.find("steam") != std::string::npos)
        return XrDefaultRuntime::SteamVR;

    if (lower.find("oculus") != std::string::npos || lower.find("meta") != std::string::npos)
        return XrDefaultRuntime::MetaOculusLink;

    if (lower.find("virtualdesktop") != std::string::npos || lower.find("vdxr") != std::string::npos)
        return XrDefaultRuntime::VirtualDesktop;

    return XrDefaultRuntime::Other;
}

// ─────────────────────────────────────────────────────────────────────────────
// PROBE: tests if the runtime responds WITHOUT leaving an open instance
// ─────────────────────────────────────────────────────────────────────────────

static bool vrIsRuntimeAndHMDReady(void) {
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    strncpy(ci.applicationInfo.applicationName, "PDprobe", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 0;
    ci.enabledExtensionNames = nullptr;

    XrInstance probe = XR_NULL_HANDLE;
    XrResult r = xrCreateInstance(&ci, &probe);
    if (XR_FAILED(r) || probe == XR_NULL_HANDLE) {
        return false;
    }

    XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sysId = XR_NULL_SYSTEM_ID;
    XrResult rs = xrGetSystem(probe, &sysInfo, &sysId);
    xrDestroyInstance(probe);

    if (XR_FAILED(rs)) {
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAUNCH STEAMVR
// Strategy:
// 1. Direct vrstartup.exe (Bypasses the Steam "Already running" bug)
// 2. Steam URI
// 3. Steam exe fallback
// ─────────────────────────────────────────────────────────────────────────────

static std::string vrGetSteamInstallPath() {
    const char* keys[] = {
        "SOFTWARE\\WOW6432Node\\Valve\\Steam",
        "SOFTWARE\\Valve\\Steam"
    };
    for (const char* k : keys) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char path[MAX_PATH] = {};
            DWORD size = sizeof(path);
            DWORD type = REG_SZ;
            if (RegQueryValueExA(hKey, "InstallPath", nullptr, &type, (LPBYTE)path, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(path);
            }
            RegCloseKey(hKey);
        }
    }
    return "";
}

static bool vrLaunchSteamVR(const std::string& activeRuntimeJson) {
    LOGI("[XR Launcher] Launching SteamVR...");

    // Method 1: Direct vrstartup.exe via OpenXR JSON path
    // Prevents the "game already running" Steam bug if Steam is closed.
    if (!activeRuntimeJson.empty()) {
        std::string lowerPath = activeRuntimeJson;
        for (char& c : lowerPath) c = (char)tolower((unsigned char)c);

        size_t pos = lowerPath.find("steamvr");
        if (pos != std::string::npos) {
            // Extract the base SteamVR path from the JSON path
            std::string basePath = activeRuntimeJson.substr(0, pos + 7); // 7 = len("steamvr")
            std::string vrStartupExe = basePath + "\\bin\\win64\\vrstartup.exe";

            if (GetFileAttributesA(vrStartupExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                LOGI("[XR Launcher] Found vrstartup.exe: %s", vrStartupExe.c_str());
                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi = {};
                std::string cmdLine = "\"" + vrStartupExe + "\"";
                if (CreateProcessA(nullptr, (LPSTR)cmdLine.c_str(), nullptr, nullptr,
                                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    LOGI("[XR Launcher] SteamVR launched via native vrstartup.exe");
                    return true;
                }
            }
        }
    }

    // Method 2: Steam URI (App ID 250820 = SteamVR)
    HINSTANCE hi = ShellExecuteA(nullptr, "open", "steam://run/250820", nullptr, nullptr, SW_HIDE);
    if ((INT_PTR)hi > 32) {
        LOGI("[XR Launcher] SteamVR launched via steam://run/250820 URI");
        return true;
    }

    // Method 3: Direct Steam exe from registry
    std::string steamPath = vrGetSteamInstallPath();
    if (steamPath.empty()) return false;

    std::string steamExe = steamPath + "\\steam.exe";
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = "\"" + steamExe + "\" -applaunch 250820";
    if (CreateProcessA(nullptr, (LPSTR)cmdLine.c_str(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOGI("[XR Launcher] Steam launched with -applaunch 250820");
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAUNCH META / OCULUS LINK
// ─────────────────────────────────────────────────────────────────────────────

static bool vrStartOculusService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    const char* serviceNames[] = { "OVRService", "OVRServiceLauncher", "Meta.XrRuntime" };

    for (const char* name : serviceNames) {
        SC_HANDLE svc = OpenServiceA(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
        if (!svc) continue;

        SERVICE_STATUS status = {};
        QueryServiceStatus(svc, &status);

        if (status.dwCurrentState == SERVICE_RUNNING) {
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return true;
        }

        BOOL ok = StartService(svc, 0, nullptr);
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (ok || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
            return true;
        }
        return false;
    }
    CloseServiceHandle(scm);
    return false;
}

static std::string vrExtractOculusBaseFromJson(const std::string& jsonPath) {
    size_t pos = jsonPath.rfind("\\Support\\");
    if (pos == std::string::npos) pos = jsonPath.rfind("/Support/");
    if (pos != std::string::npos) return jsonPath.substr(0, pos);
    return "";
}

static std::string vrGetOculusBaseFromRegistry() {
    const char* keys[] = {
        "SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus",
        "SOFTWARE\\WOW6432Node\\Meta Platforms, Inc.\\Meta Horizon",
        "SOFTWARE\\Oculus VR, LLC\\Oculus"
    };
    for (const char* k : keys) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char path[MAX_PATH] = {};
            DWORD size = sizeof(path);
            DWORD type = REG_SZ;
            if (RegQueryValueExA(hKey, "Base", nullptr, &type, (LPBYTE)path, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(path);
            }
            RegCloseKey(hKey);
        }
    }
    return "";
}

static bool vrLaunchOculusServerExe(const std::string& basePath) {
    if (basePath.empty()) return false;
    std::string serverExe = basePath + "\\Support\\oculus-runtime\\OVRServer_x64.exe";

    if (GetFileAttributesA(serverExe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = "\"" + serverExe + "\"";
    if (CreateProcessA(nullptr, (LPSTR)cmdLine.c_str(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

static bool vrLaunchMetaOculusLink(const std::string& activeRuntimeJson) {
    LOGI("[XR Launcher] Launching Meta/Oculus Link...");
    if (vrStartOculusService()) return true;

    if (!activeRuntimeJson.empty()) {
        std::string base = vrExtractOculusBaseFromJson(activeRuntimeJson);
        if (vrLaunchOculusServerExe(base)) return true;
    }

    std::string base = vrGetOculusBaseFromRegistry();
    if (vrLaunchOculusServerExe(base)) return true;

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// VIRTUAL DESKTOP LAUNCH (VDXR)
// ─────────────────────────────────────────────────────────────────────────────

static bool vrIsVirtualDesktopStreamerRunning() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring wname = pe.szExeFile;
            std::string name(wname.begin(), wname.end());
            for (char& c : name) c = (char)tolower((unsigned char)c);
            if (name == "virtualdesktop.streamer.exe") {
                found = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

static std::string vrExtractVDInstallFromJson(const std::string& jsonPath) {
    size_t slash1 = jsonPath.rfind('\\');
    if (slash1 == std::string::npos) slash1 = jsonPath.rfind('/');
    if (slash1 == std::string::npos) return "";

    size_t slash2 = jsonPath.rfind('\\', slash1 - 1);
    if (slash2 == std::string::npos) slash2 = jsonPath.rfind('/', slash1 - 1);
    if (slash2 == std::string::npos) return "";

    return jsonPath.substr(0, slash2);
}

static std::string vrGetVirtualDesktopInstallPath(const std::string& activeRuntimeJson) {
    const char* regKeys[] = {
        "SOFTWARE\\Virtual Desktop, Inc.\\Virtual Desktop Streamer",
        "SOFTWARE\\WOW6432Node\\Virtual Desktop, Inc.\\Virtual Desktop Streamer"
    };
    for (const char* k : regKeys) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char path[MAX_PATH] = {};
            DWORD size = sizeof(path);
            DWORD type = REG_SZ;
            if (RegQueryValueExA(hKey, "InstallPath", nullptr, &type, (LPBYTE)path, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(path);
            }
            RegCloseKey(hKey);
        }
    }

    if (!activeRuntimeJson.empty()) {
        std::string base = vrExtractVDInstallFromJson(activeRuntimeJson);
        if (!base.empty()) return base;
    }

    const char* defaults[] = {
        "C:\\Program Files\\Virtual Desktop Streamer",
        "C:\\Program Files (x86)\\Virtual Desktop Streamer"
    };
    for (const char* d : defaults) {
        DWORD attrs = GetFileAttributesA(d);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            return std::string(d);
        }
    }
    return "";
}

static bool vrLaunchVirtualDesktop(const std::string& activeRuntimeJson) {
    LOGI("[XR Launcher] Launching Virtual Desktop Streamer (VDXR)...");

    if (vrIsVirtualDesktopStreamerRunning()) return true;

    std::string installPath = vrGetVirtualDesktopInstallPath(activeRuntimeJson);
    if (installPath.empty()) return false;

    std::string streamerExe = installPath + "\\VirtualDesktop.Streamer.exe";
    if (GetFileAttributesA(streamerExe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = "\"" + streamerExe + "\"";
    if (CreateProcessA(nullptr, (LPSTR)cmdLine.c_str(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN ENTRY POINT (NON-BLOCKING)
// Detects default runtime and launches it.
// Should be called continuously (e.g. per frame) until it returns true.
// ─────────────────────────────────────────────────────────────────────────────
bool vrEnsureDefaultRuntimeRunning() {
    static ULONGLONG s_launchTime = 0;
    static bool s_isLaunching = false;

    // Pump Windows messages to prevent the game window from freezing
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 1. Check if Runtime and HMD are already active
    if (vrIsRuntimeAndHMDReady()) {
        if (s_isLaunching) {
            LOGI("[XR Launcher] Runtime + HMD are now ready.");
        }
        s_isLaunching = false;
        return true; // We are good to go
    }

    // 2. Asynchronous waiting logic (Non-Blocking)
    ULONGLONG currentTime = GetTickCount64();
    if (s_isLaunching) {
        // Give the runtime up to 15 seconds to start without freezing the game
        if (currentTime - s_launchTime < 15000) {
            return false; // Still waiting, let the game loop continue
        }
        LOGI("[XR Launcher] Launch timeout reached (15s). Retrying if necessary...");
        s_isLaunching = false; // Reset to allow another attempt
    }

    // 3. Identify and launch
    std::string jsonPath = vrGetActiveRuntimeJsonPath();
    XrDefaultRuntime runtimeType = vrIdentifyRuntime(jsonPath);

    LOGI("[XR Launcher] Runtime unavailable, attempting to launch...");

    bool launched = false;
    switch (runtimeType) {
        case XrDefaultRuntime::SteamVR:
            launched = vrLaunchSteamVR(jsonPath);
            break;
        case XrDefaultRuntime::MetaOculusLink:
            launched = vrLaunchMetaOculusLink(jsonPath);
            break;
        case XrDefaultRuntime::VirtualDesktop:
            launched = vrLaunchVirtualDesktop(jsonPath);
            break;
        case XrDefaultRuntime::Other:
            LOGI("[XR Launcher] Third-party runtime detected, automatic launch not supported.");
            launched = false;
            break;
        case XrDefaultRuntime::Unknown:
        default:
            LOGI("[XR Launcher] No OpenXR runtime configured on this system!");
            return false;
    }

    if (launched) {
        s_isLaunching = true;
        s_launchTime = GetTickCount64();
    } else if (runtimeType != XrDefaultRuntime::Other) {
        LOGI("[XR Launcher] Runtime launch failed.");
    }

    return false; // Tell caller we are not ready yet (but we might be launching)
}

#endif // !ANDROID
