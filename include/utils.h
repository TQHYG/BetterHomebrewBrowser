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
    bool IsValidURLSCE(const char *url, int32_t timeoutMs = 5000, int *retOut = nullptr); //Can this URL be used with SceHttp?

    int DownloadFile(const char *url, const char *path);
    void Decapitalise(wchar_t *wcstring);

    // Append a formatted line to savedata0:bhbb_log.txt. Unlike print(), this
    // always runs in release builds, so failures can be diagnosed on device.
    void Log(const char *fmt, ...);

    // Plain libcurl helpers. These replace paf_file_ext::CurlFile on the
    // network paths: the prebuilt libPafFileExt.a shipped with the SDK was
    // compiled against a newer PAF API (int64_t based File::WaitAsync / Seek /
    // GetFileSize) than the PAF 3.57 headers this build links against.
    int HttpGet(const char *url, paf::string &out);
    int HttpGetBinary(const char *url, char **outData, size_t *outSize); // caller frees with sce_paf_free
    int ResolveFinalURL(const char *url, paf::string &outURL);           // follows redirects, returns effective URL

    // Picks the url handed to SceDownload for a download.
    //
    // For every candidate the redirect chain is resolved first (so jump links
    // like get_hb_url.php or github release assets become direct file urls),
    // then up to four 5s probes are run in this order:
    //   1. resolved url as-is (https preferred)
    //   2. resolved url with the http downgrade
    //   3. original url as-is
    //   4. original url with the http downgrade
    // If nothing passes, the resolved http form is handed over anyway so that
    // bgdl can report its own error instead of failing locally.
    int PickDownloadURL(const std::vector<paf::string> &urls, paf::string &out);
};
#endif