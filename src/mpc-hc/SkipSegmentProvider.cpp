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
    struct SkipKeyword {
        LPCTSTR keyword;
        SkipSegmentKind kind;
    };

    const SkipKeyword s_skipKeywords[] = {
        { _T("intro"), SKIP_INTRO },
        { _T("opening"), SKIP_INTRO },
        { _T("op"), SKIP_INTRO },
        { _T("outro"), SKIP_OUTRO },
        { _T("ending"), SKIP_OUTRO },
        { _T("ed"), SKIP_OUTRO },
        { _T("credits"), SKIP_OUTRO },
    };

    SkipSegmentKind GetSkipKeywordKind(CString word)
    {
        word.TrimRight(_T("0123456789"));
        if (word.IsEmpty()) {
            return SKIP_UNKNOWN;
        }

        word.MakeLower();
        for (const SkipKeyword& skipKeyword : s_skipKeywords) {
            if (word == skipKeyword.keyword) {
                return skipKeyword.kind;
            }
        }
        return SKIP_UNKNOWN;
    }

    bool Overlaps(const SkipSegment& a, const SkipSegment& b)
    {
        return a.rtStart < b.rtStop && b.rtStart < a.rtStop;
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
        const SkipSegmentKind kind = GetSkipChapterKind(chapters[i].name);
        if (kind == SKIP_UNKNOWN) {
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
            segments.push_back({ chapters[i].rt, rtStop, chapters[i].name, kind });
        }
    }
}

bool CChapterSkipSegmentProvider::IsSkipChapterName(const CString& name)
{
    return GetSkipChapterKind(name) != SKIP_UNKNOWN;
}

SkipSegmentKind CChapterSkipSegmentProvider::GetSkipChapterKind(const CString& name)
{
    CString word;
    for (int i = 0, len = name.GetLength(); i <= len; i++) {
        const TCHAR c = (i < len) ? name[i] : _T('\0');
        if (c && IsCharAlphaNumeric(c)) {
            word += c;
        } else if (!word.IsEmpty()) {
            const SkipSegmentKind kind = GetSkipKeywordKind(word);
            if (kind != SKIP_UNKNOWN) {
                return kind;
            }
            word.Empty();
        }
    }
    return SKIP_UNKNOWN;
}

void CCompositeSkipSegmentProvider::AddProvider(std::unique_ptr<ISkipSegmentProvider> pProvider)
{
    m_providers.push_back(std::move(pProvider));
}

bool CCompositeSkipSegmentProvider::IsDirty() const
{
    for (const auto& pProvider : m_providers) {
        if (pProvider->IsDirty()) {
            return true;
        }
    }
    return false;
}

bool CCompositeSkipSegmentProvider::IsPending() const
{
    for (const auto& pProvider : m_providers) {
        if (pProvider->IsPending()) {
            return true;
        }
    }
    return false;
}

void CCompositeSkipSegmentProvider::GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments)
{
    segments.clear();

    // Every provider is asked each time: this is also how it learns that its result was taken
    bool bEarlierPending = false;
    for (const auto& pProvider : m_providers) {
        pProvider->SetEarlierSegments(segments, !bEarlierPending);

        std::vector<SkipSegment> found;
        pProvider->GetSkipSegments(rtDuration, found);
        bEarlierPending = bEarlierPending || pProvider->IsPending();

        // the segments of the earlier providers: only the kinds they found nothing of can be added
        const std::vector<SkipSegment> earlier = segments;
        for (const SkipSegment& segment : found) {
            bool bAdd = true;
            for (const SkipSegment& other : earlier) {
                if (other.kind == segment.kind || Overlaps(other, segment)) {
                    bAdd = false;
                    break;
                }
            }
            if (bAdd) {
                segments.push_back(segment);
            }
        }
    }

    std::stable_sort(segments.begin(), segments.end(), [](const SkipSegment & a, const SkipSegment & b) {
        return a.rtStart < b.rtStart;
    });
}

void CCompositeSkipSegmentProvider::Reset()
{
    for (const auto& pProvider : m_providers) {
        pProvider->Reset();
    }
}
