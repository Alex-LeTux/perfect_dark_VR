// vr_runtime_launcher.h
// Automatic detection and launch of the default OpenXR runtime (Windows)
// Compatible with the vr_openxr.cpp file of Perfect Dark PC port
// Supports: SteamVR, Meta/Oculus Link, Virtual Desktop (VDXR)
// Author: generated for Alex Le Tux

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
// This is the key that ALL OpenXR runtimes write to declare themselves active.
// ─────────────────────────────────────────────────────────────────────────────

static std::string vrGetActiveRuntimeJsonPath() {
    const char* KEY = "SOFTWARE\\Khronos\\OpenXR\\1";
    HKEY hKey;

    // Try 64-bit first, then 32-bit (WOW6432Node)
    REGSAM views[] = { KEY_READ | KEY_WOW64_64KEY, KEY_READ | KEY_WOW64_32KEY };
    for (REGSAM view : views) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, KEY, 0, view, &hKey) == ERROR_SUCCESS) {
            char path[MAX_PATH] = {};
            DWORD size = sizeof(path);
            DWORD type = REG_SZ;
            LSTATUS st = RegQueryValueExA(hKey, "ActiveRuntime", nullptr, &type, (LPBYTE)path, &size);
            RegCloseKey(hKey);
            if (st == ERROR_SUCCESS && strlen(path) > 0) {
                LOGI("[XR Launcher] ActiveRuntime JSON: %s", path);
                return std::string(path);
            }
        }
    }
    LOGI("[XR Launcher] HKLM\\SOFTWARE\\Khronos\\OpenXR\\1\\ActiveRuntime not found");
    return "";
}

static XrDefaultRuntime vrIdentifyRuntime(const std::string& jsonPath) {
    if (jsonPath.empty()) return XrDefaultRuntime::Unknown;

    // Convert to lowercase for case-insensitive comparison
    std::string lower = jsonPath;
    for (char& c : lower) c = (char)tolower((unsigned char)c);

    if (lower.find("steamvr") != std::string::npos ||
        lower.find("steam") != std::string::npos)
        return XrDefaultRuntime::SteamVR;

    if (lower.find("oculus") != std::string::npos ||
        lower.find("meta") != std::string::npos)
        return XrDefaultRuntime::MetaOculusLink;

    // Virtual Desktop: the JSON is named virtualdesktop-openxr.json
    // and is located in "Virtual Desktop Streamer\OpenXR\"
    if (lower.find("virtualdesktop") != std::string::npos ||
        lower.find("virtual desktop") != std::string::npos ||
        lower.find("vdxr") != std::string::npos)
        return XrDefaultRuntime::VirtualDesktop;

    return XrDefaultRuntime::Other;
}

// ─────────────────────────────────────────────────────────────────────────────
// PROBE: tests if the runtime responds WITHOUT leaving an open instance
// ─────────────────────────────────────────────────────────────────────────────


// vr_runtime_launcher.h — replaces vrIsRuntimeResponding()
static bool vrIsRuntimeAndHMDReady(void) {
    // Step 1: can the instance be created?
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    strncpy(ci.applicationInfo.applicationName, "PDprobe", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 0;
    ci.enabledExtensionNames = nullptr;

    XrInstance probe = XR_NULL_HANDLE;
    XrResult r = xrCreateInstance(&ci, &probe);
    if (XR_FAILED(r) || probe == XR_NULL_HANDLE) {
        LOGI("XR Launcher: runtime not ready (xrCreateInstance=%d)", (int)r);
        return false;
    }

    // Step 2: is the HMD visible?
    XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sysId = XR_NULL_SYSTEM_ID;
    XrResult rs = xrGetSystem(probe, &sysInfo, &sysId);
    xrDestroyInstance(probe);

    if (XR_FAILED(rs)) {
        LOGI("XR Launcher: HMD not yet connected (xrGetSystem=%d)", (int)rs);
        return false;
    }
    return true;
}




//static bool vrIsRuntimeResponding() {
//    // Create a minimal instance without extensions to probe the runtime
//    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
//    strncpy(ci.applicationInfo.applicationName, "PD_probe", XR_MAX_APPLICATION_NAME_SIZE - 1);
//    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
//    ci.enabledExtensionCount = 0;
//    ci.enabledExtensionNames = nullptr;
//
//    XrInstance probe = XR_NULL_HANDLE;
//    XrResult r = xrCreateInstance(&ci, &probe);
//    if (XR_SUCCEEDED(r) && probe != XR_NULL_HANDLE) {
//        xrDestroyInstance(probe);
//        return true;
//    }
//    LOGI("[XR Launcher] Probe xrCreateInstance => %d (runtime missing or not ready)", (int)r);
//    return false;
//}

// ─────────────────────────────────────────────────────────────────────────────
// LAUNCH STEAMVR
// Preference: Steam URI (independent of install path)
// Fallback: exe via Steam registry
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

static bool vrLaunchSteamVR() {
    LOGI("[XR Launcher] Launching SteamVR...");

    // Method 1: Steam URI (App ID 250820 = SteamVR)
    HINSTANCE hi = ShellExecuteA(nullptr, "open", "steam://run/250820", nullptr, nullptr, SW_HIDE);
    if ((INT_PTR)hi > 32) {
        LOGI("[XR Launcher] SteamVR launched via steam://run/250820 URI");
        return true;
    }
    LOGI("[XR Launcher] Steam URI failed (hi=%d), fallback to exe...", (int)(INT_PTR)hi);

    // Method 2: direct Steam exe from registry
    std::string steamPath = vrGetSteamInstallPath();
    if (steamPath.empty()) {
        LOGI("[XR Launcher] Steam not found in registry");
        return false;
    }

    std::string steamExe = steamPath + "\\steam.exe";
    LOGI("[XR Launcher] Launching Steam: %s", steamExe.c_str());

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
    LOGI("[XR Launcher] CreateProcess Steam failed: %lu", GetLastError());
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAUNCH META / OCULUS LINK
// Strategy by order of robustness:
// 1. Windows Service OVRService / OVRServiceLauncher (no path, always valid)
// 2. Exe via path extracted from ActiveRuntime JSON (Khronos registry)
// 3. Exe via Oculus/Meta registry key (install path)
// ─────────────────────────────────────────────────────────────────────────────

static bool vrStartOculusService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        LOGI("[XR Launcher] OpenSCManager failed: %lu", GetLastError());
        return false;
    }

    const char* serviceNames[] = {
        "OVRService",           // Old Oculus installation
        "OVRServiceLauncher",   // Recent versions
        "Meta.XrRuntime"        // Possible future Meta Horizon name
    };

    for (const char* name : serviceNames) {
        SC_HANDLE svc = OpenServiceA(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
        if (!svc) continue;

        SERVICE_STATUS status = {};
        QueryServiceStatus(svc, &status);

        if (status.dwCurrentState == SERVICE_RUNNING) {
            LOGI("[XR Launcher] Service '%s' already enabled", name);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return true;
        }

        LOGI("[XR Launcher] Starting service '%s'...", name);
        BOOL ok = StartService(svc, 0, nullptr);
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (ok || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
            LOGI("[XR Launcher] Service '%s' started", name);
            return true;
        }
        LOGI("[XR Launcher] StartService '%s' failed: %lu", name, GetLastError());
        return false;
    }

    CloseServiceHandle(scm);
    LOGI("[XR Launcher] No OVR service found (OVRService, OVRServiceLauncher)");
    return false;
}

/* Extracts the base directory from the ActiveRuntime JSON path
   Ex: "C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json"
    => "C:\Program Files\Oculus" */
static std::string vrExtractOculusBaseFromJson(const std::string& jsonPath) {
    size_t pos = jsonPath.rfind("\\Support\\");
    if (pos == std::string::npos)
        pos = jsonPath.rfind("/Support/");
    if (pos != std::string::npos)
        return jsonPath.substr(0, pos);
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
    LOGI("[XR Launcher] Launching OVRServer: %s", serverExe.c_str());

    DWORD attrs = GetFileAttributesA(serverExe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        LOGI("[XR Launcher] OVRServer_x64.exe not found at: %s", serverExe.c_str());
        return false;
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = "\"" + serverExe + "\"";
    if (CreateProcessA(nullptr, (LPSTR)cmdLine.c_str(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOGI("[XR Launcher] OVRServer_x64.exe started");
        return true;
    }
    LOGI("[XR Launcher] CreateProcess OVRServer failed: %lu", GetLastError());
    return false;
}

static bool vrLaunchMetaOculusLink(const std::string& activeRuntimeJson) {
    LOGI("[XR Launcher] Launching Meta/Oculus Link...");

    // Step 1: Windows service (most robust method, no path required)
    if (vrStartOculusService()) return true;

    LOGI("[XR Launcher] OVR service unavailable, attempting via executable...");

    // Step 2: path extracted from ActiveRuntime JSON (Khronos registry)
    if (!activeRuntimeJson.empty()) {
        std::string base = vrExtractOculusBaseFromJson(activeRuntimeJson);
        if (vrLaunchOculusServerExe(base)) return true;
    }

    // Step 3: direct Oculus/Meta registry key
    std::string base = vrGetOculusBaseFromRegistry();
    if (vrLaunchOculusServerExe(base)) return true;

    LOGI("[XR Launcher] All Meta/Oculus methods have failed");
    return false;
}

/*
 ─────────────────────────────────────────────────────────────────────────────
VIRTUAL DESKTOP LAUNCH (VDXR)

Virtual Desktop Streamer is both the streamer AND the host process for the
VDXR runtime. There is no separate Windows service: VirtualDesktop.Streamer.exe
just needs to be running for the runtime to be available.

Known installation paths (in order of priority):
1. Registry key HKLM\SOFTWARE\Virtual Desktop, Inc.\Virtual Desktop Streamer
value "InstallPath" (written by the official installer)
2. Default path: C:\Program Files\Virtual Desktop Streamer
3. Extraction from the Khronos ActiveRuntime JSON path
Ex: "C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json"
=> "C:\Program Files\Virtual Desktop Streamer"

Launch strategy:
1. Check if VirtualDesktop.Streamer.exe is already running (via process snapshot)
2. If not, launch VirtualDesktop.Streamer.exe from the resolved path

Note: VDXR only responds to xrCreateInstance() once the headset is connected
and streaming is active. The polling in vrEnsureDefaultRuntimeRunning() handles
this delay. We increase the timeout to 20s for Virtual Desktop because starting
the streamer + connecting the headset takes more time than SteamVR.
 ─────────────────────────────────────────────────────────────────────────────
*/

static bool vrIsVirtualDesktopStreamerRunning() {
    // Iterate over the process snapshot to detect VirtualDesktop.Streamer.exe
    // Uses W (Unicode) variants because UNICODE is defined in this project.
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            // Convert wchar_t* to std::string for case-insensitive comparison
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

// Extracts the install folder from the ActiveRuntime JSON path
// Ex: "C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json"
//  => "C:\Program Files\Virtual Desktop Streamer"
static std::string vrExtractVDInstallFromJson(const std::string& jsonPath) {
    // Goes up two levels: \OpenXR\file.json => parent folder of OpenXR folder
    size_t slash1 = jsonPath.rfind('\\');
    if (slash1 == std::string::npos) slash1 = jsonPath.rfind('/');
    if (slash1 == std::string::npos) return "";

    size_t slash2 = jsonPath.rfind('\\', slash1 - 1);
    if (slash2 == std::string::npos) slash2 = jsonPath.rfind('/', slash1 - 1);
    if (slash2 == std::string::npos) return "";

    return jsonPath.substr(0, slash2);
}

static std::string vrGetVirtualDesktopInstallPath(const std::string& activeRuntimeJson) {
    // Method 1: official registry key
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
                LOGI("[XR Launcher] VD install path (registry): %s", path);
                return std::string(path);
            }
            RegCloseKey(hKey);
        }
    }

    // Method 2: extraction from ActiveRuntime JSON
    if (!activeRuntimeJson.empty()) {
        std::string base = vrExtractVDInstallFromJson(activeRuntimeJson);
        if (!base.empty()) {
            LOGI("[XR Launcher] VD install path (JSON): %s", base.c_str());
            return base;
        }
    }

    // Method 3: default path (Program Files)
    const char* defaults[] = {
        "C:\\Program Files\\Virtual Desktop Streamer",
        "C:\\Program Files (x86)\\Virtual Desktop Streamer"
    };
    for (const char* d : defaults) {
        DWORD attrs = GetFileAttributesA(d);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            LOGI("[XR Launcher] VD install path (defaut): %s", d);
            return std::string(d);
        }
    }

    LOGI("[XR Launcher] Virtual Desktop installation path not found");
    return "";
}

static bool vrLaunchVirtualDesktop(const std::string& activeRuntimeJson) {
    LOGI("[XR Launcher] Launching Virtual Desktop Streamer (VDXR)...");

    // Step 1: check if the streamer is already running
    if (vrIsVirtualDesktopStreamerRunning()) {
        LOGI("[XR Launcher] VirtualDesktop.Streamer.exe is already running.");
        // The runtime may be initializing — polling in
        // vrEnsureDefaultRuntimeRunning() prendra le relais.
        return true;
    }

    // Step 2: resolve installation path
    std::string installPath = vrGetVirtualDesktopInstallPath(activeRuntimeJson);
    if (installPath.empty()) {
        LOGI("[XR Launcher] Unable to find Virtual Desktop Streamer.");
        return false;
    }

    std::string streamerExe = installPath + "\\VirtualDesktop.Streamer.exe";
    DWORD attrs = GetFileAttributesA(streamerExe.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        LOGI("[XR Launcher] VirtualDesktop.Streamer.exe not found: %s", streamerExe.c_str());
        return false;
    }

    // Step 3: launch the streamer
    LOGI("[XR Launcher] Launching: %s", streamerExe.c_str());
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = "\"" + streamerExe + "\"";
    if (CreateProcessA(nullptr, (LPSTR)cmdLine.c_str(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOGI("[XR Launcher] VirtualDesktop.Streamer.exe launched successfully.");
        return true;
    }

    LOGI("[XR Launcher] CreateProcess VirtualDesktop.Streamer.exe failed: %lu", GetLastError());
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN ENTRY POINT
// Detects default runtime and launches it if it's not responding yet.
// Call at the very beginning of openxrInitializeVR_windows(), before vrCreateInstance().
// ─────────────────────────────────────────────────────────────────────────────
bool vrEnsureDefaultRuntimeRunning() {
    // 1. Identify configured default runtime
    std::string jsonPath = vrGetActiveRuntimeJsonPath();
    XrDefaultRuntime runtimeType = vrIdentifyRuntime(jsonPath);

    const char* runtimeName = "Inconnu";
    if      (runtimeType == XrDefaultRuntime::SteamVR)        runtimeName = "SteamVR";
    else if (runtimeType == XrDefaultRuntime::MetaOculusLink)  runtimeName = "Meta/Oculus Link";
    else if (runtimeType == XrDefaultRuntime::VirtualDesktop)  runtimeName = "Virtual Desktop (VDXR)";
    else if (runtimeType == XrDefaultRuntime::Other)           runtimeName = "Autre (runtime tiers)";
    LOGI("[XR Launcher] Default runtime detected : %s", runtimeName);

    // 2. Check if it already responds
    if (vrIsRuntimeAndHMDReady()) {
        LOGI("XR Launcher: Runtime + HMD already active.");
        return true;
    }

    LOGI("[XR Launcher] Runtime unavailable, attempting to launch...");

    // 3. Launch according to detected type
    bool launched = false;
    switch (runtimeType) {
        case XrDefaultRuntime::SteamVR:
            launched = vrLaunchSteamVR();
            break;
        case XrDefaultRuntime::MetaOculusLink:
            launched = vrLaunchMetaOculusLink(jsonPath);
            break;
        case XrDefaultRuntime::VirtualDesktop:
            launched = vrLaunchVirtualDesktop(jsonPath);
            break;
        case XrDefaultRuntime::Other:
            LOGI("[XR Launcher] Third-party runtime detected (%s), automatic launch not supported.", jsonPath.c_str());
            launched = false;
            break;
        case XrDefaultRuntime::Unknown:
        default:
            LOGI("[XR Launcher] No OpenXR runtime configured on this system!");
            return false;
    }

    if (!launched && runtimeType != XrDefaultRuntime::Other) {
        LOGI("[XR Launcher] Runtime launch failed.");
        return false;
    }

    // 4. Polling d'attente
    LOGI("[XR Launcher] Waiting for runtime");

    MSG msg; // Prevents "application is not responding"
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    DispatchMessage(&msg);

    if (vrIsRuntimeAndHMDReady()) {
    LOGI("XR Launcher: Runtime+HMD ready");
    return true;
    }

    LOGI("[XR Launcher] Waiting for runtime...");
    return false;
}


#endif // !ANDROID
