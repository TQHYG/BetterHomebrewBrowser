/* 
    BetterHomebrewBrowser, A homebrew browser for the PlayStation Vita with background downloading support
    Copyright (C) 2024 Muhammad Ibrahim

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

#ifndef _NEOVITADB_H_
#define _NEOVITADB_H_
#include <paf.h>
#include "source.h"
class NeoVitaDB : public Source
{
public:
    NeoVitaDB();
    virtual ~NeoVitaDB();
    
    int Parse();
    int DownloadIndex(bool forceRefresh);
    int GetDescription(Entry &entry, paf::wstring& out);
    int GetDownloadURL(Entry &entry, paf::string& out);
    int GetDataURL(Entry &entry, paf::string& out);
    wchar_t *GetName()
    {
        return L"NeoVitaDB";
    }
protected:
    static int GetSCECompatibleURL(std::vector<paf::string> &urlList, paf::string &out);
    // Raw json as served by the catalog, kept alive so Parse() can walk it.
    paf::string indexData;
    enum Category 
    {
        GAME = 1,
        PORT = 2,
        UTIL = 4,
        EMU = 5,
    };
};
#endif
