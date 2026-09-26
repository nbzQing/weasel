#include "stdafx.h"
#include "KeySettingsDialog.h"

#include "Configurator.h"

#include <WeaselUserSettings.h>
#include <WeaselUtility.h>
#include <rime_api.h>
#include <rime_levers_api.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::array<const char*, 5> kSwitchKeys = {
    "Shift_L", "Shift_R", "Control_L", "Control_R", "Caps_Lock"};
constexpr std::array<const wchar_t*, 5> kSwitchNames = {
    L"左 Shift", L"右 Shift", L"左 Ctrl", L"右 Ctrl", L"Caps Lock"};
constexpr std::array<const char*, 5> kActions = {
    "noop", "commit_code", "commit_text", "clear", "inline_ascii"};
constexpr std::array<const wchar_t*, 5> kActionZh = {
    L"不使用", L"上屏原始编码后切换", L"上屏当前候选后切换", L"清除输入后切换",
    L"当前输入转英文编辑"};
constexpr std::array<const wchar_t*, 5> kActionTw = {
    L"不使用", L"送出原始編碼後切換", L"送出目前候選後切換", L"清除輸入後切換",
    L"目前輸入轉英文編輯"};
constexpr std::array<const wchar_t*, 5> kActionEn = {
    L"Do not use", L"Commit code, then switch",
    L"Commit candidate, then switch", L"Clear input, then switch",
    L"Edit current input in English"};
constexpr std::array<const char*, 2> kPageCommands = {"Page_Up", "Page_Down"};
constexpr WORD kSwitchCardTitle = 1740;
constexpr WORD kPagingCardTitle = 1741;
constexpr WORD kPagingLabelBase = 1742;
constexpr WORD kScopeNote = 1744;

RimeLeversApi* Levers() {
  auto* module = rime_get_api()->find_module("levers");
  return module ? reinterpret_cast<RimeLeversApi*>(module->get_api()) : nullptr;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::vector<std::string> SelectedSchemas() {
  auto* levers = Levers();
  if (!levers)
    return {};
  auto* switcher = levers->switcher_settings_init();
  if (!switcher)
    return {};
  std::vector<std::string> result;
  if (levers->load_settings(reinterpret_cast<RimeCustomSettings*>(switcher))) {
    RimeSchemaList list{};
    if (levers->get_selected_schema_list(switcher, &list)) {
      std::set<std::string> seen;
      for (size_t index = 0; index < list.size; ++index) {
        const char* id = list.list[index].schema_id;
        if (id && *id && seen.emplace(id).second)
          result.emplace_back(id);
      }
      levers->schema_list_destroy(&list);
    }
  }
  levers->custom_settings_destroy(
      reinterpret_cast<RimeCustomSettings*>(switcher));
  return result;
}

struct PagingBindings {
  std::array<int, 2> index{-1, -1};
  std::array<std::string, 2> accept{};
};

bool ReadConfigString(RimeConfig* config,
                      const std::string& path,
                      std::string* value) {
  std::array<char, 256> buffer{};
  if (!rime_get_api()->config_get_string(config, path.c_str(), buffer.data(),
                                         buffer.size()))
    return false;
  *value = buffer.data();
  return true;
}

bool ReadPagingBindings(const std::string& config_id,
                        PagingBindings* bindings) {
  auto* rime = rime_get_api();
  RimeConfig config{};
  if (!rime->config_open(config_id.c_str(), &config))
    return false;
  const size_t count = rime->config_list_size(&config, "key_binder/bindings");
  for (size_t index = 0; index < count; ++index) {
    const std::string path =
        "key_binder/bindings/@" + std::to_string(index) + "/";
    std::string send;
    std::string when;
    std::string accept;
    if (!ReadConfigString(&config, path + "send", &send) ||
        !ReadConfigString(&config, path + "accept", &accept) ||
        !ReadConfigString(&config, path + "when", &when) ||
        (when != "has_menu" && when != "paging"))
      continue;
    for (size_t direction = 0; direction < kPageCommands.size(); ++direction)
      if (send == kPageCommands[direction] && bindings->index[direction] < 0) {
        bindings->index[direction] = static_cast<int>(index);
        bindings->accept[direction] = accept;
      }
  }
  rime->config_close(&config);
  return true;
}

std::wstring DisplayKeyPart(const std::string& value) {
  if (value == "minus")
    return L"−";
  if (value == "equal")
    return L"=";
  if (value == "comma")
    return L",";
  if (value == "period")
    return L".";
  if (value == "space")
    return L"Space";
  if (value == "grave")
    return L"`";
  if (value == "bracketleft")
    return L"[";
  if (value == "bracketright")
    return L"]";
  if (value == "Control")
    return L"Ctrl";
  return u8tow(value);
}

std::wstring DisplayKey(const std::string& value) {
  std::wstring result;
  size_t start = 0;
  while (start < value.size()) {
    const size_t end = value.find('+', start);
    if (!result.empty())
      result += L" + ";
    result += DisplayKeyPart(value.substr(start, end - start));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return result;
}

std::string CapturedKey(WPARAM key) {
  std::string value;
  if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0)
    value += "Control+";
  if ((::GetKeyState(VK_MENU) & 0x8000) != 0)
    value += "Alt+";
  if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0)
    value += "Shift+";
  const auto append = [&](const char* name) { return value + name; };
  if (key >= 'A' && key <= 'Z')
    return value + static_cast<char>(key - 'A' + 'a');
  if (key >= '0' && key <= '9')
    return value + static_cast<char>(key);
  if (key >= VK_F1 && key <= VK_F12)
    return value + "F" + std::to_string(key - VK_F1 + 1);
  switch (key) {
    case VK_OEM_MINUS:
      return append("minus");
    case VK_OEM_PLUS:
      return append("equal");
    case VK_OEM_COMMA:
      return append("comma");
    case VK_OEM_PERIOD:
      return append("period");
    case VK_OEM_4:
      return append("bracketleft");
    case VK_OEM_6:
      return append("bracketright");
    case VK_OEM_3:
      return append("grave");
    case VK_OEM_2:
      return append("slash");
    case VK_SPACE:
      return append("space");
    case VK_TAB:
      return append("Tab");
    case VK_PRIOR:
      return append("Page_Up");
    case VK_NEXT:
      return append("Page_Down");
    case VK_LEFT:
      return append("Left");
    case VK_RIGHT:
      return append("Right");
    case VK_UP:
      return append("Up");
    case VK_DOWN:
      return append("Down");
    default:
      return {};
  }
}

}  // namespace

std::wstring KeySettingsDialog::LocalText(const wchar_t* zh,
                                          const wchar_t* tw,
                                          const wchar_t* en) const {
  return settings_navigation::LocalText(zh, tw, en);
}

void KeySettingsDialog::CreateControls() {
  const auto create = [&](const wchar_t* kind, const std::wstring& caption,
                          DWORD style, WORD id, int x, int y, int w, int h) {
    const RECT box = settings_navigation::MapDialogUnits(m_hWnd, x, y, w, h);
    return settings_navigation::Create(m_hWnd, kind, caption, style, id,
                                       box.left, box.top, box.right - box.left,
                                       box.bottom - box.top);
  };
  const DWORD combo_style = CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE |
                            CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP;
  create(L"STATIC",
         LocalText(L"方案专用按键沿用方案默认。", L"方案專用按鍵沿用方案預設。",
                   L"Scheme-specific keys keep their defaults."),
         SS_CENTERIMAGE, kScopeNote, 14, 4, 510, 10);
  create(L"STATIC",
         LocalText(L"切换中英文", L"切換中英文", L"Switch Chinese / English"),
         SS_CENTERIMAGE, kSwitchCardTitle, 14, 17, 250, 14);
  create(L"BUTTON", L"", BS_OWNERDRAW, IDC_KEY_SWITCH_CARD, 14, 35, 512, 139);
  for (size_t index = 0; index < kSwitchKeys.size(); ++index) {
    // Keep the pairs compact and give each separator an equal-sided gap.
    const int y = 43 + static_cast<int>(index) * 25 + (index >= 2 ? 3 : 0) +
                  (index >= 4 ? 3 : 0);
    create(L"STATIC", kSwitchNames[index], SS_CENTERIMAGE,
           static_cast<WORD>(IDC_KEY_SWITCH_LABEL_BASE + index), 34, y, 160,
           18);
    const WORD id = static_cast<WORD>(IDC_KEY_SWITCH_COMBO_BASE + index);
    create(L"COMBOBOX", L"", combo_style, id, 380, y, 120, 108);
    for (size_t action = 0; action < kActions.size(); ++action) {
      if (index == 4 && action == 4)
        continue;  // librime coerces Caps Lock inline_ascii to clear.
      const std::wstring name =
          LocalText(kActionZh[action], kActionTw[action], kActionEn[action]);
      ::SendDlgItemMessageW(m_hWnd, id, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(name.c_str()));
    }
    settings_navigation::StyleCombo(m_hWnd, id);
  }
  create(
      L"STATIC",
      LocalText(
          L"“不使用”表示该键不切换；其他选项决定如何处理正在输入的文字。",
          L"「不使用」表示該鍵不切換；其他選項決定如何處理正在輸入的文字。",
          L"Do not use disables this key; other actions control active input."),
      SS_CENTERIMAGE, IDC_KEY_NOTE, 16, 178, 508, 11);
  create(L"STATIC", LocalText(L"候选翻页", L"候選翻頁", L"Candidate paging"),
         SS_CENTERIMAGE, kPagingCardTitle, 14, 193, 250, 13);
  create(L"BUTTON", L"", BS_OWNERDRAW, IDC_KEY_PAGING_CARD, 14, 210, 512, 38);
  for (size_t direction = 0; direction < 2; ++direction) {
    const int y = 212 + static_cast<int>(direction) * 18;
    create(L"STATIC",
           direction == 0 ? LocalText(L"上一页", L"上一頁", L"Previous page")
                          : LocalText(L"下一页", L"下一頁", L"Next page"),
           SS_CENTERIMAGE, static_cast<WORD>(kPagingLabelBase + direction), 34,
           y, 160, 14);
    create(L"STATIC", L"", SS_CENTERIMAGE | SS_CENTER,
           static_cast<WORD>(IDC_KEY_PAGE_UP_VALUE + direction), 350, y, 80,
           14);
    const WORD id = static_cast<WORD>(IDC_KEY_PAGE_UP_EDIT + direction);
    HWND button = create(L"BUTTON", LocalText(L"修改", L"修改", L"Change"),
                         BS_PUSHBUTTON | WS_TABSTOP, id, 442, y - 1, 60, 16);
    if (button)
      ::SetWindowSubclass(button, CaptureProc, 12,
                          reinterpret_cast<DWORD_PTR>(this));
    settings_navigation::StyleActionButton(m_hWnd, id);
  }
  create(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_KEY_APPLY, 0, 0, 80,
         18);
  settings_navigation::PrepareCard(m_hWnd, IDC_KEY_SWITCH_CARD);
  settings_navigation::PrepareCard(m_hWnd, IDC_KEY_PAGING_CARD);
}

bool KeySettingsDialog::LoadValues() {
  auto* rime = rime_get_api();
  RimeConfig config{};
  if (!rime->config_open("default", &config))
    return false;
  constexpr std::array<const char*, 5> fallback = {"commit_code", "commit_code",
                                                   "noop", "noop", "clear"};
  for (size_t index = 0; index < kSwitchKeys.size(); ++index) {
    std::string value;
    const std::string path =
        std::string("ascii_composer/switch_key/") + kSwitchKeys[index];
    initial_switches_[index] =
        ReadConfigString(&config, path, &value) ? value : fallback[index];
  }
  rime->config_close(&config);
  switches_ = initial_switches_;
  selected_schemas_ = SelectedSchemas();
  PagingBindings defaults;
  if (!ReadPagingBindings("default", &defaults))
    return false;
  initial_paging_ = defaults.accept;
  for (const auto& id : selected_schemas_) {
    PagingBindings scheme;
    if (ReadPagingBindings(id + ".schema", &scheme)) {
      for (size_t direction = 0; direction < 2; ++direction)
        if (scheme.index[direction] >= 0)
          initial_paging_[direction] = scheme.accept[direction];
      break;
    }
  }
  paging_ = initial_paging_;
  ShowValues();
  return true;
}

void KeySettingsDialog::ShowValues() {
  updating_ = true;
  for (size_t index = 0; index < switches_.size(); ++index) {
    const WORD id = static_cast<WORD>(IDC_KEY_SWITCH_COMBO_BASE + index);
    auto found = std::find(kActions.begin(), kActions.end(), switches_[index]);
    const bool unavailable = index == 4 && switches_[index] == "inline_ascii";
    if (found == kActions.end() || unavailable) {
      const std::wstring label = u8tow(switches_[index]);
      ::SendDlgItemMessageW(m_hWnd, id, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(label.c_str()));
      ::SendDlgItemMessageW(m_hWnd, id, CB_SETCURSEL, index == 4 ? 4 : 5, 0);
    } else {
      ::SendDlgItemMessageW(m_hWnd, id, CB_SETCURSEL,
                            std::distance(kActions.begin(), found), 0);
    }
  }
  for (size_t direction = 0; direction < 2; ++direction)
    ::SetDlgItemTextW(m_hWnd,
                      static_cast<WORD>(IDC_KEY_PAGE_UP_VALUE + direction),
                      DisplayKey(paging_[direction]).c_str());
  updating_ = false;
  RefreshState();
}

void KeySettingsDialog::RefreshState() {
  settings_navigation::SetUnappliedChanges(
      m_hWnd, settings_navigation::Page::Keys, HasUnappliedChanges());
  if (HWND apply = ::GetDlgItem(m_hWnd, IDC_KEY_APPLY))
    ::EnableWindow(apply, HasUnappliedChanges());
}

bool KeySettingsDialog::HasUnappliedChanges() const {
  return switches_ != initial_switches_ || paging_ != initial_paging_;
}

LRESULT KeySettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  ::SetWindowLongPtrW(
      m_hWnd, GWL_EXSTYLE,
      ::GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) | WS_EX_COMPOSITED);
  ::SetWindowLongPtrW(m_hWnd, GWL_STYLE,
                      ::GetWindowLongPtrW(m_hWnd, GWL_STYLE) | WS_CLIPCHILDREN);
  CreateControls();
  if (!LoadValues()) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法读取当前按键配置。", L"無法讀取目前按鍵設定。",
                  L"Could not load key settings.")
            .c_str(),
        L"Weasel", MB_OK | MB_ICONERROR);
    EndDialog(IDCANCEL);
    return settings_navigation::HostedPageInitResult();
  }
  settings_navigation::Install(
      m_hWnd, settings_navigation::Page::Keys,
      {IDC_KEY_APPLY, IDCANCEL, WeaselDisplayUserDataPath().wstring(), {}});
  RefreshState();
  return settings_navigation::HostedPageInitResult();
}

LRESULT KeySettingsDialog::OnDrawItem(UINT,
                                      WPARAM,
                                      LPARAM parameter,
                                      BOOL& handled) {
  const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(parameter);
  if (!draw) {
    handled = FALSE;
    return 0;
  }
  if (draw->CtlID == IDC_KEY_SWITCH_CARD) {
    settings_navigation::DrawCard(*draw);
    const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
    const COLORREF divider = settings_navigation::Mix(
        settings_theme::GetColor(COLOR_3DSHADOW), surface, 42);
    HBRUSH brush = ::CreateSolidBrush(divider);
    if (brush) {
      for (int y : {56, 109}) {
        RECT line = settings_navigation::MapDialogUnits(m_hWnd, 20, y, 466, 1);
        ::OffsetRect(&line, draw->rcItem.left, draw->rcItem.top);
        line.bottom = line.top + 1;
        ::FillRect(draw->hDC, &line, brush);
      }
      ::DeleteObject(brush);
    }
    return TRUE;
  }
  if (draw->CtlID == IDC_KEY_PAGING_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  if (draw->CtlID >= IDC_KEY_SWITCH_COMBO_BASE &&
      draw->CtlID < IDC_KEY_SWITCH_COMBO_BASE + 5) {
    settings_navigation::DrawComboItem(*draw);
    return TRUE;
  }
  handled = FALSE;
  return 0;
}

LRESULT KeySettingsDialog::OnMeasureItem(UINT,
                                         WPARAM,
                                         LPARAM parameter,
                                         BOOL& handled) {
  auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(parameter);
  if (!measure || measure->CtlID < IDC_KEY_SWITCH_COMBO_BASE ||
      measure->CtlID >= IDC_KEY_SWITCH_COMBO_BASE + 5) {
    handled = FALSE;
    return 0;
  }
  measure->itemHeight = settings_navigation::MeasureComboItemHeight(m_hWnd);
  return TRUE;
}

LRESULT KeySettingsDialog::OnStaticColor(UINT,
                                         WPARAM dc,
                                         LPARAM window,
                                         BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id >= IDC_KEY_SWITCH_LABEL_BASE && id < IDC_KEY_SWITCH_LABEL_BASE + 5) ||
      (id >= kPagingLabelBase && id < kPagingLabelBase + 2) ||
      id == IDC_KEY_PAGE_UP_VALUE || id == IDC_KEY_PAGE_DOWN_VALUE) {
    ::SetBkMode(reinterpret_cast<HDC>(dc), TRANSPARENT);
    ::SetTextColor(reinterpret_cast<HDC>(dc),
                   settings_theme::GetColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
  }
  if (id == IDC_KEY_NOTE || id == kScopeNote || id == kSwitchCardTitle ||
      id == kPagingCardTitle) {
    ::SetBkMode(reinterpret_cast<HDC>(dc), TRANSPARENT);
    ::SetTextColor(
        reinterpret_cast<HDC>(dc),
        settings_theme::GetColor(id == IDC_KEY_NOTE || id == kScopeNote
                                     ? COLOR_GRAYTEXT
                                     : COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_BTNFACE));
  }
  handled = FALSE;
  return 0;
}

LRESULT KeySettingsDialog::OnSwitchChanged(WORD code, WORD id, HWND, BOOL&) {
  if (code != CBN_SELCHANGE || updating_)
    return 0;
  const size_t index = id - IDC_KEY_SWITCH_COMBO_BASE;
  const int selected =
      static_cast<int>(::SendDlgItemMessageW(m_hWnd, id, CB_GETCURSEL, 0, 0));
  const int known_count = index == 4 ? 4 : static_cast<int>(kActions.size());
  if (selected >= 0 && selected < known_count) {
    switches_[index] = kActions[selected];
    RefreshState();
  }
  return 0;
}

void KeySettingsDialog::StopCapture() {
  if (!capture_id_)
    return;
  ::SetDlgItemTextW(m_hWnd, capture_id_,
                    LocalText(L"修改", L"修改", L"Change").c_str());
  capture_id_ = 0;
}

LRESULT KeySettingsDialog::OnCaptureClicked(WORD code, WORD id, HWND, BOOL&) {
  if (code != BN_CLICKED)
    return 0;
  StopCapture();
  capture_id_ = id;
  ::SetDlgItemTextW(m_hWnd, id,
                    LocalText(L"请按键", L"請按鍵", L"Press key").c_str());
  ::SetFocus(::GetDlgItem(m_hWnd, id));
  return 0;
}

void KeySettingsDialog::CaptureKey(WORD id, WPARAM key) {
  if (key == VK_ESCAPE) {
    StopCapture();
    return;
  }
  if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
      key == VK_LSHIFT || key == VK_RSHIFT || key == VK_LCONTROL ||
      key == VK_RCONTROL)
    return;
  if ((::GetKeyState(VK_LWIN) & 0x8000) != 0 ||
      (::GetKeyState(VK_RWIN) & 0x8000) != 0)
    return;
  const std::string value = CapturedKey(key);
  if (value.empty() || value == "Control+space" || value == "Alt+F4" ||
      (value.size() == 1 && value[0] >= '0' && value[0] <= '9')) {
    ::SetDlgItemTextW(
        m_hWnd, IDC_KEY_NOTE,
        LocalText(L"此按键不适合翻页，请按其他按键。",
                  L"此按鍵不適合翻頁，請按其他按鍵。",
                  L"This key cannot be used for paging; try another.")
            .c_str());
    return;
  }
  const size_t direction = id == IDC_KEY_PAGE_UP_EDIT ? 0 : 1;
  if (value == paging_[1 - direction]) {
    ::SetDlgItemTextW(m_hWnd, IDC_KEY_NOTE,
                      LocalText(L"上一页和下一页不能使用同一按键。",
                                L"上一頁和下一頁不能使用同一按鍵。",
                                L"Previous and next page need different keys.")
                          .c_str());
    return;
  }
  paging_[direction] = value;
  ::SetDlgItemTextW(m_hWnd,
                    static_cast<WORD>(IDC_KEY_PAGE_UP_VALUE + direction),
                    DisplayKey(value).c_str());
  ::SetDlgItemTextW(
      m_hWnd, IDC_KEY_NOTE,
      LocalText(
          L"“不使用”表示该键不切换；其他选项决定如何处理正在输入的文字。",
          L"「不使用」表示該鍵不切換；其他選項決定如何處理正在輸入的文字。",
          L"Do not use disables this key; other actions control active input.")
          .c_str());
  StopCapture();
  RefreshState();
}

LRESULT CALLBACK KeySettingsDialog::CaptureProc(HWND window,
                                                UINT message,
                                                WPARAM wparam,
                                                LPARAM lparam,
                                                UINT_PTR,
                                                DWORD_PTR data) {
  auto* dialog = reinterpret_cast<KeySettingsDialog*>(data);
  const WORD id = static_cast<WORD>(::GetDlgCtrlID(window));
  if (dialog->capture_id_ == id) {
    if (message == WM_GETDLGCODE)
      return DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
      dialog->CaptureKey(id, wparam);
      return 0;
    }
    if (message == WM_KILLFOCUS)
      dialog->StopCapture();
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(window, CaptureProc, 12);
  return ::DefSubclassProc(window, message, wparam, lparam);
}

LRESULT KeySettingsDialog::OnApply(WORD, WORD, HWND, BOOL&) {
  if (!settings_navigation::RequestApply(m_hWnd))
    ApplyChanges();
  return 0;
}

LRESULT KeySettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (!settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT KeySettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (!settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT KeySettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  if (settings_navigation::PageFromCommand(id) ==
      settings_navigation::Page::Keys)
    return 0;
  if (!settings_navigation::RequestNavigate(m_hWnd, id))
    EndDialog(id);
  return 0;
}

bool KeySettingsDialog::Persist() {
  auto* rime = rime_get_api();
  auto* levers = Levers();
  if (!levers)
    return false;
  struct Target {
    std::string config_id;
    std::filesystem::path path;
    std::filesystem::path backup;
    std::string original;
    bool existed = false;
    bool saved = false;
    RimeCustomSettings* settings = nullptr;
    std::array<int, 2> paging_index{-1, -1};
  };
  std::vector<Target> targets;
  const auto destroy = [&] {
    for (auto& target : targets) {
      if (target.settings) {
        levers->custom_settings_destroy(target.settings);
        target.settings = nullptr;
      }
    }
  };
  const auto rollback = [&] {
    for (const auto& target : targets) {
      if (!target.saved)
        continue;
      if (target.existed)
        ::CopyFileW(target.backup.c_str(), target.path.c_str(), FALSE);
      else
        ::DeleteFileW(target.path.c_str());
    }
  };
  const auto remove_backups = [&] {
    for (const auto& target : targets)
      if (!target.backup.empty())
        ::DeleteFileW(target.backup.c_str());
  };

  std::vector<std::string> ids{"default"};
  for (const auto& schema : selected_schemas_)
    ids.push_back(schema + ".schema");
  const bool paging_changed = paging_ != initial_paging_;
  for (const auto& id : ids) {
    PagingBindings bindings;
    if (paging_changed && !ReadPagingBindings(id, &bindings)) {
      destroy();
      return false;
    }
    bool needed = id == "default" && switches_ != initial_switches_;
    for (size_t direction = 0; direction < 2; ++direction)
      needed = needed || (paging_[direction] != initial_paging_[direction] &&
                          bindings.index[direction] >= 0);
    if (!needed)
      continue;

    Target target;
    target.config_id = id;
    target.paging_index = bindings.index;
    const std::string file_id =
        id.size() > 7 && id.compare(id.size() - 7, 7, ".schema") == 0
            ? id.substr(0, id.size() - 7)
            : id;
    target.path = WeaselUserDataPath() / u8tow(file_id + ".custom.yaml");
    target.existed = std::filesystem::exists(target.path);
    target.original = ReadFile(target.path);
    target.settings =
        levers->custom_settings_init(id.c_str(), "Weasel::KeySettings");
    if (!target.settings) {
      destroy();
      return false;
    }
    targets.push_back(std::move(target));
    Target& current = targets.back();
    if (!levers->load_settings(current.settings) && !current.original.empty()) {
      destroy();
      return false;
    }
    if (id == "default") {
      for (size_t index = 0; index < switches_.size(); ++index) {
        if (switches_[index] == initial_switches_[index])
          continue;
        const std::string key =
            std::string("ascii_composer/switch_key/") + kSwitchKeys[index];
        if (!levers->customize_string(current.settings, key.c_str(),
                                      switches_[index].c_str())) {
          destroy();
          return false;
        }
      }
    }
    for (size_t direction = 0; direction < 2; ++direction) {
      if (paging_[direction] == initial_paging_[direction] ||
          current.paging_index[direction] < 0)
        continue;
      const std::string key = "key_binder/bindings/@" +
                              std::to_string(current.paging_index[direction]) +
                              "/accept";
      if (!levers->customize_string(current.settings, key.c_str(),
                                    paging_[direction].c_str())) {
        destroy();
        return false;
      }
    }
  }
  if (targets.empty())
    return false;

  FILETIME now{};
  ::GetSystemTimeAsFileTime(&now);
  const auto stamp =
      (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  for (size_t index = 0; index < targets.size(); ++index) {
    auto& target = targets[index];
    target.backup = target.path.wstring() + L".before-key-settings-" +
                    std::to_wstring(stamp) + L"-" + std::to_wstring(index) +
                    L".bak";
    if (target.existed &&
        !::CopyFileW(target.path.c_str(), target.backup.c_str(), TRUE)) {
      target.backup.clear();
      rollback();
      remove_backups();
      destroy();
      return false;
    }
    if (ReadFile(target.path) != target.original) {
      rollback();
      remove_backups();
      destroy();
      return false;
    }
    target.saved = true;
    if (!levers->save_settings(target.settings)) {
      rollback();
      remove_backups();
      destroy();
      return false;
    }
    RimeConfig verify{};
    const std::string updated = ReadFile(target.path);
    const bool parsed =
        !updated.empty() && rime->config_load_string(&verify, updated.c_str());
    if (verify.ptr)
      rime->config_close(&verify);
    if (!parsed) {
      rollback();
      remove_backups();
      destroy();
      return false;
    }
  }
  destroy();
  if (Configurator().UpdateWorkspace(true) != 0) {
    rollback();
    remove_backups();
    Configurator().UpdateWorkspace(true);
    return false;
  }

  RimeConfig config{};
  bool applied = rime->config_open("default", &config) != FALSE;
  if (applied) {
    for (size_t index = 0; index < switches_.size(); ++index) {
      if (switches_[index] == initial_switches_[index])
        continue;
      std::string value;
      const std::string key =
          std::string("ascii_composer/switch_key/") + kSwitchKeys[index];
      applied =
          ReadConfigString(&config, key, &value) && value == switches_[index];
      if (!applied)
        break;
    }
    rime->config_close(&config);
  }
  if (applied && paging_changed) {
    for (const auto& target : targets) {
      PagingBindings bindings;
      if (!ReadPagingBindings(target.config_id, &bindings)) {
        applied = false;
        break;
      }
      for (size_t direction = 0; direction < 2; ++direction) {
        if (paging_[direction] != initial_paging_[direction] &&
            target.paging_index[direction] >= 0 &&
            bindings.accept[direction] != paging_[direction]) {
          applied = false;
          break;
        }
      }
      if (!applied)
        break;
    }
  }
  if (!applied) {
    rollback();
    remove_backups();
    Configurator().UpdateWorkspace(true);
    return false;
  }
  remove_backups();
  return true;
}

bool KeySettingsDialog::ApplyChanges() {
  if (!HasUnappliedChanges())
    return true;
  if (!Persist()) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法应用按键设置。请检查配置文件是否被其他程序修改。",
                  L"無法套用按鍵設定。請檢查設定檔是否被其他程式修改。",
                  L"Could not apply key settings. Check for external changes.")
            .c_str(),
        L"Weasel", MB_OK | MB_ICONERROR);
    return false;
  }
  initial_switches_ = switches_;
  initial_paging_ = paging_;
  RefreshState();
  return true;
}
