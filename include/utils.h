/* 
    BetterHomebrewBrowser, A homebrew browser for the PlayStation Vita with background downloading support
    Copyright (C) 2023 Muhammad Ibrahim
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
#ifndef _UTILS_H_
#define _UTILS_H_
#include <paf.h>
#include <vector>
namespace Utils
{
    void InitMusic();
    SceVoid StartBGDL();
 
    void HttpsToHttp(const char *src, paf::string &outURL);
    int DownloadFile(const char *url, const char *path);
    void Decapitalise(wchar_t *wcstring);
    // Append a formatted line to ux0:/data/betterHomebrewBrowser/bhbb_log.txt.
    // Unlike print(), this always runs in release builds, so failures can be
    // diagnosed on device.
    void Log(const char *fmt, ...);
    // Logging is off by default and controlled by the settings page, so a
    // normal run does not write anything to the memory card.
    void SetLogEnabled(bool enabled);
    bool IsLogEnabled();
    // Plain libcurl helpers. These replace paf_file_ext::CurlFile on the
    // network paths: the prebuilt libPafFileExt.a shipped with the SDK was
    // compiled against a newer PAF API (int64_t based File::WaitAsync / Seek /
    // GetFileSize) than the PAF 3.57 headers this build links against.
    int HttpGet(const char *url, paf::string &out);
    int HttpGetBinary(const char *url, char **outData, size_t *outSize); // caller frees with sce_paf_free
    // Follows the whole redirect chain with libcurl (max 10 hops) and returns
    // the final url. httpStatus receives the status of the last hop (0 when it
    // could not be read) and redirects the number of hops taken.
    int ResolveFinalURL(const char *url, paf::string &outURL, long *httpStatus = nullptr, int32_t *redirects = nullptr);
    // Decides which url is handed to SceDownload.
    //
    // Every candidate is resolved with libcurl first: it follows the hop chain
    // (get_hb_url.php, github release assets, archive.org, ...) and reports the
    // final url plus its http status. The resolved url is delivered as-is
    // (https stays https), because bgdl handles both schemes on this platform
    // and a resolved link needs no further redirects.
    //
    // When nothing resolves, the plain http form of the first candidate is
    // handed over anyway so bgdl can report its own error instead of failing
    // silently here.
    int PickDownloadURL(const std::vector<paf::string> &urls, paf::string &out);
};
#endif
