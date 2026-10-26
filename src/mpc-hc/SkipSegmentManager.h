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

#include "SkipButton.h"
#include "SkipSegmentProvider.h"
#include <memory>
#include <vector>

class CMainFrame;

class CSkipSegmentManager final
{
public:
    explicit CSkipSegmentManager(CMainFrame* pMainFrame);

    bool Create();
    void Destroy();

    void Reset();

    void UpdatePosition(REFERENCE_TIME rtNow, REFERENCE_TIME rtDur);
    void UpdateButton();

    bool PreTranslateMessage(const MSG* pMsg);

    bool Skip();

private:
    CMainFrame* m_pMainFrame;
    std::unique_ptr<ISkipSegmentProvider> m_pProvider;
    CSkipButton m_button;

    std::vector<SkipSegment> m_segments;
    bool m_bSegmentsValid;
    REFERENCE_TIME m_rtDuration;
    int m_iActiveSegment;
    REFERENCE_TIME m_rtSkippedStart;

    bool CanShowButton() const;
    bool IsVideoWindowMessage(HWND hWnd) const;
    static bool IsPlainEnterAssigned();
};
