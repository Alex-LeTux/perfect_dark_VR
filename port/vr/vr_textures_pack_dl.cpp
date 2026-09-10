#include <thread>
#include <atomic>
#include <string>
#include <cstdio>
#include "miniz/miniz.h"
#include <filesystem>
#include <istream>

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <urlmon.h>
#elif defined(__ANDROID__)
#include <jni.h>
#endif


extern "C" {
int extTexInit(void);
}


enum DownloadState {
    STATE_IDLE = 0,
    STATE_DOWNLOADING,
    STATE_EXTRACTING,
    STATE_FINISHED,
    STATE_ERROR
};

static std::atomic<int> g_CurrentState(STATE_IDLE);
static std::atomic<float> g_DownloadProgress(0.0f);

#ifndef __ANDROID__
const char* TARGET_PACK_KEYWORD = "PD.PLUS.HD.TEXTURE.PACK.v";
#else
const char* TARGET_PACK_KEYWORD = "PD.PLUS.HD.TEXTURE.PACK.QUEST.STANDALONE.v";
#endif

// ============================================================================
// IMPLÉMENTATION WINDOWS
// ============================================================================
#ifdef _WIN32

// Pour récupérer la progression via l'API Windows, on doit fournir cette classe
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
// IMPLÉMENTATION ANDROID
// ============================================================================
#elif defined(__ANDROID__)

#include <jni.h>
#include <SDL.h>

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

static char g_DescriptionText[256] = "Fetching info...\n";
static std::atomic<bool> g_DescFetchStarted(false);

// ============================================================================
// PATH CONFIGURATION (Windows vs Android)
// ============================================================================
#ifndef __ANDROID__
    const char* ZIP_FILE_PATH = "data/temp_assets.zip";
    const char* EXT_TEX_DIR = "data/ext_tex";
    const char* EXT_TEX_PREFIX = "data/ext_tex/";
#else
const char* ZIP_FILE_PATH = "temp_assets.zip";
const char* EXT_TEX_DIR = "ext_tex";
const char* EXT_TEX_PREFIX = "ext_tex/";
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

// 3. Function to find the platform-specific download URL
std::string ExtractPlatformDownloadUrl(const std::string& json, const std::string& keyword) {
    size_t offset = 0;

    while (true) {
        // Look for the download link label
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

// 4. Function to retrieve the file size (in MB)
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
            // On cherche la clé "size" juste après
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


// 5. Function to retrieve the file upload date
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

// Function that downloads the large JSON text from GitHub
std::string FetchGitHubReleaseRaw() {
    const char* url = "https://api.github.com/repos/retro-foundry/Perfect-Dark-Plus-HD-Textures/releases/latest";
    std::string result = "";

#ifdef _WIN32
    IStream* stream = nullptr;
    HRESULT hr = URLOpenBlockingStreamA(NULL, url, &stream, 0, NULL);
    if (hr == S_OK && stream) {
        char buffer[1024];
        ULONG bytesRead = 0;
        // La réponse JSON est longue, on lit le flux en boucle !
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
    std::string json = FetchGitHubReleaseRaw();

    // Get the version (e.g. "v1.2")
    std::string version = ExtractJsonValue(json, "tag_name");

    // Get the platform-specific size and date (PC or Quest)
    double sizeMB = ExtractAssetSizeMB(json, TARGET_PACK_KEYWORD);
    std::string date = ExtractAssetDate(json, TARGET_PACK_KEYWORD);

    if (!version.empty() && sizeMB > 0.0 && !date.empty()) {
        // Full display: Version, Date, and Size
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "PD Plus HD Textures Pack\nVersion: %s\nDate: %s\nSize: %.1f MB\n\n", version.c_str(), date.c_str(), sizeMB);
    }
    else if (!version.empty() && sizeMB > 0.0) {
        // If the date cannot be found but the file size is available
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "PD Plus HD Textures Pack\nVersion: %s\nSize: %.1f MB\n\n", version.c_str(), sizeMB);
    }
    else if (!version.empty()) {
        // If only the version is available
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "PD Plus HD Textures Pack\nVersion: %s\n\n", version.c_str());
    }
    else {
        // Full fallback in case of an internet or API error
        snprintf(g_DescriptionText, sizeof(g_DescriptionText), "PD Plus HD Textures Pack\n\n");
    }
}

extern "C" {
const char* GetDescriptionText(void) {
    bool expected = false;
    if (g_DescFetchStarted.compare_exchange_strong(expected, true)) {
        std::thread t(FetchDescWorker);
        t.detach();
    }
    return g_DescriptionText;
}
}


// ============================================================================
// MAIN THREAD (Uses the GitHub API)
// ============================================================================
void DownloadAndExtractWorker() {
    g_CurrentState.store(STATE_DOWNLOADING);
    g_DownloadProgress.store(0.0f);

    // --- 1. RETRIEVE THE DOWNLOAD LINK VIA THE API ---
    std::string json = FetchGitHubReleaseRaw();

    // Use our new function with the platform keyword
    std::string link = ExtractPlatformDownloadUrl(json, TARGET_PACK_KEYWORD);

    if (link.empty()) {
        // If the GitHub API is unreachable or the file does not exist for this platform
        g_CurrentState.store(STATE_ERROR);
        return;
    }

    char dynamicUrl[512] = {0};
    snprintf(dynamicUrl, sizeof(dynamicUrl), "%s", link.c_str());

    // --- 2. PREPARE THE FOLDER ---
    std::filesystem::create_directories(EXT_TEX_DIR);

    // --- 3. NATIVE ZIP DOWNLOAD ---
    bool downloadSuccess = PerformNativeDownload(dynamicUrl, ZIP_FILE_PATH);

    if (!downloadSuccess) {
        g_CurrentState.store(STATE_ERROR);
        return;
    }

    // --- DECOMPRESSION (miniz) ---
    g_CurrentState.store(STATE_EXTRACTING);

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_file(&zip_archive, ZIP_FILE_PATH, 0)) {
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

            targetPath = std::string(EXT_TEX_PREFIX) + subPath;
        } else {
            targetPath = std::string(EXT_TEX_PREFIX) + originalPath;
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
    remove(ZIP_FILE_PATH);
    extTexInit();

    g_CurrentState.store(STATE_FINISHED);
}

// ============================================================================
// C INTERFACE (Linked with optionsmenu.c)
// ============================================================================
extern "C" {

void StartAssetDownloadThread(void) {
    if (g_CurrentState.load() == STATE_DOWNLOADING ||
        g_CurrentState.load() == STATE_EXTRACTING) {
        return;
    }
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


void DeleteAssetFolder(void) {
    std::thread t([]() {
        std::error_code ec;
        std::filesystem::remove_all(EXT_TEX_DIR, ec);
        ResetAssetDownloadState();
    });
    t.detach();
}

int DoesAssetFolderExist(void) {
    std::error_code ec;
    if (std::filesystem::exists(EXT_TEX_DIR, ec)) {
        return 1;
    }
    return 0;
}

}
