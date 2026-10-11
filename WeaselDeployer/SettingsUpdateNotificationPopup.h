#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <windows.h>

#include "SettingsNavigation.h"

namespace settings_updates {
class UpdateNotificationPopup {
 public:
  HWND window() const { return window_; }

  void Open(HWND owner, HWND anchor) {
    if (::IsWindow(window_)) {
      ::SetForegroundWindow(window_);
      return;
    }
    using GetWindowDpi = UINT(WINAPI*)(HWND);
    const auto get_dpi = reinterpret_cast<GetWindowDpi>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    dpi_ = get_dpi ? get_dpi(owner) : 96;
    font_ = reinterpret_cast<HFONT>(::SendMessageW(anchor, WM_GETFONT, 0, 0));

    WNDCLASSW type{};
    type.lpfnWndProc = Proc;
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    type.lpszClassName = L"Weasel.SettingsUpdateNotification";
    ::RegisterClassW(&type);

    RECT owner_bounds{};
    ::GetWindowRect(owner, &owner_bounds);
    MONITORINFO monitor{sizeof(monitor)};
    ::GetMonitorInfoW(::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST),
                      &monitor);
    constexpr int kWidth = 460, kHeight = 190;
    const int width = Scale(kWidth), height = Scale(kHeight);
    const int x =
        std::clamp(int(owner_bounds.left +
                       (owner_bounds.right - owner_bounds.left - width) / 2),
                   int(monitor.rcWork.left),
                   (std::max)(int(monitor.rcWork.left),
                              int(monitor.rcWork.right) - width));
    const int y =
        std::clamp(int(owner_bounds.top +
                       (owner_bounds.bottom - owner_bounds.top - height) / 2),
                   int(monitor.rcWork.top),
                   (std::max)(int(monitor.rcWork.top),
                              int(monitor.rcWork.bottom) - height));
    window_ = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, type.lpszClassName,
        L"升级提示弹窗", WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, x, y, width,
        height, owner, nullptr, type.hInstance, this);
    if (window_) {
      constexpr DWORD kWindowCornerPreference = 33;
      constexpr int kRoundCorners = 2;
      ::DwmSetWindowAttribute(window_, kWindowCornerPreference, &kRoundCorners,
                              sizeof(kRoundCorners));
      HRGN corners = ::CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                          Scale(16), Scale(16));
      if (corners && !::SetWindowRgn(window_, corners, FALSE))
        ::DeleteObject(corners);
      Sync();
      ::ShowWindow(window_, SW_SHOWNORMAL);
      ::SetFocus(::GetDlgItem(window_, kToggle));
    }
  }

  void SystemChanged() {
    if (!::IsWindow(window_))
      return;
    ::EnumChildWindows(
        window_,
        [](HWND child, LPARAM) {
          settings_theme::ApplyNative(child);
          return TRUE;
        },
        0);
    ::RedrawWindow(window_, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }

  bool Translate(MSG& message) {
    if (!::IsWindow(window_) ||
        (message.hwnd != window_ && !::IsChild(window_, message.hwnd)))
      return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
      ::SendMessageW(window_, WM_CLOSE, 0, 0);
      return true;
    }
    return ::IsDialogMessageW(window_, &message) != FALSE;
  }

 private:
  static constexpr int kToggle = 40330, kClose = 40331;
  static constexpr wchar_t kRegistryPath[] = L"Software\\Rime\\Weasel\\Updates";
  HWND window_ = nullptr;
  HFONT font_ = nullptr;
  UINT dpi_ = 96;

  int Scale(int value) const { return ::MulDiv(value, dpi_, 96); }

  HWND Add(const wchar_t* type,
           const wchar_t* text,
           DWORD style,
           int id,
           int x,
           int y,
           int width,
           int height) {
    return settings_navigation::Create(window_, type, text, style, WORD(id),
                                       Scale(x), Scale(y), Scale(width),
                                       Scale(height));
  }

  void Init() {
    Add(L"STATIC", L"升级提示弹窗", SS_CENTERIMAGE, 0, 20, 12, 320, 30);
    Add(L"BUTTON", L"×", BS_PUSHBUTTON | WS_TABSTOP, kClose, 408, 12, 30, 28);
    settings_navigation::StyleActionButton(window_, kClose);
    Add(L"STATIC", L"发现新版时自动弹出提示", SS_CENTERIMAGE, 0, 20, 66, 352,
        30);
    Add(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, kToggle, 386, 72, 48, 20);
    settings_navigation::StyleSwitch(window_, kToggle);
    Add(L"STATIC", L"关闭后不会自动弹出升级窗口。", SS_LEFT, 0, 20, 110, 418,
        28);
    Add(L"BUTTON", L"关闭", BS_PUSHBUTTON | WS_TABSTOP, kClose + 1, 338, 142,
        100, 32);
    settings_navigation::StyleActionButton(window_, kClose + 1);
  }

  static bool ReadPreference() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE,
                        &key) != ERROR_SUCCESS)
      return true;
    wchar_t value[16]{};
    DWORD size = sizeof(value), type = 0;
    LSTATUS result =
        ::RegQueryValueExW(key, L"ShowUpdateNotification", nullptr, &type,
                           reinterpret_cast<BYTE*>(value), &size);
    if (result != ERROR_SUCCESS) {
      size = sizeof(value);
      result = ::RegQueryValueExW(key, L"CheckForUpdates", nullptr, &type,
                                  reinterpret_cast<BYTE*>(value), &size);
    }
    ::RegCloseKey(key);
    if (result != ERROR_SUCCESS)
      return true;
    if (type == REG_SZ && size >= 2 * sizeof(wchar_t))
      return value[0] != L'0';
    if (type == REG_DWORD && size == sizeof(DWORD)) {
      DWORD numeric = 0;
      std::memcpy(&numeric, value, sizeof(numeric));
      return numeric != 0;
    }
    return true;
  }

  static LSTATUS WritePreference(bool enabled) {
    HKEY key = nullptr;
    const LSTATUS open =
        ::RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0,
                          KEY_SET_VALUE, nullptr, &key, nullptr);
    if (open != ERROR_SUCCESS)
      return open;
    const wchar_t* value = enabled ? L"1" : L"0";
    LSTATUS result = ERROR_SUCCESS;
    if (enabled)
      result = ::RegSetValueExW(key, L"CheckForUpdates", 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value),
                                2 * sizeof(wchar_t));
    if (result == ERROR_SUCCESS)
      result = ::RegSetValueExW(key, L"ShowUpdateNotification", 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value),
                                2 * sizeof(wchar_t));
    ::RegCloseKey(key);
    return result;
  }

  void Sync() {
    ::SendDlgItemMessageW(window_, kToggle, BM_SETCHECK,
                          ReadPreference() ? BST_CHECKED : BST_UNCHECKED, 0);
    SystemChanged();
  }

  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<UpdateNotificationPopup*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<UpdateNotificationPopup*>(
          reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      self->window_ = window;
      ::SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return ::DefWindowProcW(window, message, w, l);
    switch (message) {
      case WM_CREATE:
        self->Init();
        return 0;
      case WM_GETFONT:
        return reinterpret_cast<LRESULT>(self->font_);
      case WM_COMMAND:
        if (LOWORD(w) == kClose || LOWORD(w) == kClose + 1) {
          ::SendMessageW(window, WM_CLOSE, 0, 0);
          return 0;
        }
        if (LOWORD(w) == kToggle && HIWORD(w) == BN_CLICKED) {
          const bool enabled =
              ::SendDlgItemMessageW(window, kToggle, BM_GETCHECK, 0, 0) ==
              BST_CHECKED;
          const LSTATUS result = WritePreference(enabled);
          if (result != ERROR_SUCCESS) {
            ::SendDlgItemMessageW(window, kToggle, BM_SETCHECK,
                                  enabled ? BST_UNCHECKED : BST_CHECKED, 0);
            const std::wstring error = L"无法保存升级提示设置（错误 " +
                                       std::to_wstring(result) + L"）。";
            ::MessageBoxW(window, error.c_str(), L"小狼毫设置",
                          MB_OK | MB_ICONERROR);
          }
          return 0;
        }
        break;
      case WM_CLOSE:
        ::DestroyWindow(window);
        return 0;
      case WM_NCDESTROY:
        self->window_ = nullptr;
        break;
      case WM_ERASEBKGND: {
        RECT bounds{};
        ::GetClientRect(window, &bounds);
        ::FillRect(reinterpret_cast<HDC>(w), &bounds,
                   settings_theme::GetBrush(COLOR_WINDOW));
        return 1;
      }
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
        ::SetTextColor(reinterpret_cast<HDC>(w),
                       settings_theme::GetColor(COLOR_WINDOWTEXT));
        ::SetBkColor(reinterpret_cast<HDC>(w),
                     settings_theme::GetColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(
            settings_theme::GetBrush(COLOR_WINDOW));
    }
    return ::DefWindowProcW(window, message, w, l);
  }
};

inline UpdateNotificationPopup& Popup() {
  static UpdateNotificationPopup popup;
  return popup;
}
}  // namespace settings_updates
