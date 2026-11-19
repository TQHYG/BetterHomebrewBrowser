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

#include <paf.h>

#include "db/neovitadb.h"
#include "bhbb_locale.h"
#include "utils.h"
#include "print.h"
#include "json.h"

// NeoVitaDB serves a static catalog from GitHub pages:
//   <base>/vita.json                    the index (every entry as a flat object)
//   <base>/icons/<icon>                 square icon png
//   <base>/<screenshot>                 screenshots, "a;b;c" in the index
// Every value in the index is a string, so everything is read defensively.
#define NEOVITADB_CATALOG_BASE "https://robin994.github.io/NeoVitaDB-Catalog"
#define NEOVITADB_INDEX_URL    NEOVITADB_CATALOG_BASE "/vita.json"

// The index is roughly 2.2 MiB. Parsing is zero copy (the input is a mutable
// char*, so ArduinoJson keeps pointers instead of duplicating every string), so
// the pool only has to hold the node tree: 1314 entries x ~30 members.
// A single 4 MiB PAF heap allocation failed on hardware, 2 MiB is the value we
// log and tune from now on.
#define NEOVITADB_JSON_CAPACITY (2 * 1024 * 1024)

using namespace paf;
using namespace paf::common;

namespace
{
    // The catalog stores everything as a string, but numbers show up occasionally
    // and as<const char *>() returns nullptr for those.
    paf::string JsonText(JsonVariant value)
    {
        if(value.is<const char *>())
        {
            const char *str = value.as<const char *>();
            return str ? paf::string(str) : paf::string("");
        }

        if(value.is<long long>())
        {
            char buffer[32];
            sce_paf_snprintf(buffer, sizeof(buffer), "%lld", value.as<long long>());
            return paf::string(buffer);
        }

        return paf::string("");
    }

    void JsonText(JsonVariant value, paf::wstring *out)
    {
        paf::string str = JsonText(value);
        common::Utf8ToUtf16(str, out);
    }

    // "screenshots/1-1.png;screenshots/1-2.png" -> two absolute urls.
    void AppendScreenshotURLs(const char *list, std::vector<paf::string> &out)
    {
        if(!list || list[0] == 0)
            return;

        const char *start = list;

        while(true)
        {
            const char *end = start;
            while(*end != 0 && *end != ';')
                end++;

            size_t len = (size_t)(end - start);
            if(len > 0)
            {
                char *token = (char *)sce_paf_malloc(len + 1);
                if(token)
                {
                    sce_paf_memcpy(token, start, len);
                    token[len] = 0;

                    paf::string url = NEOVITADB_CATALOG_BASE "/";
                    url += token;
                    out.push_back(url);

                    sce_paf_free(token);
                }
            }

            if(*end == 0)
                break;

            start = end + 1;
        }
    }
}

NeoVitaDB::NeoVitaDB()
{
    iconFolderPath = "ux0:/data/betterHomebrewBrowser/icons/neovitadb/";
    iconsURL = NEOVITADB_CATALOG_BASE "/icons.db";
    screenshotsSupported = true;
    categories = {
        {
            CategoryAll,
            db_category_all,
            db_category_all
        },
        {
            GAME,
            db_category_single_game,
            db_category_game
        },
        {
            PORT,
            db_category_single_port,
            db_category_port
        },
        {
            EMU,
            db_category_single_emu,
            db_category_emu
        },
        {
            UTIL,
            db_category_single_util,
            db_category_util
        }
    };
    sortModes = {
        {
            msg_sort_mostrecent,
            List::Sort_MostRecent
        },
        {
            msg_sort_oldfirst,
            List::Sort_OldestFirst
        },
        {
            msg_sort_alpha,
            List::Sort_Alphabetical       
        },
        {
            msg_sort_alpharev,
            List::Sort_AlphabeticalRev
        },
        {
            msg_sort_mostdownloaded,
            List::Sort_MostDownloaded
        },
        {
            msg_sort_leastdownloaded,
            List::Sort_LeastDownloaded
        }
    };
    paf::Dir::CreateRecursive(iconFolderPath.c_str());
}

NeoVitaDB::~NeoVitaDB()
{

}

int NeoVitaDB::DownloadIndex(bool forceRefresh)
{
    // The catalog has no cheap way to ask "did anything change", so always fetch.
    indexData = "";

    Utils::Log("[NeoVitaDB] fetching index\n");

    int ret = Utils::HttpGet(NEOVITADB_INDEX_URL, indexData);
    if(ret != SCE_OK)
    {
        Utils::Log("[NeoVitaDB] index download FAIL 0x%X\n", ret);
        return ret;
    }

    Utils::Log("[NeoVitaDB] index %u bytes\n", (unsigned int)indexData.size());

    return SCE_OK;
}

int NeoVitaDB::GetSCECompatibleURL(std::vector<paf::string> &urlList, paf::string &out)
{
    // Same probing order as CBPSDB/VHBD; see Utils::PickDownloadURL.
    return Utils::PickDownloadURL(urlList, out);
}

int NeoVitaDB::GetDownloadURL(Source::Entry& entry, paf::string& out)
{
    return GetSCECompatibleURL(entry.downloadURL, out);
}

int NeoVitaDB::GetDataURL(Source::Entry& entry, paf::string& out)
{
    return GetSCECompatibleURL(entry.dataURL, out);
}

int NeoVitaDB::GetDescription(Source::Entry& entry, paf::wstring& out)
{
    out = entry.description;
    return 0;
}

int NeoVitaDB::Parse()
{
    if(indexData.size() == 0)
    {
        Utils::Log("[NeoVitaDB] parse without index\n");
        return -1;
    }

    pList->Clear();

    DynamicJsonDocument jdoc(NEOVITADB_JSON_CAPACITY);

    // capacity() is 0 when the pool allocation itself failed, which is a very
    // different problem from the pool running out while parsing.
    Utils::Log("[NeoVitaDB] json capacity %u for %u bytes of index\n",
        (unsigned int)jdoc.capacity(), (unsigned int)indexData.size());

    if(jdoc.capacity() == 0)
    {
        Utils::Log("[NeoVitaDB] json pool allocation failed\n");
        return -1;
    }

    DeserializationError err = deserializeJson(jdoc, indexData.c_str());
    if(err != DeserializationError::Ok)
    {
        Utils::Log("[NeoVitaDB] json error 0x%X (%s), used %u of %u\n", err.code(), err.c_str(),
            (unsigned int)jdoc.memoryUsage(), (unsigned int)jdoc.capacity());
        return err.code();
    }

    Utils::Log("[NeoVitaDB] json parsed, used %u of %u\n",
        (unsigned int)jdoc.memoryUsage(), (unsigned int)jdoc.capacity());

    size_t elemCount = jdoc.size();
    Utils::Log("[NeoVitaDB] parsing %u entries\n", (unsigned int)elemCount);

    for(size_t i = 0; i < elemCount; i++)
    {
        JsonVariant item = jdoc[i];

        // The id is what the list keys on, skip entries without one.
        paf::string idText = JsonText(item["id"]);
        if(idText.size() == 0)
            continue;

        Source::Entry entry(this);

        entry.hash = IDParam(idText.c_str()).GetIDHash();

        JsonText(item["titleid"], &entry.titleID);
        JsonText(item["name"], &entry.title);
        JsonText(item["author"], &entry.author);
        JsonText(item["version"], &entry.version);
        JsonText(item["long_description"], &entry.description);

        paf::string iconName = JsonText(item["icon"]);
        if(iconName.size() > 0)
        {
            char iconPath[0x120];
            sce_paf_snprintf(iconPath, sizeof(iconPath), "%s%s", iconFolderPath.c_str(), iconName.c_str());
            entry.iconPath = iconPath;

            paf::string iconURL = NEOVITADB_CATALOG_BASE "/icons/";
            iconURL += iconName.c_str();
            entry.iconURL.push_back(iconURL);
        }

        paf::string screenshots = JsonText(item["screenshots"]);
        AppendScreenshotURLs(screenshots.c_str(), entry.screenshotURL);

        entry.dataPath = "ux0:data/";
        // Every value in the catalog is a json string, so parse the numbers
        // explicitly instead of relying on ArduinoJson's string to int rules.
        entry.category = (int)sce_paf_strtoul(JsonText(item["type"]).c_str(), nullptr, 10);
        entry.lastUpdated.ParseSQLiteDateTime(JsonText(item["date"]).c_str());
        entry.downloadNum = (unsigned int)sce_paf_strtoul(JsonText(item["downloads"]).c_str(), nullptr, 10);

        paf::string downloadURL = JsonText(item["url"]);
        if(downloadURL.size() > 0)
            entry.downloadURL.push_back(downloadURL);

        entry.downloadSize = (size_t)sce_paf_strtoull(JsonText(item["size"]).c_str(), nullptr, 10);
        entry.dataSize = (size_t)sce_paf_strtoull(JsonText(item["data_size"]).c_str(), nullptr, 10);

        if(entry.dataSize != 0)
        {
            paf::string dataURL = JsonText(item["data"]);
            if(dataURL.size() > 0)
                entry.dataURL.push_back(dataURL);
        }

        pList->Add(entry);
    }

    Utils::Log("[NeoVitaDB] parsed %u entries\n", (unsigned int)pList->GetSize(CategoryAll));

    return 0;
}
