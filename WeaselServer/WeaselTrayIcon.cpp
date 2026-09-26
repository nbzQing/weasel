#include "stdafx.h"
#include "WeaselTrayIcon.h"
#include <atlstr.h>
#include <WeaselUserSettings.h>
#include <WeaselMenu.h>

#include <algorithm>
#include <gdiplus.h>
#include <string>
#include <set>

#pragma comment(lib, "gdiplus.lib")

// nasty
#include <resource.h>

static UINT mode_icon[] = {IDI_ZH,   IDI_ZH,   IDI_EN,
                           IDI_CAPS, IDI_CAPS, IDI_RELOAD};
static const WCHAR* mode_label[] = {NULL,
                                    /*L"中文"*/ NULL,
                                    /*L"西文"*/ NULL,
                                    NULL,
                                    /*L"中文大写"*/ NULL,
                                    /*L"西文大写"*/ NULL,
                                    L"Under maintenance"};

namespace {
unsigned int LoadPackageUpdateCount() {
  constexpr wchar_t kRegistry[] = L"Software\\Rime\\Weasel\\PackageUpdates";
  DWORD count = 0;
  DWORD size = sizeof(count);
  if (::RegGetValueW(HKEY_CURRENT_USER, kRegistry, L"AvailableCount",
                     RRF_RT_REG_DWORD, nullptr, &count,
                     &size) != ERROR_SUCCESS) {
    return 0;
  }
  return (std::min)(count, 99ul);
}

std::wstring SettingsMenuText(unsigned int count) {
  const LANGID language = GetThreadUILanguage();
  if (PRIMARYLANGID(language) != LANG_CHINESE) {
    return L"Settings (&S)\t" + std::to_wstring(count) + L" updates";
  }
  const WORD sublanguage = SUBLANGID(language);
  const bool simplified = sublanguage == SUBLANG_CHINESE_SIMPLIFIED ||
                          sublanguage == SUBLANG_CHINESE_SINGAPORE;
  return (simplified ? L"输入法设定 (&S)\t" : L"輸入法設定 (&S)\t") +
         std::to_wstring(count) + (simplified ? L" 项更新" : L" 項更新");
}

bool SetMenuCommandText(HMENU menu, UINT command, const std::wstring& text) {
  const int count = ::GetMenuItemCount(menu);
  for (int index = 0; index < count; ++index) {
    if (::GetMenuItemID(menu, index) == command) {
      MENUITEMINFOW item = {};
      item.cbSize = sizeof(item);
      item.fMask = MIIM_STRING;
      item.dwTypeData = const_cast<wchar_t*>(text.c_str());
      return ::SetMenuItemInfoW(menu, index, TRUE, &item) != FALSE;
    }
    if (HMENU child = ::GetSubMenu(menu, index)) {
      if (SetMenuCommandText(child, command, text))
        return true;
    }
  }
  return false;
}

bool CapsLockEnabled() {
  return (::GetKeyState(VK_CAPITAL) & 1) != 0;
}

HICON LoadIconFile(const std::wstring& path) {
  if (path.empty() ||
      ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return nullptr;
  }
  HICON icon = reinterpret_cast<HICON>(
      ::LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0,
                   LR_LOADFROMFILE | LR_DEFAULTSIZE));
  if (icon)
    return icon;
  Gdiplus::Bitmap image(path.c_str());
  if (image.GetLastStatus() != Gdiplus::Ok)
    return nullptr;
  return image.GetHICON(&icon) == Gdiplus::Ok ? icon : nullptr;
}

HICON LoadResourceIcon(UINT resource) {
  return reinterpret_cast<HICON>(
      ::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource),
                   IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
}

HICON LoadResolvedStatusIcon(const std::wstring& schema_override,
                             const std::wstring& global,
                             UINT fallback) {
  if (!schema_override.empty() &&
      !weasel::StatusIconUsesGlobal(schema_override)) {
    if (HICON icon = LoadIconFile(schema_override))
      return icon;
  }
  if (HICON icon = LoadIconFile(global))
    return icon;
  return LoadResourceIcon(fallback);
}
}  // namespace

WeaselTrayIcon::WeaselTrayIcon(weasel::UI& ui)
    : m_style(ui.style()),
      m_status(ui.status()),
      m_mode(INITIAL),
      m_schema_id(),
      m_disabled(false),
      m_caps_lock(CapsLockEnabled()) {
  Gdiplus::GdiplusStartupInput startup;
  if (Gdiplus::GdiplusStartup(&m_graphics_token, &startup, nullptr) !=
      Gdiplus::Ok) {
    m_graphics_token = 0;
  }
}

WeaselTrayIcon::~WeaselTrayIcon() {
  if (m_graphics_token)
    Gdiplus::GdiplusShutdown(m_graphics_token);
}

void WeaselTrayIcon::CustomizeMenu(HMENU hMenu) {
  const auto update_count = LoadPackageUpdateCount();
  if (update_count)
    SetMenuCommandText(hMenu, ID_WEASELTRAY_SETTINGS,
                       SettingsMenuText(update_count));
  m_quick_actions.clear();
  m_quick_snapshot =
      m_switch_snapshot ? m_switch_snapshot() : weasel::QuickSwitchSnapshot{};
  HMENU quick = ::CreatePopupMenu();
  if (!quick)
    return;
  const bool available =
      m_quick_snapshot.session_id && !m_quick_snapshot.groups.empty();
  const bool english = PRIMARYLANGID(GetThreadUILanguage()) != LANG_CHINESE;
  if (!available) {
    ::AppendMenuW(quick, MF_STRING | MF_GRAYED, 0,
                  english ? L"No active input session" : L"没有可用的输入会话");
  } else {
    const auto favorites = LoadQuickFavorites();
    UINT next_id = 41000;
    bool added_favorite = false;
    for (const auto& group : m_quick_snapshot.groups) {
      if (!favorites.count(group.schema_index))
        continue;
      if (group.states.size() == 2) {
        const int next = group.selected ? 0 : 1;
        const auto label = group.label + L" · " + group.states[group.selected] +
                           L"  →  " + group.states[next];
        ::AppendMenuW(quick, MF_STRING, next_id, label.c_str());
        m_quick_actions[next_id++] = {group.schema_index, next};
      } else {
        HMENU states = ::CreatePopupMenu();
        for (size_t i = 0; i < group.states.size(); ++i) {
          ::AppendMenuW(
              states,
              MF_STRING |
                  (static_cast<int>(i) == group.selected ? MF_CHECKED : 0),
              next_id, group.states[i].c_str());
          m_quick_actions[next_id++] = {group.schema_index,
                                        static_cast<int>(i)};
        }
        ::AppendMenuW(quick, MF_POPUP, reinterpret_cast<UINT_PTR>(states),
                      group.label.c_str());
      }
      added_favorite = true;
    }
    if (added_favorite)
      ::AppendMenuW(quick, MF_SEPARATOR, 0, nullptr);
    HMENU all = ::CreatePopupMenu();
    for (const auto& group : m_quick_snapshot.groups) {
      HMENU states = ::CreatePopupMenu();
      for (size_t i = 0; i < group.states.size(); ++i) {
        ::AppendMenuW(
            states,
            MF_STRING |
                (static_cast<int>(i) == group.selected ? MF_CHECKED : 0),
            next_id, group.states[i].c_str());
        m_quick_actions[next_id++] = {group.schema_index, static_cast<int>(i)};
      }
      ::AppendMenuW(states, MF_SEPARATOR, 0, nullptr);
      ::AppendMenuW(states, MF_STRING, next_id,
                    favorites.count(group.schema_index)
                        ? (english ? L"Remove from favorites" : L"取消常用")
                        : (english ? L"Add to favorites" : L"设为常用"));
      m_quick_actions[next_id++] = {group.schema_index, -2};
      const auto label = group.label + L" · " + group.states[group.selected];
      ::AppendMenuW(all, MF_POPUP, reinterpret_cast<UINT_PTR>(states),
                    label.c_str());
    }
    ::AppendMenuW(quick, MF_POPUP, reinterpret_cast<UINT_PTR>(all),
                  english ? L"All switches" : L"全部开关");
  }
  ::InsertMenuW(hMenu, 0,
                MF_BYPOSITION | MF_POPUP | (available ? 0 : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(quick),
                english ? L"Current schema switches" : L"当前方案快捷开关");
  ::InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
}

void WeaselTrayIcon::SetQuickSwitchCallbacks(
    std::function<weasel::QuickSwitchSnapshot()> snapshot,
    std::function<bool(const weasel::QuickSwitchSnapshot&, int, int)> select) {
  m_switch_snapshot = std::move(snapshot);
  m_switch_select = std::move(select);
}

std::set<int> WeaselTrayIcon::LoadQuickFavorites() const {
  std::set<int> favorites;
  const auto value_name = L"Favorites:" + u8tow(m_quick_snapshot.schema_id);
  wchar_t buffer[512] = {};
  DWORD bytes = sizeof(buffer);
  constexpr wchar_t key[] = L"Software\\Rime\\Weasel\\QuickSwitches";
  if (::RegGetValueW(HKEY_CURRENT_USER, key, value_name.c_str(), RRF_RT_REG_SZ,
                     nullptr, buffer, &bytes) != ERROR_SUCCESS) {
    for (size_t i = 0; i < m_quick_snapshot.groups.size() && i < 4; ++i)
      favorites.insert(m_quick_snapshot.groups[i].schema_index);
    return favorites;
  }
  const wchar_t* cursor = buffer;
  while (*cursor) {
    wchar_t* end = nullptr;
    const long value = std::wcstol(cursor, &end, 10);
    if (end == cursor)
      break;
    favorites.insert(static_cast<int>(value));
    cursor = *end == L',' ? end + 1 : end;
  }
  return favorites;
}

void WeaselTrayIcon::SaveQuickFavorites(const std::set<int>& favorites) const {
  std::wstring value;
  for (int index : favorites) {
    if (!value.empty())
      value += L",";
    value += std::to_wstring(index);
  }
  const auto value_name = L"Favorites:" + u8tow(m_quick_snapshot.schema_id);
  constexpr wchar_t key[] = L"Software\\Rime\\Weasel\\QuickSwitches";
  ::RegSetKeyValueW(HKEY_CURRENT_USER, key, value_name.c_str(), REG_SZ,
                    value.c_str(),
                    static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

bool WeaselTrayIcon::HandleQuickSwitchCommand(UINT id) {
  const auto action = m_quick_actions.find(id);
  if (action == m_quick_actions.end())
    return false;
  if (action->second.state == -2) {
    auto favorites = LoadQuickFavorites();
    if (favorites.count(action->second.schema_index))
      favorites.erase(action->second.schema_index);
    else
      favorites.insert(action->second.schema_index);
    SaveQuickFavorites(favorites);
  } else if (m_switch_select) {
    m_switch_select(m_quick_snapshot, action->second.schema_index,
                    action->second.state);
  }
  return true;
}

BOOL WeaselTrayIcon::Create(HWND hTargetWnd) {
  HMODULE hModule = GetModuleHandle(NULL);
  CIcon icon;
  icon.LoadIconW(IDI_ZH);
  BOOL bRet =
      CSystemTray::Create(hModule, NULL, WM_WEASEL_TRAY_NOTIFY,
                          get_weasel_ime_name().c_str(), icon, IDR_MENU_POPUP);
  if (hTargetWnd) {
    SetTargetWnd(hTargetWnd);
  }
  if (!m_style.display_tray_icon) {
    RemoveIcon();
  } else {
    AddIcon();
  }
  return bRet;
}

void WeaselTrayIcon::RequestRefresh() {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_refresh_enabled) {
    return;
  }
  m_pending_state = WeaselTrayIconState::From(m_style, m_status);
  if (m_refresh_pending) {
    return;
  }
  m_refresh_pending = true;
  if (!::PostMessage(GetTargetWnd(), WM_WEASEL_SERVICE_NOTIFY, 0, 0)) {
    m_refresh_pending = false;
  }
}

void WeaselTrayIcon::ApplyRefresh() {
  WeaselTrayIconState state;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_refresh_pending || !m_refresh_enabled) {
      return;
    }
    state = m_pending_state;
    state.caps_lock = m_caps_lock.load();
    m_refresh_pending = false;
    m_refresh_in_progress = true;
  }
  Refresh(state);
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_refresh_in_progress = false;
  }
  m_state_cv.notify_all();
}

void WeaselTrayIcon::ReloadSettings() {
  if (!m_last_state.valid)
    return;
  auto state = m_last_state;
  state.caps_lock = m_caps_lock.load();
  Refresh(state, true);
}

void WeaselTrayIcon::SetCapsLockState(bool enabled) {
  if (m_caps_lock.exchange(enabled) == enabled)
    return;
  RequestRefresh();
}

void WeaselTrayIcon::DisableRefresh() {
  std::unique_lock<std::mutex> lock(m_state_mutex);
  m_refresh_enabled = false;
  m_refresh_pending = false;
  m_state_cv.wait(lock, [this] { return !m_refresh_in_progress; });
}

void WeaselTrayIcon::Refresh(const WeaselTrayIconState& state, bool force) {
  m_last_state = state;
  if (!state.display_tray_icon &&
      !state.disabled)  // display notification when deploying
  {
    if (m_mode != INITIAL) {
      RemoveIcon();
      m_mode = INITIAL;
    }
    m_disabled = false;
    return;
  }
  WeaselTrayMode mode = state.disabled ? DISABLED
                        : state.ascii_mode
                            ? (state.caps_lock ? ASCII_CAPS : ASCII)
                            : (state.caps_lock ? ZHUNG_CAPS : ZHUNG);
  if (force || mode != m_mode || m_schema_id != state.schema_id) {
    m_mode = mode;
    m_schema_id = state.schema_id;
    const auto icons = weasel::StatusIconSettings::Load();
    const auto schema_icons =
        weasel::SchemaStatusIconSettings::Load(state.schema_id);
    HICON icon = nullptr;
    if (mode == ASCII) {
      icon = LoadResolvedStatusIcon(schema_icons.ascii, icons.english, IDI_EN);
    } else if (mode == ZHUNG) {
      icon =
          LoadResolvedStatusIcon(schema_icons.chinese, icons.chinese, IDI_ZH);
    } else if (mode == ASCII_CAPS || mode == ZHUNG_CAPS) {
      icon = LoadResolvedStatusIcon(schema_icons.caps, icons.caps, IDI_CAPS);
    }
    if (icon) {
      SetIcon(icon);
      ::DestroyIcon(icon);
    } else {
      SetIcon(mode_icon[mode]);
    }
    ShowIcon();

    if (mode_label[mode] && m_disabled == false) {
      CString info;
      info.LoadStringW(IDS_STR_UNDER_MAINTENANCE);
      ShowBalloon(info, get_weasel_ime_name().c_str());
      m_disabled = true;
    }
    if (m_mode != DISABLED)
      m_disabled = false;
  } else if (!Visible()) {
    ShowIcon();
  }
}
