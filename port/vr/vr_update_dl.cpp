#include <thread>
#include <atomic>
#include <string>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "miniz/miniz.h"

#ifdef ANDROID
#include <jni.h>
#include <SDL.h>
#else
#include <windows.h>
#include <urlmon.h> // Make sure to link urlmon.lib
#endif

#ifdef ANDROID
#include <android/log.h>
#include <sys/stat.h> // Pour vérifier la taille du fichier
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PD_VR_UPDATE", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PD_VR_UPDATE", __VA_ARGS__)
#else
#define LOGI(...)
#define LOGE(...)
#endif

extern "C" {
#include "fs.h"
}

// ----------------------------------------------------------------------------
// PLATFORM SPECIFIC KEYWORDS
// ----------------------------------------------------------------------------
#ifdef ANDROID
#define UPDATE_ASSET_KEYWORD "Perfect_Dark_VR_Standalone" // Ensure your Quest APK contains this word
#else
#define UPDATE_ASSET_KEYWORD "Perfect_Dark_PCVR"
#endif

// ----------------------------------------------------------------------------
// STATE VARIABLES
// ----------------------------------------------------------------------------
enum UpdateState {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_FINISHED,
    UPDATE_STATE_ERROR
};

static std::atomic<int> g_UpdateState(UPDATE_STATE_IDLE);
static std::atomic<float> g_UpdateProgress(0.0f);
static char g_UpdateDescription[256] = "Fetching info...\n";
static std::atomic<bool> g_UpdateDescFetchStarted(false);

// Forward declarations for your existing GitHub API helpers
std::string FetchGitHubReleaseRaw(const char* url);
std::string ExtractJsonValue(const std::string& json, const std::string& key);
std::string ExtractPlatformDownloadUrl(const std::string& json, const std::string& keyword);
double ExtractAssetSizeMB(const std::string& json, const std::string& keyword);
std::string ExtractAssetDate(const std::string& json, const std::string& keyword);

static std::string g_FetchedVersion = "";
static std::atomic<bool> g_UpdateDescFetchFinished(false);

extern "C" {
extern const char* VR_Version;
}

// ----------------------------------------------------------------------------
// PLATFORM SPECIFIC DOWNLOAD HELPERS
// ----------------------------------------------------------------------------
#ifdef ANDROID

// Reuse the Java download method you already use for texture packs
bool PerformApkDownload(const char* url, const char* filepath) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) return false;

    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz = env->GetObjectClass(activity);

    // We assume downloadZip can download any file, including APKs
    jmethodID methodID = env->GetMethodID(clazz, "downloadZip", "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (!methodID) return false;

    jstring jUrl = env->NewStringUTF(url);
    jstring jPath = env->NewStringUTF(filepath);

    jboolean success = env->CallBooleanMethod(activity, methodID, jUrl, jPath);

    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(jPath);
    env->DeleteLocalRef(activity);

    return success == JNI_TRUE;
}

// Call a Java method to trigger the Android Intent
void TriggerApkInstall(const char* filepath) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) return;

    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz = env->GetObjectClass(activity);

    jmethodID methodID = env->GetMethodID(clazz, "installApk", "(Ljava/lang/String;)V");

    // Protection anti-crash : si la méthode n'existe pas, on vide l'erreur JNI
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(activity);
        return; // On annule l'installation proprement
    }

    if (methodID) {
        jstring jPath = env->NewStringUTF(filepath);
        env->CallVoidMethod(activity, methodID, jPath);

        // Protection supplémentaire si la méthode Java elle-même lève une exception
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        env->DeleteLocalRef(jPath);
    }

    env->DeleteLocalRef(activity);
}

#else

// Windows Download Callback
class UpdateDownloadCallback : public IBindStatusCallback {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText) override {
        if (ulProgressMax > 0) {
            g_UpdateProgress.store(((float)ulProgress / ulProgressMax) * 100.0f);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD dwReserved, IBinding* pib) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG* pnPriority) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD reserved) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT hresult, LPCWSTR szError) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD* grfBINDF, BINDINFO* pbindinfo) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD grfBSCF, DWORD dwSize, FORMATETC* pformatetc, STGMEDIUM* pstgmed) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID riid, IUnknown* punk) override { return S_OK; }
};

#endif

// ----------------------------------------------------------------------------
// DESCRIPTION FETCHING THREAD
// ----------------------------------------------------------------------------
void FetchUpdateDescWorker() {
    const char* LATEST_URL = "https://api.github.com/repos/Alex-LeTux/perfect_dark_VR/releases/latest";
    const char* keyword = UPDATE_ASSET_KEYWORD;

    std::string json = FetchGitHubReleaseRaw(LATEST_URL);
    std::string version = ExtractJsonValue(json, "tag_name");

    // Save fetched version for comparison
    g_FetchedVersion = version;

    double sizeMB = ExtractAssetSizeMB(json, keyword);
    std::string date = ExtractAssetDate(json, keyword);

    if (!version.empty() && sizeMB > 0.0 && !date.empty()) {
        snprintf(g_UpdateDescription, sizeof(g_UpdateDescription),
                 "Installed version: %s\nLatest version: %s\nDate: %s\nSize: %.1f MB\n",
                 VR_Version, version.c_str(), date.c_str(), sizeMB);
    } else if (!version.empty()) {
        snprintf(g_UpdateDescription, sizeof(g_UpdateDescription),
                 "Installed version: %s\nLatest version: %s\n",
                 VR_Version, version.c_str());
    } else {
        snprintf(g_UpdateDescription, sizeof(g_UpdateDescription),
                 "Installed version: %s\nError fetching update info!\n",
                 VR_Version);
    }

    // Indicates that the check is finished
    g_UpdateDescFetchFinished.store(true);
}

// ----------------------------------------------------------------------------
// DOWNLOAD & UPDATE EXECUTION THREAD
// ----------------------------------------------------------------------------
void DownloadAndUpdateWorker() {
    g_UpdateState.store(UPDATE_STATE_DOWNLOADING);
    g_UpdateProgress.store(0.0f);

    const char* LATEST_URL = "https://api.github.com/repos/Alex-LeTux/perfect_dark_VR/releases/latest";
    const char* keyword = UPDATE_ASSET_KEYWORD;

    // 1. Fetch Download Link
    std::string json = FetchGitHubReleaseRaw(LATEST_URL);
    std::string link = ExtractPlatformDownloadUrl(json, keyword);

    if (link.empty()) {
        g_UpdateState.store(UPDATE_STATE_ERROR);
        return;
    }

#ifdef ANDROID
    // ========================================================================
    // ANDROID UPDATE ROUTINE
    // ========================================================================
    const char* extStorage = SDL_AndroidGetExternalStoragePath();
    std::string zipPath = std::string(extStorage) + "/update_temp.zip";
    std::string apkPath = std::string(extStorage) + "/update_temp.apk";

    LOGI("Starting download. Target: %s", zipPath.c_str());

    remove(zipPath.c_str());
    remove(apkPath.c_str());

    bool success = PerformApkDownload(link.c_str(), zipPath.c_str());
    LOGI("PerformApkDownload returned: %d", success);

    if (!success) {
        LOGE("Error: PerformApkDownload failed.");
        g_UpdateState.store(UPDATE_STATE_ERROR);
        return;
    }

    struct stat st;
    if (stat(zipPath.c_str(), &st) == 0) {
        LOGI("Success: Downloaded file found. Size: %lld bytes", (long long)st.st_size);
        if (st.st_size < 1000000) {
            LOGE("Error: File is too small to be an APK!");
        }
    } else {
        LOGE("Fatal Error: File does not exist after PerformApkDownload!");
    }

    int renRes = rename(zipPath.c_str(), apkPath.c_str());
    LOGI("Rename return code (0 = success): %d", renRes);

    g_UpdateState.store(UPDATE_STATE_FINISHED);

    LOGI("Calling TriggerApkInstall...");
    TriggerApkInstall(apkPath.c_str());
    LOGI("TriggerApkInstall finished on C++ side.");
#else
    // ========================================================================
    // WINDOWS UPDATE ROUTINE
    // ========================================================================

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string currentExePath(exePath);

    std::filesystem::path currentPath(currentExePath);
    std::string gameDir = currentPath.parent_path().string();
    std::string downloadZipPath = gameDir + "\\update_temp.zip";
    std::string extractedExePath = gameDir + "\\PerfectDarkVR_UpdateTemp.exe";

    UpdateDownloadCallback callback;
    HRESULT res = URLDownloadToFileA(NULL, link.c_str(), downloadZipPath.c_str(), 0, &callback);

    if (res != S_OK) {
        g_UpdateState.store(UPDATE_STATE_ERROR);
        return;
    }

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_file(&zip_archive, downloadZipPath.c_str(), 0)) {
        g_UpdateState.store(UPDATE_STATE_ERROR);
        return;
    }

    bool exeFound = false;
    int num_files = (int)mz_zip_reader_get_num_files(&zip_archive);
    for (int i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;

        std::string filename = file_stat.m_filename;
        if (filename.find(".exe") != std::string::npos) {
            mz_zip_reader_extract_to_file(&zip_archive, i, extractedExePath.c_str(), 0);
            exeFound = true;
            break;
        }
    }
    mz_zip_reader_end(&zip_archive);
    remove(downloadZipPath.c_str());

    if (!exeFound) {
        g_UpdateState.store(UPDATE_STATE_ERROR);
        return;
    }

    g_UpdateState.store(UPDATE_STATE_FINISHED);

    std::string batFilePath = gameDir + "\\apply_update.bat";
    std::ofstream batFile(batFilePath);

    if (batFile.is_open()) {
        batFile << "@echo off\n";
        batFile << "cd /d \"" << gameDir << "\"\n";
        batFile << ":waitloop\n";
        batFile << "move /y \"" << extractedExePath << "\" \"" << currentExePath << "\" > NUL 2>&1\n";
        batFile << "if errorlevel 1 (\n";
        batFile << "    timeout /t 1 /nobreak > NUL\n";
        batFile << "    goto waitloop\n";
        batFile << ")\n";
        batFile << "start \"\" \"" << currentExePath << "\"\n";
        batFile << "(goto) 2>nul & del \"%~f0\"\n";
        batFile.close();
    }

    ShellExecuteA(NULL, "open", batFilePath.c_str(), NULL, NULL, SW_HIDE);
    TerminateProcess(GetCurrentProcess(), 0);
#endif
}

// ----------------------------------------------------------------------------
// C EXPORTS FOR MENUS
// ----------------------------------------------------------------------------

extern "C" {

void StartFetchingUpdateDescription(void) {
    bool expected = false;
    if (g_UpdateDescFetchStarted.compare_exchange_strong(expected, true)) {
        g_UpdateDescFetchFinished.store(false);
        std::thread worker(FetchUpdateDescWorker);
        worker.detach();
    }
}

const char* GetUpdateDescriptionText(void) {
    return g_UpdateDescription;
}

void StartGameUpdateThread(void) {
    if (g_UpdateState.load() == UPDATE_STATE_DOWNLOADING) {
        return;
    }
    std::thread worker(DownloadAndUpdateWorker);
    worker.detach();
}

int GetGameUpdateState(void) {
    return g_UpdateState.load();
}

extern "C" float GetAssetDownloadProgress(void); // Declared in vr_textures_pack_dl.cpp

float GetGameUpdateProgress(void) {
#ifdef ANDROID
    return GetAssetDownloadProgress();
#else
    return g_UpdateProgress.load();
#endif
}

void ResetGameUpdateState(void) {
    g_UpdateState.store(UPDATE_STATE_IDLE);
    g_UpdateProgress.store(0.0f);
}

int CheckUpdateStatus(const char* currentVersion) {
    if (!g_UpdateDescFetchFinished.load()) return 0;
    if (g_FetchedVersion.empty()) return 3;

    if (g_FetchedVersion != std::string(currentVersion)) {
        return 1;
    }
    return 2;
}

}
