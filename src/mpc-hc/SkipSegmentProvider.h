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

#include "DSMPropertyBag.h"
#include <vector>

struct SkipSegment {
    REFERENCE_TIME rtStart;
    REFERENCE_TIME rtStop;
    CString name;
};

class ISkipSegmentProvider
{
public:
    virtual ~ISkipSegmentProvider() = default;

    virtual bool IsDirty() const = 0;

    virtual void GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments) = 0;
};

class CChapterSkipSegmentProvider final : public ISkipSegmentProvider
{
public:
    explicit CChapterSkipSegmentProvider(const CComPtr<IDSMChapterBag>& pCB);

    bool IsDirty() const override;
    void GetSkipSegments(REFERENCE_TIME rtDuration, std::vector<SkipSegment>& segments) override;

    static bool IsSkipChapterName(const CString& name);

private:
    const CComPtr<IDSMChapterBag>& m_pCB;
    CComPtr<IDSMChapterBag> m_pLastCB;
};
