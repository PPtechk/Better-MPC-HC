/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "SkipStructureProvider.h"
#include "MatroskaChapterReader.h"
#include "mplayerc.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    // The files nearest to the current one in the name order are read at most
    const size_t MAX_SIBLINGS = 60;
    // Cached chapter tables, a series is read once, not at every episode
    const size_t MAX_CACHE_ENTRIES = 512;

    struct CachedTable {
        ULONGLONG ullSize;
        FILETIME ftWrite;
        bool bValid;
        SkipChapterTable table;
    };

    struct TableCache {
        std::mutex lock;
        std::map<std::wstring, CachedTable> entries;
    };

    // Never destroyed: a worker thread can still run while the program exits
    TableCache& GetTableCache()
    {
        static TableCache* pCache = new TableCache;
        return *pCache;
    }

    bool ReadTable(const CString& strPath, const WIN32_FIND_DATA& fd, SkipChapterTable& table)
    {
        const ULONGLONG ullSize = (ULONGLONG(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        const std::wstring key = CStringW(strPath).MakeLower().GetString();

        TableCache& cache = GetTableCache();
        {
            std::lock_guard<std::mutex> lock(cache.lock);
            const auto it = cache.entries.find(key);
            if (it != cache.entries.end() && it->second.ullSize == ullSize
                    && CompareFileTime(&it->second.ftWrite, &fd.ftLastWriteTime) == 0) {
                table = it->second.table;
                return it->second.bValid;
            }
        }

        CachedTable entry;
        entry.ullSize = ullSize;
        entry.ftWrite = fd.ftLastWriteTime;
        entry.bValid = ReadMatroskaChapterTable(strPath, entry.table);

        std::lock_guard<std::mutex> lock(cache.lock);
        if (cache.entries.size() >= MAX_CACHE_ENTRIES) {
            cache.entries.clear();
        }
        cache.entries[key] = entry;

        table = entry.table;
        return entry.bValid;
    }

    struct FolderFile {
        CString name;
        WIN32_FIND_DATA fd;
    };
}

CStructureSkipSegmentProvider::CStructureSkipSegmentProvider(const CComPtr<IDSMChapterBag>& pCB, std::function<CString()> getCurrentPath)
    : m_pCB(pCB)
    , m_getCurrentPath(getCurrentPath)
    , m_bLoadConsumed(false)
{
}

CStructureSkipSegmentProvider::~CStructureSkipSegmentProvider()
{
    Reset();
}

bool CStructureSkipSegmentProvider::IsDirty() const
{
    return m_pCB.p != m_pLastCB.p || (m_pLoad && m_pLoad->bDone && !m_bLoadConsumed);
}

bool CStructureSkipSegmentProvider::IsPending() const
{
    return m_pLoad && !m_bLoadConsumed;
}

void CStructureSkipSegmentProvider::Reset()
{
    if (m_pLoad) {
        m_pLoad->bCancel = true;
        m_pLoad.reset();
    }
    m_strLoadPath.Empty();
    m_bLoadConsumed = false;
}

void CStructureSkipSegmentProvider::GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments)
{
    segments.clear();

    m_pLastCB = m_pCB;

    if (!AfxGetAppSettings().bSkipByChapterStructure || rtDuration <= 0 || !m_pLastCB || m_pLastCB->ChapGetCount() < 2) {
        return;
    }

    const CString strPath = m_getCurrentPath();
    if (strPath.IsEmpty() || strPath.Find(_T("://")) >= 0 || !IsMatroskaFileName(strPath)) {
        return;
    }

    if (!m_pLoad || strPath != m_strLoadPath) {
        Reset();
        m_pLoad = std::make_shared<SiblingLoad>();
        m_strLoadPath = strPath;
        std::thread(&CStructureSkipSegmentProvider::LoadSiblings, m_pLoad, strPath).detach();
        return; // IsDirty() reports when the other files are read
    }

    if (!m_pLoad->bDone) {
        return;
    }
    m_bLoadConsumed = true;

    SkipChapterTable current;
    current.rtDuration = rtDuration;
    for (DWORD i = 0, cnt = m_pLastCB->ChapGetCount(); i < cnt; i++) {
        REFERENCE_TIME rt;
        if (SUCCEEDED(m_pLastCB->ChapGet(i, &rt, nullptr))) {
            current.starts.push_back(rt);
        }
    }
    std::sort(current.starts.begin(), current.starts.end());
    current.starts.erase(std::unique(current.starts.begin(), current.starts.end()), current.starts.end());

    for (const SkipStructuralSegment& found : FindStructuralSkipSegments(current, m_pLoad->tables)) {
        segments.push_back({ found.rtStart, found.rtStop,
                             found.bIntro ? CString(_T("Intro")) : CString(_T("Outro")),
                             found.bIntro ? SKIP_INTRO : SKIP_OUTRO });
    }
}

void CStructureSkipSegmentProvider::LoadSiblings(std::shared_ptr<SiblingLoad> pLoad, CString strPath)
{
    const int iSeparator = std::max(strPath.ReverseFind(_T('\\')), strPath.ReverseFind(_T('/')));
    const CString strFolder = strPath.Left(iSeparator + 1);
    const CString strFileName = strPath.Mid(iSeparator + 1);
    const int iExtension = strFileName.ReverseFind(_T('.'));
    const CString strExtension = (iExtension >= 0) ? strFileName.Mid(iExtension) : CString();

    std::vector<FolderFile> files;
    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(strFolder + _T("*") + strExtension, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            const CString strName = fd.cFileName;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strName.GetLength() > strExtension.GetLength()
                    && !strName.Right(strExtension.GetLength()).CompareNoCase(strExtension)) {
                FolderFile file;
                file.name = strName;
                file.fd = fd;
                files.push_back(file);
            }
        } while (FindNextFile(hFind, &fd) && !pLoad->bCancel);
        FindClose(hFind);
    }

    std::sort(files.begin(), files.end(), [](const FolderFile & a, const FolderFile & b) {
        return a.name.CompareNoCase(b.name) < 0;
    });

    // a long folder: the files around the current one
    size_t first = 0, last = files.size();
    if (files.size() > MAX_SIBLINGS + 1) {
        size_t current = 0;
        for (size_t i = 0; i < files.size(); i++) {
            if (!files[i].name.CompareNoCase(strFileName)) {
                current = i;
                break;
            }
        }
        first = (current > MAX_SIBLINGS / 2) ? current - MAX_SIBLINGS / 2 : 0;
        last = std::min(files.size(), first + MAX_SIBLINGS + 1);
        first = (last > MAX_SIBLINGS + 1) ? last - (MAX_SIBLINGS + 1) : 0;
    }

    std::vector<SkipChapterTable> tables;
    for (size_t i = first; i < last && !pLoad->bCancel; i++) {
        if (!files[i].name.CompareNoCase(strFileName)) {
            continue; // the current file is known from the player
        }

        SkipChapterTable table;
        if (ReadTable(strFolder + files[i].name, files[i].fd, table)) {
            tables.push_back(table);
        }
    }

    if (!pLoad->bCancel) {
        pLoad->tables = std::move(tables);
        pLoad->bDone = true;
    }
}
