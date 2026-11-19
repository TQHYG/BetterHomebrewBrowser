/* 
    BetterHomebrewBrowser, A homebrew browser for the PlayStation Vita with background downloading support
    Copyright (C) 2025 Muhammad Ibrahim

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.    
*/

#include <audioout.h>
#include <paf.h>
#include <ShellAudio.h>
#include <taihen.h>
#include <appmgr.h>
#include <psp2_compat/curl/curl.h>

#include "utils.h"
#include "print.h"
#include "common.h"
#include "bhbb_dl.h"
#include "error_codes.h"

using namespace paf;
using namespace paf::common;

void Utils::HttpsToHttp(const char *src, paf::string& outURL)
{ 
    if(!src)
    {
        outURL = "";
        return;
    }

    if(sce_paf_strncmp(src, "https", 5) != 0)
    {
        outURL = src;
        return;
    }

    // Built by hand instead of with common::FormatString: this runs on the
    // download job thread, and FormatString is also used concurrently by the
    // icon download workers. Dropping the shared helper here removed the
    // intermittent garbage urls ("http" -> " C   ") seen in the device log.
    outURL = "http";
    outURL += &src[5];
}

bool Utils::IsValidURLSCE(const char *url, int32_t timeoutMs, int *retOut)
{
    print("[Utils::IsValidURLSCE] START\n");

    paf::HttpFile file;
    paf::HttpFile::OpenArg openArg;
    SceInt32 ret = SCE_OK;

    if(retOut)
        *retOut = SCE_OK;

    if(!url || url[0] == 0)
    {
        if(retOut)
            *retOut = -1;
        return false;
    }

    if(timeoutMs <= 0)
        timeoutMs = 5000;

    openArg.ParseUrl(url);
    openArg.SetOption(timeoutMs, HttpFile::OpenArg::OptionType_ResolveTimeOut);
	openArg.SetOption(timeoutMs, HttpFile::OpenArg::OptionType_ConnectTimeOut);
	openArg.SetOption(timeoutMs, HttpFile::OpenArg::OptionType_SendTimeOut);
	openArg.SetOption(timeoutMs, HttpFile::OpenArg::OptionType_RecvTimeOut);

    ret = file.Open(&openArg);

    if(retOut)
        *retOut = ret;

    // if(ret == SCE_HTTP_ERROR_SSL) ret = SCE_OK; // Temporary fix till I figure out wot is going on with this SSL stuff (SceDownload should report proper error anyways)

    if(ret == SCE_OK)
    {
        file.Close();
        return true;
    }
    print("Open %s FAIL 0x%X\n", url, ret);
    Utils::Log("[Utils::IsValidURLSCE] %s FAIL 0x%X\n", url, ret);

    print("[Utils::IsValidURLSCE] END\n");

    return false;
}

void Utils::InitMusic()
{
    SceInt32 ret = -1;

    ret = sceMusicInternalAppInitialize(0);
    print("[sceMusicInternalAppInitialize] 0x%X\n", ret);

    SceMusicOpt optParams;
    sce_paf_memset(&optParams, 0, 0x10);

    optParams.flag = -1;

    ret = sceMusicInternalAppSetUri((char *)"pd0:data/systembgm/store.at9", &optParams);
    print("[sceMusicInternalAppSetUri] 0x%X\n", ret);

    ret = sceMusicInternalAppSetVolume(SCE_AUDIO_VOLUME_0DB);
    print("[sceMusicInternalAppSetVolume] 0x%X\n", ret);

    ret = sceMusicInternalAppSetRepeatMode(SCE_MUSIC_REPEAT_ONE);
    print("[sceMusicInternalAppSetRepeatMode] 0x%X\n", ret);

    ret = sceMusicInternalAppSetPlaybackCommand(SCE_MUSIC_EVENTID_DEFAULT, 0);
    print("[sceMusicInternalAppSetPlaybackCommand] 0x%X\n", ret);
}

namespace
{
    const size_t MAX_DOWNLOAD_SIZE = 32 * 1024 * 1024;

    struct DownloadBuffer
    {
        uint8_t *data;
        size_t size;
        size_t capacity;
    };

    size_t SaveToMemory(char *ptr, size_t size, size_t nmeb, DownloadBuffer *buffer)
    {
        if(size != 0 && nmeb > (size_t)-1 / size)
            return 0;

        size_t bytes = size * nmeb;
        if(bytes > MAX_DOWNLOAD_SIZE - buffer->size)
            return 0;

        size_t required = buffer->size + bytes;
        if(required > buffer->capacity)
        {
            size_t newCapacity = buffer->capacity ? buffer->capacity : 16 * 1024;
            while(newCapacity < required)
                newCapacity *= 2;

            uint8_t *newData = (uint8_t *)sce_paf_realloc(buffer->data, newCapacity);
            if(!newData)
                return 0;

            buffer->data = newData;
            buffer->capacity = newCapacity;
        }

        sce_paf_memcpy(buffer->data + buffer->size, ptr, bytes);
        buffer->size = required;
        return bytes;
    }

    size_t WriteToString(char *ptr, size_t size, size_t nmemb, paf::string *str)
    {
        if(!str || size == 0 || nmemb == 0)
            return 0;

        size_t bytes = size * nmemb;
        if(bytes > MAX_DOWNLOAD_SIZE)
            return 0;

        char *tmp = (char *)sce_paf_malloc(bytes + 1);
        if(!tmp)
            return 0;

        sce_paf_memcpy(tmp, ptr, bytes);
        tmp[bytes] = 0;

        *str += tmp;

        sce_paf_free(tmp);

        return bytes;
    }

    CURL *CreateHttpGetHandle(const char *url)
    {
        CURL *handle = curl_easy_init();
        if(!handle)
            return nullptr;

        curl_easy_setopt(handle, CURLOPT_URL, url);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 1L);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 180L);
        curl_easy_setopt(handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");

        return handle;
    }

    int WriteFileAtomically(const char *dest, const uint8_t *data, size_t size)
    {
        // Plain snprintf instead of common::FormatString: this runs on three
        // icon download worker threads at once and that helper is not reentrant.
        char tempPath[512];
        sce_paf_snprintf(tempPath, sizeof(tempPath), "%s.part.%08x", dest, sceKernelGetThreadId());

        LocalFile::RemoveFile(tempPath);

        int ret = SCE_OK;
        SharedPtr<LocalFile> file = LocalFile::Open(tempPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666, &ret);
        if(ret != SCE_OK || !file.get())
            return ret != SCE_OK ? ret : -1;

        size_t written = 0;
        while(written < size)
        {
            size_t result = file.get()->Write((char *)data + written, size - written);
            if(result == 0 || result > size - written)
            {
                file.reset();
                LocalFile::RemoveFile(tempPath);
                return -1;
            }
            written += result;
        }

        // Close before rename/removal. Leaving an open handle behind can damage
        // directory state on exFAT when downloads fail or are retried.
        file.reset();

        if(LocalFile::Exists(dest))
        {
            LocalFile::RemoveFile(tempPath);
            return SCE_OK;
        }

        ret = sceIoRename(tempPath, dest);
        if(ret != SCE_OK)
        {
            LocalFile::RemoveFile(tempPath);
            return ret;
        }

        return SCE_OK;
    }
}

int Utils::DownloadFile(const char *url, const char *dest)
{
    if(!url || !dest)
        return -1;

    DownloadBuffer buffer = { nullptr, 0, 0 };
    CURL *handle = curl_easy_init();
    if(!handle)
    {
        print("[Utils::DownloadFile] Failed to create curl handle!\n");
        return -1;
    }

    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 1L);
    curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
    curl_easy_setopt(handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);

#ifdef _DEBUG
    curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
#endif

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, SaveToMemory);

    int ret = curl_easy_perform(handle);
    curl_easy_cleanup(handle);

    if(ret == CURLE_OK && buffer.size == 0)
        ret = CURLE_GOT_NOTHING;

    if(ret == CURLE_OK)
        ret = WriteFileAtomically(dest, buffer.data, buffer.size);

    sce_paf_free(buffer.data);
    print("[Utils::DownloadFile] (%s -> %s) result > 0x%X, %u bytes\n", url, dest, ret, buffer.size);
    return ret;
}

#define BHBB_LOG_DIR  "ux0:/data/betterHomebrewBrowser/"
#define BHBB_LOG_PATH "ux0:/data/betterHomebrewBrowser/bhbb_log.txt"
#define BHBB_LOG_MAX  (256 * 1024)

void Utils::Log(const char *fmt, ...)
{
    static bool ready = false;
    static paf::thread::Mutex *logMutex = nullptr;

    char line[1024];
    va_list args;

    va_start(args, fmt);
    sceClibVsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    size_t len = sce_paf_strlen(line);
    if(len == 0)
        return;

    // A long url can fill the whole buffer; keep every record newline
    // terminated so the file stays parseable when truncated.
    if(line[len - 1] != '\n')
    {
        if(len >= sizeof(line) - 1)
            len = sizeof(line) - 2;

        line[len] = '\n';
        line[len + 1] = 0;
    }

    if(!logMutex)
        logMutex = new paf::thread::Mutex("BHBB::Log");

    logMutex->Lock();

    int ret = SCE_OK;

    if(!ready)
    {
        // Same folder the icon caches use, so it always exists on device.
        paf::Dir::CreateRecursive(BHBB_LOG_DIR);

        // Keep the file bounded, a previous session's log is the most useful part.
        auto probe = LocalFile::Open(BHBB_LOG_PATH, SCE_O_RDONLY, 0, &ret);
        if(ret == SCE_OK && probe.get())
        {
            auto size = probe.get()->GetFileSize();
            probe.reset();

            if(size > BHBB_LOG_MAX)
                LocalFile::RemoveFile(BHBB_LOG_PATH);
        }

        ready = true;
    }

    ret = SCE_OK;
    SharedPtr<LocalFile> file = LocalFile::Open(BHBB_LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666, &ret);
    if(ret == SCE_OK && file.get())
        file.get()->Write(line, sce_paf_strlen(line));

    logMutex->Unlock();
}

int Utils::HttpGet(const char *url, paf::string &out)
{
    out = "";

    if(!url || url[0] == 0)
    {
        Utils::Log("[Utils::HttpGet] empty url\n");
        return -1;
    }

    CURL *handle = CreateHttpGetHandle(url);
    if(!handle)
        return -1;

    curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteToString);

    int ret = curl_easy_perform(handle);
    curl_easy_cleanup(handle);

    if(ret != CURLE_OK || out.length() == 0)
    {
        Utils::Log("[Utils::HttpGet] %s FAIL 0x%X\n", url, ret != CURLE_OK ? ret : -2);
        return ret != CURLE_OK ? ret : -2;
    }

    return SCE_OK;
}

int Utils::HttpGetBinary(const char *url, char **outData, size_t *outSize)
{
    if(outData)
        *outData = nullptr;
    if(outSize)
        *outSize = 0;

    if(!url || !outData || !outSize || url[0] == 0)
    {
        Utils::Log("[Utils::HttpGetBinary] empty url\n");
        return -1;
    }

    CURL *handle = CreateHttpGetHandle(url);
    if(!handle)
        return -1;

    DownloadBuffer buffer = { nullptr, 0, 0 };

    curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, SaveToMemory);

    int ret = curl_easy_perform(handle);
    curl_easy_cleanup(handle);

    if(ret != CURLE_OK || buffer.size == 0)
    {
        Utils::Log("[Utils::HttpGetBinary] %s FAIL 0x%X\n", url, ret != CURLE_OK ? ret : -2);
        sce_paf_free(buffer.data);
        return ret != CURLE_OK ? ret : -2;
    }

    *outData = (char *)buffer.data;
    *outSize = buffer.size;

    return SCE_OK;
}

int Utils::ResolveFinalURL(const char *url, paf::string &outURL)
{
    outURL = "";

    if(!url || url[0] == 0)
    {
        Utils::Log("[Utils::ResolveFinalURL] empty url\n");
        return -1;
    }

    CURL *handle = CreateHttpGetHandle(url);
    if(!handle)
        return -1;

    // Only the headers are needed; the effective URL is what we are after. Keep
    // this short: it is the first half of a 5s probe budget.
    curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);

    int ret = curl_easy_perform(handle);

    char *effectiveURL = nullptr;
    if(ret == CURLE_OK)
        curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &effectiveURL);

    curl_easy_cleanup(handle);

    if(ret != CURLE_OK || effectiveURL == nullptr || effectiveURL[0] == 0)
    {
        Utils::Log("[Utils::ResolveFinalURL] %s FAIL 0x%X\n", url, ret != CURLE_OK ? ret : -2);
        return ret != CURLE_OK ? ret : -2;
    }

    outURL = effectiveURL;

    return SCE_OK;
}

int Utils::PickDownloadURL(const std::vector<paf::string> &urls, paf::string &out)
{
    out = "";

    if(urls.size() == 0)
    {
        Utils::Log("[Utils] no download url candidate\n");
        return -1;
    }

    // Probe budget per url: 5s, four probes at most.
    const int32_t probeTimeoutMs = 5000;

    paf::string fallback;
    int probeIndex = 0;

    for(auto &url : urls)
    {
        if(url.size() == 0)
            continue;

        // Resolve the hop chain once: it tells us whether this is a jump link
        // (get_hb_url.php, github release assets, ...) or already a direct
        // link, and it is what SceDownload needs to fetch.
        paf::string resolved;
        if(Utils::ResolveFinalURL(url.c_str(), resolved) != SCE_OK)
            resolved = url;

        bool isDirect = (sce_paf_strcmp(resolved.c_str(), url.c_str()) == 0);

        paf::string resolvedHttp;
        Utils::HttpsToHttp(resolved.c_str(), resolvedHttp);
        if(resolvedHttp.size() == 0)
            resolvedHttp = resolved;

        Utils::Log("[Utils] candidate %s (%s)\n", url.c_str(), isDirect ? "direct" : "jump");

        // Probe order: resolved as-is, resolved http, original as-is, original http.
        paf::string probes[4];
        int probeCount = 0;

        probes[probeCount++] = resolved;

        if(sce_paf_strcmp(resolvedHttp.c_str(), resolved.c_str()) != 0)
            probes[probeCount++] = resolvedHttp;

        if(!isDirect)
        {
            probes[probeCount++] = url;

            paf::string urlHttp;
            Utils::HttpsToHttp(url.c_str(), urlHttp);
            if(probeCount < 4 && sce_paf_strcmp(urlHttp.c_str(), url.c_str()) != 0)
                probes[probeCount++] = urlHttp;
        }

        for(int i = 0; i < probeCount && i < 4; i++)
        {
            int err = 0;
            probeIndex++;

            if(Utils::IsValidURLSCE(probes[i].c_str(), probeTimeoutMs, &err))
            {
                Utils::Log("[Utils] probe %d ok: %s\n", probeIndex, probes[i].c_str());
                out = probes[i];
                return SCE_PAF_OK;
            }

            Utils::Log("[Utils] probe %d rejected 0x%X: %s\n", probeIndex, err, probes[i].c_str());
        }

        if(fallback.size() == 0)
        {
            // Hand over the resolved http form: an unresolved jump link cannot
            // be fetched by the background downloader and failing here would
            // only hide the real error from bgdl.
            fallback = resolvedHttp;
        }
    }

    if(fallback.size() != 0)
    {
        Utils::Log("[Utils] no probe passed, handing %s to SceDownload\n", fallback.c_str());
        out = fallback;
        return SCE_PAF_OK;
    }

    Utils::Log("[Utils] no usable download url\n");
    return -1;
}

void Utils::Decapitalise(wchar_t *wcstring)
{
    //Convert to lowerCase
    for(int i = 0; wcstring[i] != '\0'; i++)
        wcstring[i] = sce_paf_towlower(wcstring[i]);
}

void Utils::StartBGDL()
{
    SceInt32 res = SCE_OK;
    SceUID moduleID = SCE_UID_INVALID_UID;
    SceUID sceShellID = SCE_UID_INVALID_UID;

    res = sceAppMgrGetIdByName(&sceShellID, "NPXS19999");
    if(res != SCE_OK)
    {
        print("Unable to get SceShell ID! 0x%X\n", res);
        return;
    }
#ifdef _DEBUG
    if(LocalFile::Exists("ux0:data/bgdlid"))
    {
        SceUID id;
        
        common::SharedPtr<LocalFile> openResult = LocalFile::Open("ux0:data/bgdlid", SCE_O_RDONLY, 0, NULL);
        openResult.get()->Read(&id, sizeof(SceUID));
        print("Unloading....\n");
        taiStopUnloadModuleForPid(sceShellID, id, 0, NULL, 0, NULL, NULL);
        print("Done!\n");
    }
#endif
    res = taiLoadStartModuleForPid(sceShellID, "ux0:app/BHBB00001/module/bhbb_dl.suprx", 0, NULL, 0);
    if(res < 0)
    {
        print("Unable to start BGDL! (Already Running?) 0x%X\n", res);
        return;
    }

    moduleID = res;
    
    print("BGDL started with ID 0x%X\n", moduleID);
#ifdef _DEBUG
    common::SharedPtr<LocalFile> openResult = LocalFile::Open("ux0:data/bgdlid", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666, NULL);
    openResult.get()->Write(&moduleID, sizeof(SceUID));
#endif
}