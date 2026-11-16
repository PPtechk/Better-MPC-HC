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

#pragma once

#include "SkipChapterStructure.h"
#include "SkipSegmentProvider.h"
#include <atomic>
#include <functional>
#include <memory>

// Finds the intro and the outro of a Matroska episode from the chapter layout of the other episodes
// in its folder, for the files whose chapters have no names ("Chapter 2"). See FindStructuralSkipSegments().
// The other files are read on a background thread, the result is reported through IsDirty().
class CStructureSkipSegmentProvider final : public ISkipSegmentProvider
{
public:
    // getCurrentPath returns the path of the file that is playing, an empty string if there is none
    CStructureSkipSegmentProvider(const CComPtr<IDSMChapterBag>& pCB, std::function<CString()> getCurrentPath);
    ~CStructureSkipSegmentProvider();

    bool IsDirty() const override;
    void GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments) override;
    void Reset() override;

private:
    struct SiblingLoad {
        std::atomic<bool> bCancel;
        std::atomic<bool> bDone;
        std::vector<SkipChapterTable> tables; // filled by the worker thread before bDone is set

        SiblingLoad() : bCancel(false), bDone(false) {}
    };

    const CComPtr<IDSMChapterBag>& m_pCB;
    CComPtr<IDSMChapterBag> m_pLastCB;
    std::function<CString()> m_getCurrentPath;

    std::shared_ptr<SiblingLoad> m_pLoad;
    CString m_strLoadPath;  // the file the load belongs to
    bool m_bLoadConsumed;   // GetSkipSegments() already used the finished load

    static void LoadSiblings(std::shared_ptr<SiblingLoad> pLoad, CString strPath);
};
