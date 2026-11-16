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
#include "MatroskaChapterReader.h"
#include <algorithm>
#include <cstring>
#include <tchar.h>

namespace
{
    const UINT32 ID_EBML = 0x1A45DFA3;
    const UINT32 ID_SEGMENT = 0x18538067;
    const UINT32 ID_SEEKHEAD = 0x114D9B74;
    const UINT32 ID_SEEK = 0x4DBB;
    const UINT32 ID_SEEK_ID = 0x53AB;
    const UINT32 ID_SEEK_POSITION = 0x53AC;
    const UINT32 ID_INFO = 0x1549A966;
    const UINT32 ID_TIMECODE_SCALE = 0x2AD7B1;
    const UINT32 ID_DURATION = 0x4489;
    const UINT32 ID_CHAPTERS = 0x1043A770;
    const UINT32 ID_EDITION_ENTRY = 0x45B9;
    const UINT32 ID_CHAPTER_ATOM = 0xB6;
    const UINT32 ID_CHAPTER_TIME_START = 0x91;
    const UINT32 ID_CLUSTER = 0x1F43B675;

    const UINT64 UNKNOWN_SIZE = ~UINT64(0);
    const UINT64 MAX_ELEMENT_SIZE = 4 * 1024 * 1024; // the elements read are small, anything bigger is not a header
    const int MAX_TOP_LEVEL_ELEMENTS = 128;

    class CFileHandle
    {
        HANDLE m_h;
    public:
        explicit CFileHandle(HANDLE h) : m_h(h) {}
        ~CFileHandle() {
            if (m_h != INVALID_HANDLE_VALUE) {
                CloseHandle(m_h);
            }
        }
        operator HANDLE() const { return m_h; }
        CFileHandle(const CFileHandle&) = delete;
        CFileHandle& operator=(const CFileHandle&) = delete;
    };

    // Element ID, with the length marker bits kept as it is written in the specification
    bool ReadId(const BYTE*& p, const BYTE* end, UINT32& id)
    {
        if (p >= end) {
            return false;
        }

        int length = 1;
        for (BYTE mask = 0x80; length <= 4 && !(*p & mask); mask >>= 1) {
            length++;
        }
        if (length > 4 || end - p < length) {
            return false;
        }

        id = 0;
        for (int i = 0; i < length; i++) {
            id = (id << 8) | p[i];
        }
        p += length;
        return true;
    }

    bool ReadSize(const BYTE*& p, const BYTE* end, UINT64& size)
    {
        if (p >= end) {
            return false;
        }

        int length = 1;
        BYTE mask = 0x80;
        while (length <= 8 && !(*p & mask)) {
            mask >>= 1;
            length++;
        }
        if (length > 8 || end - p < length) {
            return false;
        }

        UINT64 value = *p & (mask - 1);
        bool bAllOnes = (value == UINT64(mask - 1));
        for (int i = 1; i < length; i++) {
            value = (value << 8) | p[i];
            bAllOnes = bAllOnes && (p[i] == 0xFF);
        }
        p += length;

        size = bAllOnes ? UNKNOWN_SIZE : value;
        return true;
    }

    UINT64 ReadUInt(const BYTE* p, size_t length)
    {
        UINT64 value = 0;
        for (size_t i = 0; i < length && i < 8; i++) {
            value = (value << 8) | p[i];
        }
        return value;
    }

    template<class Func>
    void ForEachChild(const BYTE* p, const BYTE* end, Func func)
    {
        while (p < end) {
            UINT32 id;
            UINT64 size;
            if (!ReadId(p, end, id) || !ReadSize(p, end, size)) {
                return;
            }
            if (size == UNKNOWN_SIZE || size > UINT64(end - p)) {
                size = UINT64(end - p);
            }
            func(id, p, size_t(size));
            p += size;
        }
    }

    struct Header {
        UINT32 id;
        UINT64 size;
        UINT64 dataPos;
    };

    bool ReadAt(HANDLE hFile, UINT64 pos, BYTE* buffer, DWORD length, DWORD& read)
    {
        LARGE_INTEGER offset;
        offset.QuadPart = LONGLONG(pos);
        return SetFilePointerEx(hFile, offset, nullptr, FILE_BEGIN) && ReadFile(hFile, buffer, length, &read, nullptr);
    }

    bool ReadHeader(HANDLE hFile, UINT64 pos, Header& header)
    {
        BYTE buffer[16];
        DWORD read = 0;
        if (!ReadAt(hFile, pos, buffer, sizeof(buffer), read) || read == 0) {
            return false;
        }

        const BYTE* p = buffer;
        const BYTE* end = buffer + read;
        if (!ReadId(p, end, header.id) || !ReadSize(p, end, header.size)) {
            return false;
        }
        header.dataPos = pos + UINT64(p - buffer);
        return true;
    }

    bool ReadElementData(HANDLE hFile, const Header& header, std::vector<BYTE>& data)
    {
        if (header.size == UNKNOWN_SIZE || header.size > MAX_ELEMENT_SIZE) {
            return false;
        }

        data.resize(size_t(header.size));
        DWORD read = 0;
        return header.size == 0 || (ReadAt(hFile, header.dataPos, data.data(), DWORD(header.size), read) && read == header.size);
    }

    void ParseInfo(const std::vector<BYTE>& data, UINT64& timecodeScale, double& duration)
    {
        ForEachChild(data.data(), data.data() + data.size(), [&](UINT32 id, const BYTE* p, size_t size) {
            if (id == ID_TIMECODE_SCALE && size > 0) {
                timecodeScale = ReadUInt(p, size);
            } else if (id == ID_DURATION && (size == 4 || size == 8)) {
                if (size == 4) {
                    const UINT32 bits = UINT32(ReadUInt(p, 4));
                    float value;
                    memcpy(&value, &bits, sizeof(value));
                    duration = value;
                } else {
                    const UINT64 bits = ReadUInt(p, 8);
                    memcpy(&duration, &bits, sizeof(duration));
                }
            }
        });
    }

    void ParseChapters(const std::vector<BYTE>& data, std::vector<REFERENCE_TIME>& starts)
    {
        ForEachChild(data.data(), data.data() + data.size(), [&](UINT32 id, const BYTE* p, size_t size) {
            if (id != ID_EDITION_ENTRY || !starts.empty()) {
                return; // only the first edition that has chapters
            }

            ForEachChild(p, p + size, [&](UINT32 atomId, const BYTE* pAtom, size_t atomSize) {
                if (atomId != ID_CHAPTER_ATOM) {
                    return;
                }

                ForEachChild(pAtom, pAtom + atomSize, [&](UINT32 fieldId, const BYTE* pField, size_t fieldSize) {
                    if (fieldId == ID_CHAPTER_TIME_START && fieldSize > 0) {
                        starts.push_back(REFERENCE_TIME(ReadUInt(pField, fieldSize) / 100)); // ns -> 100 ns
                    }
                });
            });
        });
    }

    // Where the SeekHead says the Info and the Chapters elements are (relative to the segment data)
    struct SeekPositions {
        bool bInfo, bChapters;
        UINT64 info, chapters;

        SeekPositions() : bInfo(false), bChapters(false), info(0), chapters(0) {}
    };

    void ParseSeekHead(const std::vector<BYTE>& data, SeekPositions& positions)
    {
        ForEachChild(data.data(), data.data() + data.size(), [&](UINT32 id, const BYTE* p, size_t size) {
            if (id != ID_SEEK) {
                return;
            }

            UINT64 targetId = 0, position = 0;
            ForEachChild(p, p + size, [&](UINT32 fieldId, const BYTE* pField, size_t fieldSize) {
                if (fieldId == ID_SEEK_ID) {
                    targetId = ReadUInt(pField, fieldSize);
                } else if (fieldId == ID_SEEK_POSITION) {
                    position = ReadUInt(pField, fieldSize);
                }
            });

            if (targetId == ID_INFO) {
                positions.bInfo = true;
                positions.info = position;
            } else if (targetId == ID_CHAPTERS) {
                positions.bChapters = true;
                positions.chapters = position;
            }
        });
    }
}

bool IsMatroskaFileName(LPCTSTR pszPath)
{
    if (!pszPath) {
        return false;
    }

    const TCHAR* pExt = _tcsrchr(pszPath, _T('.'));
    if (!pExt || _tcsrchr(pszPath, _T('\\')) > pExt || _tcsrchr(pszPath, _T('/')) > pExt) {
        return false;
    }

    return !_tcsicmp(pExt, _T(".mkv")) || !_tcsicmp(pExt, _T(".mk3d")) || !_tcsicmp(pExt, _T(".mka")) || !_tcsicmp(pExt, _T(".webm"));
}

bool ReadMatroskaChapterTable(LPCTSTR pszPath, SkipChapterTable& table)
{
    table = SkipChapterTable();

    CFileHandle hFile(CreateFile(pszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
        return false;
    }

    Header header;
    if (!ReadHeader(hFile, 0, header) || header.id != ID_EBML || header.size == UNKNOWN_SIZE
            || !ReadHeader(hFile, header.dataPos + header.size, header) || header.id != ID_SEGMENT) {
        return false;
    }

    const UINT64 segmentStart = header.dataPos;
    const UINT64 segmentEnd = (header.size == UNKNOWN_SIZE || segmentStart + header.size > UINT64(fileSize.QuadPart))
                              ? UINT64(fileSize.QuadPart) : segmentStart + header.size;

    UINT64 timecodeScale = 1000000;
    double duration = 0.0;
    bool bHaveInfo = false, bHaveChapters = false;
    SeekPositions seek;
    std::vector<BYTE> data;

    auto loadInfo = [&](const Header& h) {
        if (ReadElementData(hFile, h, data)) {
            ParseInfo(data, timecodeScale, duration);
            bHaveInfo = true;
        }
    };
    auto loadChapters = [&](const Header& h) {
        if (ReadElementData(hFile, h, data)) {
            ParseChapters(data, table.starts);
            bHaveChapters = true;
        }
    };

    UINT64 pos = segmentStart;
    for (int i = 0; i < MAX_TOP_LEVEL_ELEMENTS && pos < segmentEnd && !(bHaveInfo && bHaveChapters); i++) {
        if (!ReadHeader(hFile, pos, header) || header.size == UNKNOWN_SIZE) {
            break;
        }

        if (header.id == ID_CLUSTER) {
            break; // the media data starts, the SeekHead tells where the rest is
        } else if (header.id == ID_SEEKHEAD) {
            if (ReadElementData(hFile, header, data)) {
                ParseSeekHead(data, seek);
            }
        } else if (header.id == ID_INFO && !bHaveInfo) {
            loadInfo(header);
        } else if (header.id == ID_CHAPTERS && !bHaveChapters) {
            loadChapters(header);
        }

        pos = header.dataPos + header.size;
    }

    if (!bHaveInfo && seek.bInfo && segmentStart + seek.info < segmentEnd
            && ReadHeader(hFile, segmentStart + seek.info, header) && header.id == ID_INFO) {
        loadInfo(header);
    }
    if (!bHaveChapters && seek.bChapters && segmentStart + seek.chapters < segmentEnd
            && ReadHeader(hFile, segmentStart + seek.chapters, header) && header.id == ID_CHAPTERS) {
        loadChapters(header);
    }

    if (!bHaveInfo || duration <= 0.0 || table.starts.empty()) {
        table = SkipChapterTable();
        return false;
    }

    std::sort(table.starts.begin(), table.starts.end());
    table.starts.erase(std::unique(table.starts.begin(), table.starts.end()), table.starts.end());
    table.rtDuration = REFERENCE_TIME(duration * double(timecodeScale) / 100.0);
    return true;
}
