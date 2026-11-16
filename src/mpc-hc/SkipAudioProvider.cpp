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
#include "SkipAudioProvider.h"
#include "SkipAudioDecoder.h"
#include "SkipAudioFingerprint.h"
#include "mplayerc.h"
#include "PathUtils.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <shlobj.h>

namespace
{
    const REFERENCE_TIME MIN_FILE_DURATION = 5 * 60 * 10000000LL; // shorter files are not episodes
    const size_t MAX_MEMORY_ENTRIES = 400;
    const size_t MAX_DISK_FILES = 4000;
    const size_t DISK_FILES_TO_REMOVE = 1000;
    const UINT32 CACHE_MAGIC = 0x41534D42; // "BMSA"
    const UINT32 CACHE_VERSION = 1;        // to be raised whenever the fingerprints change

#ifdef _WIN64
    const LPCTSTR DECODER_FOLDER = _T("LAVFilters64");
#else
    const LPCTSTR DECODER_FOLDER = _T("LAVFilters");
#endif

    struct FileInfo {
        CString path;
        ULONGLONG size;
        FILETIME ftWrite;
    };

    struct CacheEntry {
        ULONGLONG size;
        FILETIME ftWrite;
        SkipAudioResult result;
        SkipAudioFingerprint fp;
    };

    struct MemoryCache {
        std::mutex lock;
        std::map<std::wstring, CacheEntry> entries;
    };

    // Never destroyed: a worker thread can still run while the program exits
    MemoryCache& GetMemoryCache()
    {
        static MemoryCache* pCache = new MemoryCache;
        return *pCache;
    }

#pragma pack(push, 1)
    struct DiskHeader {
        UINT32 magic;
        UINT32 version;
        UINT64 size;
        UINT64 writeTime;
        INT32 result;
        double duration;
        double startTime;
        UINT32 keyLength; // characters of the key (UTF-16) that follow the header, then the hashes
        UINT32 hashCount;
    };
#pragma pack(pop)

    class CHandle
    {
        HANDLE m_h;
    public:
        explicit CHandle(HANDLE h) : m_h(h) {}
        ~CHandle() {
            Close();
        }
        void Close() {
            if (m_h != INVALID_HANDLE_VALUE) {
                CloseHandle(m_h);
                m_h = INVALID_HANDLE_VALUE;
            }
        }
        operator HANDLE() const { return m_h; }
        CHandle(const CHandle&) = delete;
        CHandle& operator=(const CHandle&) = delete;
    };

    UINT64 ToUInt64(const FILETIME& ft)
    {
        return (UINT64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    std::wstring CacheKey(const FileInfo& file, SkipAudioRegion region)
    {
        CStringW path(file.path);
        path.MakeLower();
        return std::wstring(path.GetString()) + (region == SKIP_AUDIO_OUTRO ? L"|outro" : L"|intro");
    }

    CString DiskPath(const CString& folder, const std::wstring& key)
    {
        UINT64 hash = 14695981039346656037ULL; // FNV-1a
        for (wchar_t c : key) {
            hash ^= UINT64(c);
            hash *= 1099511628211ULL;
        }
        CString name;
        name.Format(_T("%016I64x.fp"), hash);
        return folder + name;
    }

    bool IsSameFile(const CacheEntry& entry, const FileInfo& file)
    {
        return entry.size == file.size && CompareFileTime(&entry.ftWrite, &file.ftWrite) == 0;
    }

    void StoreInMemory(const std::wstring& key, const CacheEntry& entry)
    {
        MemoryCache& cache = GetMemoryCache();
        std::lock_guard<std::mutex> lock(cache.lock);
        if (cache.entries.size() >= MAX_MEMORY_ENTRIES) {
            cache.entries.clear();
        }
        cache.entries[key] = entry;
    }

    bool ReadFromDisk(const CString& path, const std::wstring& key, const FileInfo& file, CacheEntry& entry)
    {
        CHandle h(CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }

        DiskHeader header;
        DWORD read = 0;
        if (!ReadFile(h, &header, sizeof(header), &read, nullptr) || read != sizeof(header)
                || header.magic != CACHE_MAGIC || header.version != CACHE_VERSION
                || header.size != file.size || header.writeTime != ToUInt64(file.ftWrite)
                || header.keyLength != key.size() || header.hashCount > 4000000
                || (header.result != SKIP_AUDIO_OK && header.result != SKIP_AUDIO_NO_AUDIO)) {
            return false;
        }

        std::wstring storedKey(header.keyLength, L'\0');
        const DWORD keyBytes = DWORD(header.keyLength * sizeof(wchar_t));
        if (!ReadFile(h, &storedKey[0], keyBytes, &read, nullptr) || read != keyBytes || storedKey != key) {
            return false;
        }

        entry.fp.hashes.resize(header.hashCount);
        const DWORD hashBytes = DWORD(header.hashCount * sizeof(uint32_t));
        if (hashBytes && (!ReadFile(h, entry.fp.hashes.data(), hashBytes, &read, nullptr) || read != hashBytes)) {
            return false;
        }

        entry.size = file.size;
        entry.ftWrite = file.ftWrite;
        entry.result = SkipAudioResult(header.result);
        entry.fp.duration = header.duration;
        entry.fp.startTime = header.startTime;
        return true;
    }

    void WriteToDisk(const CString& path, const std::wstring& key, const CacheEntry& entry)
    {
        CString tempPath;
        tempPath.Format(_T("%s.%lu.tmp"), path.GetString(), GetCurrentThreadId());

        CHandle h(CreateFile(tempPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (h == INVALID_HANDLE_VALUE) {
            return;
        }

        DiskHeader header;
        header.magic = CACHE_MAGIC;
        header.version = CACHE_VERSION;
        header.size = entry.size;
        header.writeTime = ToUInt64(entry.ftWrite);
        header.result = entry.result;
        header.duration = entry.fp.duration;
        header.startTime = entry.fp.startTime;
        header.keyLength = UINT32(key.size());
        header.hashCount = UINT32(entry.fp.hashes.size());

        const DWORD keyBytes = DWORD(key.size() * sizeof(wchar_t));
        const DWORD hashBytes = DWORD(entry.fp.hashes.size() * sizeof(uint32_t));
        DWORD written = 0;
        bool bOk = WriteFile(h, &header, sizeof(header), &written, nullptr) && written == sizeof(header)
                   && WriteFile(h, key.data(), keyBytes, &written, nullptr) && written == keyBytes;
        if (bOk && hashBytes) {
            bOk = WriteFile(h, entry.fp.hashes.data(), hashBytes, &written, nullptr) && written == hashBytes;
        }
        h.Close();

        if (!bOk || !MoveFileEx(tempPath, path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFile(tempPath);
        }
    }

    // Once per run: the oldest files go when there are too many
    void PruneDiskCache(const CString& folder)
    {
        static std::atomic<bool> s_bPruned(false);
        if (s_bPruned.exchange(true)) {
            return;
        }

        std::vector<std::pair<UINT64, CString>> files;
        WIN32_FIND_DATA fd;
        HANDLE hFind = FindFirstFile(folder + _T("*.fp"), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                files.emplace_back(ToUInt64(fd.ftLastWriteTime), CString(fd.cFileName));
            } while (FindNextFile(hFind, &fd));
            FindClose(hFind);
        }

        if (files.size() > MAX_DISK_FILES) {
            std::sort(files.begin(), files.end());
            for (size_t i = 0; i < DISK_FILES_TO_REMOVE; i++) {
                DeleteFile(folder + files[i].second);
            }
        }
    }

    SkipAudioResult GetFingerprint(const FileInfo& file, SkipAudioRegion region, double minDuration, double maxDuration,
                                   const CString& cacheFolder, const std::atomic<bool>* pCancel, SkipAudioFingerprint& fp)
    {
        fp = SkipAudioFingerprint();

        const std::wstring key = CacheKey(file, region);
        CacheEntry entry;
        bool bCached = false;
        {
            MemoryCache& cache = GetMemoryCache();
            std::lock_guard<std::mutex> lock(cache.lock);
            const auto it = cache.entries.find(key);
            if (it != cache.entries.end() && IsSameFile(it->second, file)) {
                entry = it->second;
                bCached = true;
            }
        }
        if (!bCached && !cacheFolder.IsEmpty() && ReadFromDisk(DiskPath(cacheFolder, key), key, file, entry)) {
            StoreInMemory(key, entry);
            bCached = true;
        }

        if (bCached) {
            if (entry.result != SKIP_AUDIO_OK) {
                return entry.result;
            }
            if ((minDuration > 0.0 && entry.fp.duration < minDuration) || (maxDuration > 0.0 && entry.fp.duration > maxDuration)) {
                fp.duration = entry.fp.duration;
                return SKIP_AUDIO_OUT_OF_RANGE;
            }
            fp = entry.fp;
            return SKIP_AUDIO_OK;
        }

        const SkipAudioResult result = ComputeSkipAudioFingerprint(std::wstring(CStringW(file.path).GetString()), region,
                                                                   minDuration, maxDuration, pCancel, fp);
        if (result == SKIP_AUDIO_OK || result == SKIP_AUDIO_NO_AUDIO) {
            entry.size = file.size;
            entry.ftWrite = file.ftWrite;
            entry.result = result;
            entry.fp = fp;
            StoreInMemory(key, entry);
            if (!cacheFolder.IsEmpty()) {
                WriteToDisk(DiskPath(cacheFolder, key), key, entry);
            }
        }
        return result;
    }

    // The files of the folder with the same extension, sorted by name; current: the index of the file
    std::vector<FileInfo> ListFolder(const CString& strPath, size_t& current)
    {
        const int iSeparator = std::max(strPath.ReverseFind(_T('\\')), strPath.ReverseFind(_T('/')));
        const CString strFolder = strPath.Left(iSeparator + 1);
        const CString strFileName = strPath.Mid(iSeparator + 1);
        const int iExtension = strFileName.ReverseFind(_T('.'));
        const CString strExtension = (iExtension >= 0) ? strFileName.Mid(iExtension) : CString();

        std::vector<FileInfo> files;
        WIN32_FIND_DATA fd;
        HANDLE hFind = FindFirstFile(strFolder + _T("*") + strExtension, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                const CString strName = fd.cFileName;
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strName.GetLength() > strExtension.GetLength()
                        && !strName.Right(strExtension.GetLength()).CompareNoCase(strExtension)) {
                    FileInfo file;
                    file.path = strFolder + strName;
                    file.size = (ULONGLONG(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                    file.ftWrite = fd.ftLastWriteTime;
                    files.push_back(file);
                }
            } while (FindNextFile(hFind, &fd));
            FindClose(hFind);
        }

        std::sort(files.begin(), files.end(), [](const FileInfo & a, const FileInfo & b) {
            return a.path.CompareNoCase(b.path) < 0;
        });

        current = files.size();
        for (size_t i = 0; i < files.size(); i++) {
            if (!files[i].path.CompareNoCase(strPath)) {
                current = i;
                break;
            }
        }
        return files;
    }

    REFERENCE_TIME ToReferenceTime(double seconds)
    {
        return REFERENCE_TIME(seconds * 10000000.0 + 0.5);
    }
}

CAudioSkipSegmentProvider::CAudioSkipSegmentProvider(std::function<CString()> getCurrentPath)
    : m_getCurrentPath(getCurrentPath)
    , m_bEarlierFinal(false)
    , m_bNeedIntro(false)
    , m_bNeedOutro(false)
    , m_bJobConsumed(false)
{
}

CAudioSkipSegmentProvider::~CAudioSkipSegmentProvider()
{
    Reset();
}

bool CAudioSkipSegmentProvider::IsDirty() const
{
    return m_pJob && m_pJob->bDone && !m_bJobConsumed;
}

bool CAudioSkipSegmentProvider::IsPending() const
{
    return m_pJob && !m_bJobConsumed;
}

void CAudioSkipSegmentProvider::SetEarlierSegments(const std::vector<SkipSegment>& earlier, bool bFinal)
{
    m_bEarlierFinal = bFinal;
    m_bNeedIntro = m_bNeedOutro = true;
    for (const SkipSegment& segment : earlier) {
        if (segment.kind == SKIP_INTRO) {
            m_bNeedIntro = false;
        } else if (segment.kind == SKIP_OUTRO) {
            m_bNeedOutro = false;
        }
    }
}

void CAudioSkipSegmentProvider::Reset()
{
    if (m_pJob) {
        m_pJob->bCancel = true;
        m_pJob.reset();
    }
    m_strJobPath.Empty();
    m_bJobConsumed = false;
}

void CAudioSkipSegmentProvider::GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments)
{
    segments.clear();

    if (!AfxGetAppSettings().bSkipByAudio || rtDuration < MIN_FILE_DURATION) {
        return;
    }

    const CString strPath = m_getCurrentPath();
    if (strPath.IsEmpty() || strPath.Find(_T("://")) >= 0) {
        return;
    }

    if (!m_pJob || strPath != m_strJobPath) {
        // only when the chapters are done with and something is still missing
        if (!m_bEarlierFinal || (!m_bNeedIntro && !m_bNeedOutro)) {
            return;
        }

        Reset();
        m_pJob = std::make_shared<Job>();
        m_pJob->bIntro = m_bNeedIntro;
        m_pJob->bOutro = m_bNeedOutro;
        m_strJobPath = strPath;

        const CString strDecoderFolder = PathUtils::CombinePaths(PathUtils::GetProgramPath(), DECODER_FOLDER);
        try {
            std::thread(&CAudioSkipSegmentProvider::Run, m_pJob, strPath, strDecoderFolder, GetCacheFolder()).detach();
        } catch (...) {
            m_pJob->bDone = true;
        }
        return; // IsDirty() reports when the job is done
    }

    if (!m_pJob->bDone) {
        return;
    }
    m_bJobConsumed = true;

    for (SkipSegment segment : m_pJob->segments) {
        segment.rtStop = std::min(segment.rtStop, rtDuration);
        if (segment.rtStop > segment.rtStart) {
            segments.push_back(segment);
        }
    }
}

CString CAudioSkipSegmentProvider::GetCacheFolder()
{
    CString strFolder;
    if (AfxGetMyApp()->IsIniValid()) {
        strFolder = PathUtils::GetProgramPath(); // portable use: next to the program, like the settings
    } else {
        TCHAR path[MAX_PATH];
        if (FAILED(SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
            return CString();
        }
        strFolder = PathUtils::CombinePaths(path, _T("MPC-HC"));
    }

    strFolder = PathUtils::CombinePaths(strFolder, _T("SkipAudioCache"));
    if (strFolder.Right(1) != _T("\\")) {
        strFolder += _T('\\');
    }
    return strFolder;
}

void CAudioSkipSegmentProvider::Run(std::shared_ptr<Job> pJob, CString strPath, CString strDecoderFolder, CString strCacheFolder)
{
    // Low CPU and disk priority: the playback comes first
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

    try {
        if (!LoadSkipAudioDecoder(std::wstring(CStringW(strDecoderFolder).GetString()))) {
            pJob->bDone = true;
            return;
        }

        if (!strCacheFolder.IsEmpty()) {
            const int result = SHCreateDirectoryEx(nullptr, strCacheFolder.Left(strCacheFolder.GetLength() - 1), nullptr);
            if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS) {
                PruneDiskCache(strCacheFolder);
            } else {
                strCacheFolder.Empty(); // no disk cache, only memory
            }
        }

        size_t current;
        const std::vector<FileInfo> files = ListFolder(strPath, current);
        if (current < files.size()) {
            const SkipAudioRegion regions[2] = { SKIP_AUDIO_INTRO, SKIP_AUDIO_OUTRO };
            const bool bWanted[2] = { pJob->bIntro, pJob->bOutro };
            SkipAudioFingerprint currentFp[2];
            bool bHave[2] = { false, false };
            for (int r = 0; r < 2 && !pJob->bCancel; r++) {
                bHave[r] = bWanted[r] && GetFingerprint(files[current], regions[r], 0.0, 0.0, strCacheFolder, &pJob->bCancel, currentFp[r]) == SKIP_AUDIO_OK
                           && !currentFp[r].hashes.empty();
            }

            const std::vector<size_t> order = GetSkipAudioNeighbourOrder(files.size(), current);
            const SkipAudioSource source = [&](size_t i, SkipAudioRegion region, double minDuration, double maxDuration, SkipAudioFingerprint & fp) {
                return GetFingerprint(files[order[i]], region, minDuration, maxDuration, strCacheFolder, &pJob->bCancel, fp) == SKIP_AUDIO_OK;
            };

            SkipAudioSegment intro, outro;
            if (!pJob->bCancel && (bHave[0] || bHave[1])) {
                FindSkipAudioSegments(bHave[0] ? &currentFp[0] : nullptr, bHave[1] ? &currentFp[1] : nullptr,
                                      order.size(), source, &pJob->bCancel, intro, outro);
            }

            if (!pJob->bCancel) {
                if (intro.bFound) {
                    pJob->segments.push_back({ ToReferenceTime(intro.start), ToReferenceTime(intro.end), CString(_T("Intro")), SKIP_INTRO });
                }
                if (outro.bFound) {
                    pJob->segments.push_back({ ToReferenceTime(outro.start), ToReferenceTime(outro.end), CString(_T("Outro")), SKIP_OUTRO });
                }
            }
        }
    } catch (...) {
        pJob->segments.clear();
    }

    pJob->bDone = true;
}
