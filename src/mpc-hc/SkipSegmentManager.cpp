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
#include "SkipSegmentManager.h"
#include "MainFrm.h"
#include "mplayerc.h"
#include <mvrInterfaces.h>

CSkipSegmentManager::CSkipSegmentManager(CMainFrame* pMainFrame)
    : m_pMainFrame(pMainFrame)
    , m_pProvider(std::make_unique<CChapterSkipSegmentProvider>(pMainFrame->m_pCB))
    , m_button(pMainFrame)
    , m_bSegmentsValid(false)
    , m_rtDuration(0)
    , m_iActiveSegment(-1)
    , m_rtSkippedStart(-1)
{
}

bool CSkipSegmentManager::Create()
{
    return !!m_button.Create(m_pMainFrame, [this] {
        Skip();
    });
}

void CSkipSegmentManager::Destroy()
{
    if (m_button.m_hWnd) {
        m_button.DestroyWindow();
    }
}

void CSkipSegmentManager::Reset()
{
    m_segments.clear();
    m_bSegmentsValid = false;
    m_rtDuration = 0;
    m_iActiveSegment = -1;
    m_rtSkippedStart = -1;

    m_button.Hide();
}

void CSkipSegmentManager::UpdatePosition(REFERENCE_TIME rtNow, REFERENCE_TIME rtDur)
{
    if (!m_bSegmentsValid || rtDur != m_rtDuration || m_pProvider->IsDirty()) {
        m_pProvider->GetSkipSegments(rtDur, m_segments);
        m_rtDuration = rtDur;
        m_bSegmentsValid = true;
    }

    m_iActiveSegment = -1;
    for (size_t i = 0; i < m_segments.size(); i++) {
        if (rtNow >= m_segments[i].rtStart && rtNow < m_segments[i].rtStop) {
            m_iActiveSegment = int(i);
            break;
        }
    }

    if (m_rtSkippedStart >= 0 &&
            (m_iActiveSegment < 0 || m_segments[m_iActiveSegment].rtStart != m_rtSkippedStart)) {
        m_rtSkippedStart = -1;
    }

    UpdateButton();
}

void CSkipSegmentManager::UpdateButton()
{
    if (!m_button.m_hWnd) {
        return;
    }

    const bool bShow = m_iActiveSegment >= 0
                       && m_segments[m_iActiveSegment].rtStart != m_rtSkippedStart
                       && CanShowButton();

    if (bShow) {
        const CAppSettings& s = AfxGetAppSettings();
        const bool bAutoHideControls = s.bHideFullscreenControls && (m_pMainFrame->m_fFullScreen || s.bHideWindowedControls);

        CRect rcArea;
        int nReservedBottom = 0;
        if (bAutoHideControls) {
            m_pMainFrame->GetClientRect(rcArea);
            m_pMainFrame->ClientToScreen(rcArea);
            if (s.eHideFullscreenControlsPolicy != CAppSettings::HideFullscreenControlsPolicy::SHOW_NEVER) {
                unsigned uTop, uLeft, uRight, uBottom;
                m_pMainFrame->m_controls.GetDockZones(uTop, uLeft, uRight, uBottom);
                nReservedBottom = int(uBottom);
            }
        } else {
            m_pMainFrame->m_wndView.GetClientRect(rcArea);
            m_pMainFrame->m_wndView.ClientToScreen(rcArea);
        }
        m_button.Show(rcArea, nReservedBottom);
    } else {
        m_button.Hide();
    }
}

bool CSkipSegmentManager::CanShowButton() const
{
    if (!AfxGetAppSettings().bShowSkipButton) {
        return false;
    }

    if (m_pMainFrame->GetLoadState() != MLS::LOADED || m_pMainFrame->GetPlaybackMode() != PM_FILE
            || m_pMainFrame->GetMediaState() == State_Stopped) {
        return false;
    }

    if (!m_pMainFrame->IsWindowVisible() || m_pMainFrame->IsIconic() || !m_pMainFrame->IsWindowEnabled()) {
        return false;
    }

    if (m_pMainFrame->IsD3DFullScreenMode()) {
        return false;
    }
    if (m_pMainFrame->m_pMVRI) {
        bool bExclusive = false;
        if (SUCCEEDED(m_pMainFrame->m_pMVRI->GetBool("exclusiveModeActive", &bExclusive)) && bExclusive) {
            return false;
        }
    }

    return true;
}

bool CSkipSegmentManager::PreTranslateMessage(const MSG* pMsg)
{
    if (pMsg->message != WM_KEYDOWN || pMsg->wParam != VK_RETURN || !m_button.IsShown()) {
        return false;
    }

    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0 || GetKeyState(VK_SHIFT) < 0) {
        return false;
    }

    if (!IsVideoWindowMessage(pMsg->hwnd) || IsPlainEnterAssigned()) {
        return false;
    }

    return Skip();
}

bool CSkipSegmentManager::IsVideoWindowMessage(HWND hWnd) const
{
    const HWND hViewWnd = m_pMainFrame->m_wndView.m_hWnd;
    return hWnd == m_pMainFrame->m_hWnd || hWnd == hViewWnd || ::IsChild(hViewWnd, hWnd);
}

bool CSkipSegmentManager::IsPlainEnterAssigned()
{
    const CAppSettings& s = AfxGetAppSettings();

    POSITION pos = s.wmcmds.GetHeadPosition();
    while (pos) {
        const wmcmd& wc = s.wmcmds.GetNext(pos);
        if (wc.key == VK_RETURN && !(wc.fVirt & (FCONTROL | FALT | FSHIFT))) {
            return true;
        }
    }

    return false;
}

bool CSkipSegmentManager::Skip()
{
    if (!m_button.IsShown() || m_iActiveSegment < 0) {
        return false;
    }

    const SkipSegment& segment = m_segments[m_iActiveSegment];
    const REFERENCE_TIME rtTarget = segment.rtStop;
    m_rtSkippedStart = segment.rtStart;

    m_button.Hide();

    m_pMainFrame->SeekTo(rtTarget);

    return true;
}
