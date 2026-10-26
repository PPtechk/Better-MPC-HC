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

#include "EventDispatcher.h"
#include <functional>

class CMainFrame;

namespace Gdiplus
{
    class Graphics;
}

class CSkipButton final : public CWnd
{
    DECLARE_DYNAMIC(CSkipButton)

public:
    explicit CSkipButton(CMainFrame* pMainFrame);
    virtual ~CSkipButton();

    BOOL Create(CWnd* pOwnerWnd, std::function<void()> onClick);

    void Show(const CRect& rcArea, int nReservedBottom);
    void Hide();
    bool IsShown() const;

private:
    CMainFrame* m_pMainFrame;
    std::function<void()> m_onClick;

    EventClient m_eventc;
    void EventCallback(MpcEvent ev);

    ULONG_PTR m_gdiplusToken;
    CRect     m_rcButton;
    bool      m_bHovered;
    bool      m_bPressed;
    bool      m_bRenderPending;

    CSize GetButtonSize() const;
    void SetHovered(bool bHovered);
    void Render();
    void DrawButton(Gdiplus::Graphics& graphics, const CSize& size) const;

protected:
    DECLARE_MESSAGE_MAP()

    afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnCaptureChanged(CWnd* pWnd);
    afx_msg void OnDestroy();
};
