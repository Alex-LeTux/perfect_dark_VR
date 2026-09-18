#include <thread>
#include <atomic>
#include <string>
#include <cstdio>
#include <filesystem>
#include <istream>
#include <algorithm>
#include <cctype>

#include "miniz/miniz.h"

// ============================================================================
// PLATFORM-SPECIFIC INCLUDES
// ============================================================================
#ifdef _WIN32
#include <windows.h>
    #include <urlmon.h>
#elif defined(__ANDROID__)
#include <jni.h>
#include <SDL.h>
#endif

// ============================================================================
// EXTERNAL C FUNCTIONS & ENGINE HEADERS
// ============================================================================
extern "C" {
// Include the engine's filesystem header for fsFullPath[cite: 2]
#include "fs.h"

int extTexInit(void);
void extTexSetPack(const char *newPackName);
}

// ============================================================================
// GLOBALS & STATE
// ============================================================================
enum DownloadState {
    STATE_IDLE = 0,
    STATE_DOWNLOADING,
    STATE_EXTRACTING,
    STATE_FINISHED,
    STATE_ERROR
};

static std::atomic<int> g_CurrentState(STATE_IDLE);
static std::atomic<float> g_DownloadProgress(0.0f);
static std::string g_TargetPackFullPath = "";
static std::string g_TargetPackName = "";
static std::string g_TargetKeyword = "";

static char g_DescriptionText[256] = "Fetching info...\n";
static std::atomic<bool> g_DescFetchStarted(false);
static std::string g_DescKeyword = "";

// ============================================================================
// WINDOWS IMPLEMENTATION
// ============================================================================
#ifdef _WIN32

// To retrieve progress via the Windows API, this class must be provided
class NativeDownloadCallback : public IBindStatusCallback {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText) override {
        if (ulProgressMax > 0) {
            g_DownloadProgress.store(((float)ulProgress / ulProgressMax) * 100.0f);
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

bool PerformNativeDownload(const char* url, const char* filepath) {
    NativeDownloadCallback callback;
    HRESULT res = URLDownloadToFileA(NULL, url, filepath, 0, &callback);
    return (res == S_OK);
}

// ============================================================================
// ANDROID IMPLEMENTATION
// ============================================================================
#elif defined(__ANDROID__)

// Helper to retrieve the JNI environment via SDL2
JNIEnv* GetJniEnv() {
    return (JNIEnv*)SDL_AndroidGetJNIEnv();
}

// 1. Function to download the ZIP on Android
bool PerformNativeDownload(const char* url, const char* filepath) {
    JNIEnv* env = GetJniEnv();
    if (!env) return false;

    // Get the current Java Activity managed by SDL
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz = env->GetObjectClass(activity);

    // Look for the downloadZip(String url, String path) method
    jmethodID methodID = env->GetMethodID(clazz, "downloadZip", "(Ljava/lang/String;Ljava/lang/String;)Z");

    jstring jUrl = env->NewStringUTF(url);
    jstring jPath = env->NewStringUTF(filepath);

    // Execute the Java function
    jboolean success = env->CallBooleanMethod(activity, methodID, jUrl, jPath);

    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(jPath);
    env->DeleteLocalRef(activity);

    return success == JNI_TRUE;
}

// 2. Function to read text from a URL on Android
std::string PerformNativeFetchText(const char* url) {
    JNIEnv* env = GetJniEnv();
    if (!env) return "";

    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz = env->GetObjectClass(activity);

    // Look for the fetchText(String url) method
    jmethodID methodID = env->GetMethodID(clazz, "fetchText", "(Ljava/lang/String;)Ljava/lang/String;");

    jstring jUrl = env->NewStringUTF(url);
    jstring result = (jstring)env->CallObjectMethod(activity, methodID, jUrl);

    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(activity);

    if (!result) return "";

    const char* chars = env->GetStringUTFChars(result, nullptr);
    std::string resStr(chars);

    env->ReleaseStringUTFChars(result, chars);
    env->DeleteLocalRef(result);

    return resStr;
}

// 3. Callback called by Java to update the progress bar
extern "C" JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_updateDownloadProgressNative(JNIEnv* env, jobject thiz, jfloat progress) {
    g_DownloadProgress.store(progress);
}

#endif

// ============================================================================
// GITHUB API UTILITIES
// ============================================================================

// 1. Function to extract a value from a key in raw JSON
std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";

    pos = json.find("\"", pos);
    if (pos == std::string::npos) return "";

    pos += 1;
    size_t endPos = json.find("\"", pos);
    if (endPos == std::string::npos) return "";

    return json.substr(pos, endPos - pos);
}

// 2. Function to find the platform-specific download URL
std::string ExtractPlatformDownloadUrl(const std::string& json, const std::string& keyword) {
    size_t offset = 0;

    while (true) {
        size_t pos = json.find("\"browser_download_url\"", offset);
        if (pos == std::string::npos) return "";

        pos = json.find(":", pos);
        pos = json.find("\"", pos) + 1;
        size_t endPos = json.find("\"", pos);

        if (pos == std::string::npos || endPos == std::string::npos) return "";

        std::string url = json.substr(pos, endPos - pos);

        // Check if this URL contains our keyword (PC or Quest)
        if (url.find(keyword) != std::string::npos) {
            return url;
        }

        offset = endPos;
    }
}

// 3. Function to retrieve the file size (in MB)
double ExtractAssetSizeMB(const std::string& json, const std::string& keyword) {
    size_t offset = 0;

    while (true) {
        size_t namePos = json.find("\"name\"", offset);
        if (namePos == std::string::npos) return 0.0;

        size_t valPos = json.find(":", namePos);
        valPos = json.find("\"", valPos) + 1;
        size_t endPos = json.find("\"", valPos);
        std::string name = json.substr(valPos, endPos - valPos);

        if (name.find(keyword) != std::string::npos) {
            size_t sizePos = json.find("\"size\"", namePos);
            if (sizePos != std::string::npos) {
                sizePos = json.find(":", sizePos) + 1;
                size_t sizeEndPos = json.find(",", sizePos);

                std::string sizeStr = json.substr(sizePos, sizeEndPos - sizePos);
                return std::stod(sizeStr) / (1024.0 * 1024.0);
            }
        }

        offset = endPos;
    }
}

// 4. Function to retrieve the file upload date
std::string ExtractAssetDate(const std::string& json, const std::string& keyword) {
    size_t offset = 0;

    while (true) {
        size_t namePos = json.find("\"name\"", offset);
        if (namePos == std::string::npos) return "";

        size_t valPos = json.find(":", namePos);
        valPos = json.find("\"", valPos) + 1;
        size_t endPos = json.find("\"", valPos);
        std::string name = json.substr(valPos, endPos - valPos);

        if (name.find(keyword) != std::string::npos) {
            size_t datePos = json.find("\"updated_at\"", namePos);
            if (datePos != std::string::npos) {
                datePos = json.find(":", datePos);
                datePos = json.find("\"", datePos) + 1;
                size_t dateEnd = json.find("\"", datePos);

                std::string dateStr = json.substr(datePos, dateEnd - datePos);

                size_t tPos = dateStr.find("T");
                if (tPos != std::string::npos) {
                    dateStr = dateStr.substr(0, tPos);
                }

                return dateStr;
            }
        }

        offset = endPos;
    }
}

// 5. Function that downloads the large JSON text from GitHub
std::string FetchGitHubReleaseRaw(const char* url) {
    std::string result = "";

#ifdef _WIN32
    IStream* stream = nullptr;
    HRESULT hr = URLOpenBlockingStreamA(NULL, url, &stream, 0, NULL);
    if (hr == S_OK && stream) {
        char buffer[1024];
        ULONG bytesRead = 0;

        while (SUCCEEDED(stream->Read(buffer, sizeof(buffer) - 1, &bytesRead)) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result += buffer;
        }
        stream->Release();
    }
#elif defined(__ANDROID__)
    result = PerformNativeFetchText(url);
#endif

    return result;
}

// ============================================================================
// DESCRIPTION THREAD (Uses the GitHub API)
// ============================================================================
void FetchDescWorker() {
    const char* LATEST_URL = "https://api.github.com/repos/retro-foundry/Perfect-Dark-Plus-HD-Textures/releases/latest";
    const char* BETA_URL = "https://api.github.com/repos/retro-foundry/Perfect-Dark-Plus-HD-Textures/releases/tags/Beta_Release_V0.10";

    std::string json = FetchGitHubReleaseRaw(LATEST_URL);
    double sizeMB = ExtractAssetSizeMB(json, g_DescKeyword);

    if (sizeMB == 0.0) {
        json = FetchGitHubReleaseRaw(BETA_URL);
        sizeMB = ExtractAssetSizeMB(json, g_DescKeyword);
    }

    std::string version = ExtractJsonValue(json, "tag_name");
    std::string date = ExtractAssetDate(json, g_DescKeyword);

    if (!version.empty() && sizeMB > 0.0 && !date.empty()) {
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "Version: %s\nDate: %s\nSize: %.1f MB\n", version.c_str(), date.c_str(), sizeMB);
    }
    else if (!version.empty()) {
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "Version: %s\n", version.c_str());
    }
    else {
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "Error fetching description!\n");
    }
}

// ============================================================================
// MAIN THREAD (Uses the GitHub API)
// ============================================================================
void DownloadAndExtractWorker() {
    g_CurrentState.store(STATE_DOWNLOADING);
    g_DownloadProgress.store(0.0f);

    const char* LATEST_URL = "https://api.github.com/repos/retro-foundry/Perfect-Dark-Plus-HD-Textures/releases/latest";
    const char* BETA_URL = "https://api.github.com/repos/retro-foundry/Perfect-Dark-Plus-HD-Textures/releases/tags/Beta_Release_V0.10";

    // --- 1. RETRIEVE THE DOWNLOAD LINK VIA THE API ---
    std::string json = FetchGitHubReleaseRaw(LATEST_URL);
    std::string link = ExtractPlatformDownloadUrl(json, g_TargetKeyword);

    if (link.empty()) {
        json = FetchGitHubReleaseRaw(BETA_URL);
        link = ExtractPlatformDownloadUrl(json, g_TargetKeyword);
    }

    if (link.empty()) {
        g_CurrentState.store(STATE_ERROR);
        return;
    }

    char dynamicUrl[512] = {0};
    snprintf(dynamicUrl, sizeof(dynamicUrl), "%s", link.c_str());

    // --- 2. PREPARE THE FOLDER & DYNAMIC ZIP PATH ---
    std::filesystem::create_directories(g_TargetPackFullPath);

    // Dynamically set the ZIP path relative to the target pack folder
    std::string zipFilePath = g_TargetPackFullPath + "_temp.zip";

    // --- 3. NATIVE ZIP DOWNLOAD ---
    bool downloadSuccess = PerformNativeDownload(dynamicUrl, zipFilePath.c_str());

    if (!downloadSuccess) {
        g_CurrentState.store(STATE_ERROR);
        return;
    }

    // --- 4. DECOMPRESSION (miniz) ---
    g_CurrentState.store(STATE_EXTRACTING);

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_file(&zip_archive, zipFilePath.c_str(), 0)) {
        g_CurrentState.store(STATE_ERROR);
        return;
    }

    int num_files = (int)mz_zip_reader_get_num_files(&zip_archive);
    for (int i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) continue;

        g_DownloadProgress.store(((float)i / num_files) * 100.0f);

        std::string originalPath = file_stat.m_filename;
        std::string targetPath;
        size_t firstSlash = originalPath.find('/');

        if (firstSlash != std::string::npos) {
            std::string subPath = originalPath.substr(firstSlash + 1);
            if (subPath.empty()) continue;
            targetPath = g_TargetPackFullPath + "/" + subPath;
        } else {
            targetPath = g_TargetPackFullPath + "/" + originalPath;
        }

        std::filesystem::path filePath(targetPath);
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) continue;

        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".txt") continue;

        mz_zip_reader_extract_to_file(&zip_archive, i, targetPath.c_str(), 0);
    }

    mz_zip_reader_end(&zip_archive);

    // Clean up the dynamically created zip file
    remove(zipFilePath.c_str());

    // Set the successfully downloaded pack
    extTexSetPack(g_TargetPackName.c_str());
    g_CurrentState.store(STATE_FINISHED);

}

// ============================================================================
// C INTERFACE
// ============================================================================
extern "C" {

void StartFetchingDescription(const char* keyword) {
    g_DescKeyword = keyword;
    g_DescFetchStarted.store(false);
    snprintf(g_DescriptionText, sizeof(g_DescriptionText), "Fetching info...\n");
}

const char* GetDescriptionText(void) {
    bool expected = false;
    if (g_DescFetchStarted.compare_exchange_strong(expected, true)) {
        std::thread t(FetchDescWorker);
        t.detach();
    }
    return g_DescriptionText;
}

void StartAssetDownloadThread(const char* fullPath_ignored, const char* packName, const char* keyword) {
    if (g_CurrentState.load() == STATE_DOWNLOADING || g_CurrentState.load() == STATE_EXTRACTING) {
        return;
    }

    // We completely ignore 'fullPath_ignored' sent by the UI.
    // Instead, we use fsFullPath and the packName to build the true robust path,
    // exactly like ext_tex.c does for loading[cite: 2].
    char relPath[512];
    snprintf(relPath, sizeof(relPath), "$S/texture-packs/%s", packName);

    g_TargetPackFullPath = fsFullPath(relPath);
    g_TargetPackName = packName;
    g_TargetKeyword = keyword;

    std::thread worker(DownloadAndExtractWorker);
    worker.detach();
}

int GetAssetDownloadState(void) {
    return g_CurrentState.load();
}

float GetAssetDownloadProgress(void) {
    return g_DownloadProgress.load();
}

void ResetAssetDownloadState(void) {
    g_CurrentState.store(STATE_IDLE);
    g_DownloadProgress.store(0.0f);
}

void DeleteAssetFolderFullPath(const char* fullPath) {
    // Even if the UI sends a broken path containing "app_process",
    // we isolate the pack name at the end of the path...
    std::filesystem::path p(fullPath);
    std::string packName = p.filename().string();

    // ...and we rebuild the true system path via fsFullPath!
    char relPath[512];
    snprintf(relPath, sizeof(relPath), "$S/texture-packs/%s", packName.c_str());
    std::string resolvedPath = fsFullPath(relPath);

    std::error_code ec;
    std::filesystem::remove_all(resolvedPath, ec);
    ResetAssetDownloadState();
}
}
