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
#include "SkipButton.h"
#include "MainFrm.h"
#include "mplayerc.h"
#include <algorithm>

#define SKIP_BUTTON_WIDTH         88
#define SKIP_BUTTON_HEIGHT        34
#define SKIP_BUTTON_MARGIN        24
#define SKIP_BUTTON_GAP           8
#define SKIP_BUTTON_RADIUS        6.0f
#define SKIP_BUTTON_BORDER        2.0f
#define SKIP_BUTTON_SHADOW        2.5f
#define SKIP_BUTTON_FONT_SIZE     15.0f
#define SKIP_BUTTON_ALPHA         200
#define SKIP_BUTTON_ALPHA_HOVERED 255

namespace
{
    void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, Gdiplus::REAL radius)
    {
        const Gdiplus::REAL d = radius * 2;
        path.AddArc(rect.X, rect.Y, d, d, 180, 90);
        path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270, 90);
        path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0, 90);
        path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90, 90);
        path.CloseFigure();
    }
}

IMPLEMENT_DYNAMIC(CSkipButton, CWnd)

CSkipButton::CSkipButton(CMainFrame* pMainFrame)
    : m_pMainFrame(pMainFrame)
    , m_gdiplusToken(0)
    , m_rcButton(0, 0, 0, 0)
    , m_bHovered(false)
    , m_bPressed(false)
    , m_bRenderPending(true)
{
    GetEventd().Connect(m_eventc, {
        MpcEvent::DPI_CHANGED,
    }, std::bind(&CSkipButton::EventCallback, this, std::placeholders::_1));
}

CSkipButton::~CSkipButton()
{
}

BOOL CSkipButton::Create(CWnd* pOwnerWnd, std::function<void()> onClick)
{
    m_onClick = onClick;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        m_gdiplusToken = 0;
        return FALSE;
    }

    LPCTSTR pszClassName = AfxRegisterWndClass(0, AfxGetApp()->LoadStandardCursor(IDC_HAND));

    if (!CreateEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, pszClassName, _T("Skip"),
                  WS_POPUP, 0, 0, 0, 0, pOwnerWnd->GetSafeHwnd(), nullptr)) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
        return FALSE;
    }

    return TRUE;
}

void CSkipButton::EventCallback(MpcEvent ev)
{
    switch (ev) {
        case MpcEvent::DPI_CHANGED:
            m_bRenderPending = true;
            break;

        default:
            ASSERT(FALSE);
    }
}

CSize CSkipButton::GetButtonSize() const
{
    const DpiHelper& dpi = m_pMainFrame->m_dpi;
    return CSize(dpi.ScaleX(SKIP_BUTTON_WIDTH), dpi.ScaleY(SKIP_BUTTON_HEIGHT));
}

void CSkipButton::Show(const CRect& rcArea, int nReservedBottom)
{
    if (!m_hWnd) {
        return;
    }

    const DpiHelper& dpi = m_pMainFrame->m_dpi;
    const CSize size = GetButtonSize();
    const int marginX = dpi.ScaleX(SKIP_BUTTON_MARGIN);
    const int gap = dpi.ScaleY(SKIP_BUTTON_GAP);

    const int band = rcArea.Height() / 8;
    const int top = std::min(rcArea.bottom - band + (band - size.cy) / 2,
                             rcArea.bottom - nReservedBottom - gap - size.cy);

    if (rcArea.Width() < size.cx + 2 * marginX || top < rcArea.top + gap) {
        Hide();
        return;
    }

    const CRect rcButton(CPoint(rcArea.right - marginX - size.cx, top), size);

    if (m_bRenderPending || rcButton.Size() != m_rcButton.Size()) {
        m_rcButton = rcButton;
        Render();
    } else if (rcButton != m_rcButton) {
        m_rcButton = rcButton;
        SetWindowPos(nullptr, m_rcButton.left, m_rcButton.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!IsWindowVisible()) {
        ShowWindow(SW_SHOWNOACTIVATE);
    }
}

void CSkipButton::Hide()
{
    if (!m_hWnd) {
        return;
    }

    if (GetCapture() == this) {
        ReleaseCapture();
    }
    m_bPressed = false;

    if (IsWindowVisible()) {
        ShowWindow(SW_HIDE);
    }

    SetHovered(false);
}

bool CSkipButton::IsShown() const
{
    return m_hWnd && IsWindowVisible();
}

void CSkipButton::SetHovered(bool bHovered)
{
    if (m_bHovered != bHovered) {
        m_bHovered = bHovered;
        if (IsShown()) {
            Render();
        } else {
            m_bRenderPending = true;
        }
    }
}

void CSkipButton::Render()
{
    const CSize size = m_rcButton.Size();
    if (size.cx <= 0 || size.cy <= 0) {
        return;
    }

    CDC memDC;
    if (!memDC.CreateCompatibleDC(nullptr)) {
        return;
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size.cx;
    bmi.bmiHeader.biHeight = -size.cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap) {
        return;
    }
    HGDIOBJ hOldBitmap = ::SelectObject(memDC, hBitmap);

    {
        Gdiplus::Bitmap bitmap(size.cx, size.cy, size.cx * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pBits));
        Gdiplus::Graphics graphics(&bitmap);
        DrawButton(graphics, size);
    }

    POINT ptDst = m_rcButton.TopLeft();
    SIZE sizeDst = size;
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    VERIFY(::UpdateLayeredWindow(m_hWnd, nullptr, &ptDst, &sizeDst, memDC, &ptSrc, 0, &blend, ULW_ALPHA));

    ::SelectObject(memDC, hOldBitmap);
    DeleteObject(hBitmap);

    m_bRenderPending = false;
}

void CSkipButton::DrawButton(Gdiplus::Graphics& graphics, const CSize& size) const
{
    const Gdiplus::REAL scale = Gdiplus::REAL(m_pMainFrame->m_dpi.ScaleFactorY());
    const BYTE alpha = BYTE(m_bHovered ? SKIP_BUTTON_ALPHA_HOVERED : SKIP_BUTTON_ALPHA);
    const BYTE shadowAlpha = BYTE(alpha * 3 / 5);

    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const Gdiplus::REAL borderWidth = std::max(1.0f, SKIP_BUTTON_BORDER * scale);
    const Gdiplus::REAL shadowWidth = borderWidth + SKIP_BUTTON_SHADOW * scale;

    const Gdiplus::REAL inset = shadowWidth / 2;
    const Gdiplus::RectF rect(inset, inset, Gdiplus::REAL(size.cx) - 2 * inset, Gdiplus::REAL(size.cy) - 2 * inset);
    const Gdiplus::REAL radius = std::min(SKIP_BUTTON_RADIUS * scale, rect.Height / 2);

    Gdiplus::GraphicsPath framePath;
    AddRoundRect(framePath, rect, radius);

    Gdiplus::SolidBrush hitBrush(Gdiplus::Color(1, 0, 0, 0));
    graphics.FillPath(&hitBrush, &framePath);

    Gdiplus::Pen shadowPen(Gdiplus::Color(shadowAlpha, 0, 0, 0), shadowWidth);
    Gdiplus::Pen borderPen(Gdiplus::Color(alpha, 255, 255, 255), borderWidth);
    graphics.DrawPath(&shadowPen, &framePath);
    graphics.DrawPath(&borderPen, &framePath);

    Gdiplus::FontFamily segoeUI(L"Segoe UI");
    const Gdiplus::FontFamily* pFontFamily = segoeUI.IsAvailable() ? &segoeUI : Gdiplus::FontFamily::GenericSansSerif();

    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::GraphicsPath textPath;
    textPath.AddString(L"Skip", -1, pFontFamily, Gdiplus::FontStyleBold, SKIP_BUTTON_FONT_SIZE * scale, rect, &format);

    Gdiplus::Pen textShadowPen(Gdiplus::Color(shadowAlpha, 0, 0, 0), SKIP_BUTTON_SHADOW * scale);
    textShadowPen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(alpha, 255, 255, 255));
    graphics.DrawPath(&textShadowPen, &textPath);
    graphics.FillPath(&textBrush, &textPath);
}

BEGIN_MESSAGE_MAP(CSkipButton, CWnd)
    ON_WM_MOUSEACTIVATE()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_CAPTURECHANGED()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

int CSkipButton::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message)
{
    return MA_NOACTIVATE;
}

void CSkipButton::OnMouseMove(UINT nFlags, CPoint point)
{
    if (!m_bHovered) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&tme);
        SetHovered(true);
    }

    __super::OnMouseMove(nFlags, point);
}

void CSkipButton::OnMouseLeave()
{
    SetHovered(false);

    __super::OnMouseLeave();
}

void CSkipButton::OnLButtonDown(UINT nFlags, CPoint point)
{
    m_bPressed = true;
    SetCapture();
}

void CSkipButton::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (!m_bPressed) {
        return;
    }
    m_bPressed = false;
    if (GetCapture() == this) {
        ReleaseCapture();
    }

    CRect rcClient;
    GetClientRect(rcClient);
    if (rcClient.PtInRect(point) && m_onClick) {
        m_onClick();
    }
}

void CSkipButton::OnCaptureChanged(CWnd* pWnd)
{
    m_bPressed = false;

    __super::OnCaptureChanged(pWnd);
}

void CSkipButton::OnDestroy()
{
    __super::OnDestroy();

    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}
