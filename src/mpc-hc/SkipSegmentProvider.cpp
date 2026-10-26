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
#include "SkipSegmentProvider.h"
#include <algorithm>

namespace
{
    const LPCTSTR s_skipKeywords[] = {
        _T("intro"),
        _T("opening"),
        _T("op"),
        _T("outro"),
        _T("ending"),
        _T("ed"),
        _T("credits"),
    };

    bool IsSkipKeyword(CString word)
    {
        word.TrimRight(_T("0123456789"));
        if (word.IsEmpty()) {
            return false;
        }

        word.MakeLower();
        for (LPCTSTR keyword : s_skipKeywords) {
            if (word == keyword) {
                return true;
            }
        }
        return false;
    }
}

CChapterSkipSegmentProvider::CChapterSkipSegmentProvider(const CComPtr<IDSMChapterBag>& pCB)
    : m_pCB(pCB)
{
}

bool CChapterSkipSegmentProvider::IsDirty() const
{
    return m_pCB.p != m_pLastCB.p;
}

void CChapterSkipSegmentProvider::GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments)
{
    segments.clear();

    m_pLastCB = m_pCB;
    if (!m_pLastCB) {
        return;
    }

    struct Chapter {
        REFERENCE_TIME rt;
        CString name;
    };
    std::vector<Chapter> chapters;

    for (DWORD i = 0, cnt = m_pLastCB->ChapGetCount(); i < cnt; i++) {
        REFERENCE_TIME rt;
        CComBSTR name;
        if (SUCCEEDED(m_pLastCB->ChapGet(i, &rt, &name))) {
            chapters.push_back({ rt, CString(name) });
        }
    }

    std::stable_sort(chapters.begin(), chapters.end(), [](const Chapter & a, const Chapter & b) {
        return a.rt < b.rt;
    });

    for (size_t i = 0; i < chapters.size(); i++) {
        if (!IsSkipChapterName(chapters[i].name)) {
            continue;
        }

        REFERENCE_TIME rtStop = rtDuration;
        for (size_t j = i + 1; j < chapters.size(); j++) {
            if (chapters[j].rt > chapters[i].rt) {
                rtStop = chapters[j].rt;
                break;
            }
        }
        if (rtDuration > 0) {
            rtStop = std::min(rtStop, rtDuration);
        }

        if (rtStop > chapters[i].rt) {
            segments.push_back({ chapters[i].rt, rtStop, chapters[i].name });
        }
    }
}

bool CChapterSkipSegmentProvider::IsSkipChapterName(const CString& name)
{
    CString word;
    for (int i = 0, len = name.GetLength(); i <= len; i++) {
        const TCHAR c = (i < len) ? name[i] : _T('\0');
        if (c && IsCharAlphaNumeric(c)) {
            word += c;
        } else if (!word.IsEmpty()) {
            if (IsSkipKeyword(word)) {
                return true;
            }
            word.Empty();
        }
    }
    return false;
}
