#include "stdafx.h"
#include <WeaselUI.h>
#include "WeaselPanel.h"

using namespace weasel;

class weasel::UIImpl {
 public:
  WeaselPanel panel;

  UIImpl(weasel::UI& ui) : panel(ui), shown(false) {}
  ~UIImpl() {}
  void Refresh() {
    if (!panel.IsWindow())
      return;
    if (timer) {
      Hide();
      KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
      timer = 0;
    }
    panel.Refresh();
  }
  void Show();
  void Hide();
  void ShowWithTimeout(size_t millisec);
  void UpdateInputPosition(RECT const& rc);
  void CancelInitialShow();
  bool IsShown() const { return shown; }
  void ReloadUserSettings() {
    if (panel.IsWindow())
      panel.ReloadUserSettings();
  }

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static VOID CALLBACK OnInitialShowTimer(_In_ HWND hwnd,
                                          _In_ UINT uMsg,
                                          _In_ UINT_PTR idEvent,
                                          _In_ DWORD dwTime);
  static const int AUTOHIDE_TIMER = 20121220;
  static const int INITIAL_SHOW_TIMER = 20121221;
  static UINT_PTR timer;
  bool shown;
  bool initialShowPending = false;
  bool hasFreshAnchor = false;
  DWORD initialShowTick = 0;
  DWORD latestAnchorTick = 0;

 private:
  void ShowNow();
  void ScheduleInitialShow();
};

UINT_PTR UIImpl::timer = 0;

void UIImpl::ShowNow() {
  if (!panel.IsWindow())
    return;
  panel.ShowWindow(SW_SHOWNA);
  panel.ShowAcrylicBackdrop();
  shown = true;
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    timer = 0;
  }
}

void UIImpl::ScheduleInitialShow() {
  if (!panel.IsWindow() || !initialShowPending)
    return;
  // The candidate content can be ready before the host's asynchronous text
  // extent. Keep the first frame hidden while subsequent layout notifications
  // replace a provisional caret rectangle, regardless of the host application.
  constexpr DWORD kAnchorQuietMs = 50;
  constexpr DWORD kMaximumWaitMs = 120;
  const DWORD now = GetTickCount();
  const DWORD elapsed = now - initialShowTick;
  const DWORD remaining =
      elapsed < kMaximumWaitMs ? kMaximumWaitMs - elapsed : 0;
  const DWORD anchorElapsed = now - latestAnchorTick;
  const DWORD quietRemaining = hasFreshAnchor && anchorElapsed < kAnchorQuietMs
                                   ? kAnchorQuietMs - anchorElapsed
                                   : 0;
  const DWORD delay =
      hasFreshAnchor ? min(quietRemaining, remaining) : remaining;
  if (delay == 0) {
    CancelInitialShow();
    ShowNow();
    return;
  }
  if (!SetTimer(panel.m_hWnd, INITIAL_SHOW_TIMER, delay,
                &UIImpl::OnInitialShowTimer)) {
    CancelInitialShow();
    ShowNow();
  }
}

void UIImpl::Show() {
  if (!panel.IsWindow() || shown)
    return;
  if (!initialShowPending) {
    initialShowPending = true;
    initialShowTick = GetTickCount();
    if (!SetPropW(panel.m_hWnd, L"WeaselInitialShowOwner", this)) {
      initialShowPending = false;
      ShowNow();
      return;
    }
  }
  ScheduleInitialShow();
}

void UIImpl::CancelInitialShow() {
  if (panel.IsWindow()) {
    KillTimer(panel.m_hWnd, INITIAL_SHOW_TIMER);
    RemovePropW(panel.m_hWnd, L"WeaselInitialShowOwner");
  }
  initialShowPending = false;
}

void UIImpl::UpdateInputPosition(RECT const& rc) {
  if (!panel.IsWindow())
    return;
  panel.MoveTo(rc);
  if (rc.bottom > rc.top) {
    hasFreshAnchor = true;
    latestAnchorTick = GetTickCount();
  } else {
    hasFreshAnchor = false;
  }
  if (initialShowPending)
    ScheduleInitialShow();
}

VOID CALLBACK UIImpl::OnInitialShowTimer(_In_ HWND hwnd,
                                         _In_ UINT uMsg,
                                         _In_ UINT_PTR idEvent,
                                         _In_ DWORD dwTime) {
  auto* self =
      reinterpret_cast<UIImpl*>(GetPropW(hwnd, L"WeaselInitialShowOwner"));
  if (self && self->initialShowPending)
    self->ScheduleInitialShow();
}

void UIImpl::Hide() {
  if (!panel.IsWindow())
    return;
  CancelInitialShow();
  panel.HideAcrylicBackdrop();
  panel.ShowWindow(SW_HIDE);
  shown = false;
  hasFreshAnchor = false;
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    timer = 0;
  }
}

void UIImpl::ShowWithTimeout(size_t millisec) {
  if (!panel.IsWindow())
    return;
  CancelInitialShow();
  DLOG(INFO) << "ShowWithTimeout: " << millisec;
  panel.ShowWindow(SW_SHOWNA);
  panel.ShowAcrylicBackdrop();
  shown = true;
  SetTimer(panel.m_hWnd, AUTOHIDE_TIMER, static_cast<UINT>(millisec),
           &UIImpl::OnTimer);
  timer = UINT_PTR(this);
}
VOID CALLBACK UIImpl::OnTimer(_In_ HWND hwnd,
                              _In_ UINT uMsg,
                              _In_ UINT_PTR idEvent,
                              _In_ DWORD dwTime) {
  DLOG(INFO) << "OnTimer:";
  KillTimer(hwnd, idEvent);
  UIImpl* self = (UIImpl*)timer;
  timer = 0;
  if (self) {
    self->Hide();
    self->shown = false;
  }
}

bool UI::Create(HWND parent) {
  if (pimpl_) {
    pimpl_->CancelInitialShow();
    pimpl_->shown = false;
    pimpl_->hasFreshAnchor = false;
    pimpl_->panel.Create(
        parent, 0, 0, WS_POPUP,
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        0U, 0);
    // The TSF candidate window is destroyed between UI lifecycles while its
    // DirectWrite resources remain alive.  It can therefore miss the settings
    // broadcast sent while no window exists.  Reload whenever the window is
    // recreated so the next composition uses the persisted font choices.
    pimpl_->panel.ReloadUserSettings();
    return true;
  }

  pimpl_ = new UIImpl(*this);
  if (!pimpl_)
    return false;

  pimpl_->panel.Create(
      parent, 0, 0, WS_POPUP,
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
      0U, 0);
  pimpl_->panel.ReloadUserSettings();
  return true;
}

void UI::Destroy(bool full) {
  if (pimpl_) {
    pimpl_->CancelInitialShow();
    pimpl_->shown = false;
    pimpl_->hasFreshAnchor = false;
    // destroy panel
    if (pimpl_->panel.IsWindow()) {
      pimpl_->panel.DestroyWindow();
    }
    if (full) {
      delete pimpl_;
      pimpl_ = 0;
      pDWR.reset();
    }
  }
}

bool UI::GetIsReposition() {
  if (pimpl_)
    return pimpl_->panel.GetIsReposition();
  else
    return false;
}

void UI::Show() {
  if (pimpl_) {
    pimpl_->Show();
  }
}

void UI::Hide() {
  if (pimpl_) {
    pimpl_->Hide();
  }
}

void UI::ShowWithTimeout(size_t millisec) {
  if (pimpl_) {
    pimpl_->ShowWithTimeout(millisec);
  }
}

bool UI::IsCountingDown() const {
  return pimpl_ && pimpl_->timer != 0;
}

bool UI::IsShown() const {
  return pimpl_ && pimpl_->IsShown();
}

void UI::Refresh() {
  if (pimpl_) {
    pimpl_->Refresh();
  }
}

void UI::ReloadUserSettings() {
  if (pimpl_)
    pimpl_->ReloadUserSettings();
}

void UI::UpdateInputPosition(RECT const& rc) {
  if (pimpl_ && pimpl_->panel.IsWindow()) {
    pimpl_->UpdateInputPosition(rc);
  }
}

void UI::Update(const Context& ctx, const Status& status) {
  if (ctx_ == ctx && status_ == status)
    return;
  ctx_ = ctx;
  status_ = status;
  // A lone multi-line candidate (for example a statistics report) needs its
  // full text so the layout can measure and grow to every line.
  const bool single_multiline_candidate =
      ctx_.cinfo.candies.size() == 1 &&
      ctx_.cinfo.candies.front().str.find_first_of(L"\r\n") !=
          std::wstring::npos;
  if (style_.candidate_abbreviate_length > 0 && !single_multiline_candidate) {
    for (auto& c : ctx_.cinfo.candies) {
      if (c.str.length() > (size_t)style_.candidate_abbreviate_length) {
        c.str =
            c.str.substr(0, (size_t)style_.candidate_abbreviate_length - 1) +
            L"..." + c.str.substr(c.str.length() - 1);
      }
    }
  }
  Refresh();
}
