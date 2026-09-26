#include "stdafx.h"
#include "LayoutEffectsSettingsDialog.h"

#include "AppearancePreview.h"
#include "Configurator.h"
#include "UIStyleSettings.h"

#include <WeaselUserSettings.h>
#include <WeaselUtility.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {
struct LayoutField {
  const char* key;
  const wchar_t* zh;
  const wchar_t* tw;
  const wchar_t* en;
  int fallback;
  int minimum;
  int maximum;
};

constexpr std::array<LayoutField, 20> kFields{{
    {"border_width", L"边框宽度", L"邊框寬度", L"Border width", 1, 0, 1000},
    {"corner_radius", L"候选窗口圆角半径", L"候選視窗圓角半徑",
     L"Window corner radius", 11, 0, 1000},
    {"margin_x", L"横向边距", L"橫向邊距", L"Horizontal margin", 11, -1000,
     1000},
    {"margin_y", L"纵向边距", L"縱向邊距", L"Vertical margin", 7, -1000, 1000},
    {"round_corner", L"候选背景色块圆角半径", L"候選背景色塊圓角半徑",
     L"Highlight radius", 8, 0, 1000},
    {"candidate_spacing", L"候选项间距", L"候選項間距", L"Candidate gap", 6, 0,
     1000},
    {"hilite_padding_x", L"高亮区域左右内距", L"高亮區域左右內距",
     L"Highlight inset X", 8, 0, 1000},
    {"hilite_padding_y", L"高亮区域上下内距", L"高亮區域上下內距",
     L"Highlight inset Y", 4, 0, 1000},
    {"hilite_spacing", L"候选项与标签间距", L"候選項與標籤間距", L"Label gap",
     5, 0, 1000},
    {"spacing", L"编码区域与候选区域间距", L"編碼區域與候選區域間距",
     L"Preedit gap", 5, 0, 1000},
    {"min_width", L"最小宽度", L"最小寬度", L"Minimum width", 130, 0, 10000},
    {"max_width", L"最大宽度", L"最大寬度", L"Maximum width", 0, 0, 10000},
    {"shadow_radius", L"阴影半径", L"陰影半徑", L"Shadow radius", 6, 0, 1000},
    {"shadow_offset_x", L"阴影横向偏移", L"陰影橫向偏移", L"Shadow offset X", 0,
     -1000, 1000},
    {"shadow_offset_y", L"阴影纵向偏移", L"陰影縱向偏移", L"Shadow offset Y", 2,
     -1000, 1000},
    {"baseline", L"基线（字号百分比）", L"基線（字號百分比）", L"Baseline (%)",
     0, 0, 1000},
    {"linespacing", L"行距（字号百分比）", L"行距（字號百分比）",
     L"Line spacing (%)", 0, 0, 1000},
    {"min_height", L"最小高度", L"最小高度", L"Minimum height", 0, 0, 10000},
    {"max_height", L"最大高度", L"最大高度", L"Maximum height", 0, 0, 10000},
    {"hilite_padding", L"高亮区域与文字间距", L"高亮區域與文字間距",
     L"Highlight padding", 5, 0, 1000},
}};
static_assert(kFields.size() == 20);

constexpr int kVisibleRows = 10;
constexpr int kFirstRowY = 45;
constexpr int kRowHeight = 19;
constexpr int kInputHeight = 10;
constexpr int kInputOffsetY = 2;
constexpr int kGroupRows[4] = {6, 2, 10, 23};
constexpr int kSettingsColumnWidthDlu = 318;
constexpr int kColumnGapDlu = 12;
constexpr int kPreviewColumnLeftDlu = settings_navigation::kPageInsetDlu +
                                      kSettingsColumnWidthDlu + kColumnGapDlu;
constexpr int kPreviewColumnWidthDlu = settings_navigation::kPageBodyWidthDlu -
                                       kSettingsColumnWidthDlu - kColumnGapDlu;
int GroupRowCount(int group, const std::string&) {
  return kGroupRows[group];
}

std::vector<std::string> SelectedSchemaIds() {
  auto* levers = reinterpret_cast<RimeLeversApi*>(
      rime_get_api()->find_module("levers")->get_api());
  auto* switcher = levers->switcher_settings_init();
  if (!switcher)
    return {};
  std::vector<std::string> ids;
  if (levers->load_settings(reinterpret_cast<RimeCustomSettings*>(switcher))) {
    RimeSchemaList selected{};
    levers->get_selected_schema_list(switcher, &selected);
    std::set<std::string> unique;
    for (size_t i = 0; i < selected.size; ++i)
      if (selected.list[i].schema_id && *selected.list[i].schema_id)
        if (unique.emplace(selected.list[i].schema_id).second)
          ids.emplace_back(selected.list[i].schema_id);
  }
  levers->custom_settings_destroy(
      reinterpret_cast<RimeCustomSettings*>(switcher));
  return ids;
}

int CurrentPageSize() {
  auto* rime = rime_get_api();
  int value = 6;
  const auto selected = SelectedSchemaIds();
  const std::string config_id =
      selected.empty() ? "default" : selected.front() + ".schema";
  RimeConfig config{};
  if (rime->config_open(config_id.c_str(), &config)) {
    rime->config_get_int(&config, "menu/page_size", &value);
    rime->config_close(&config);
  }
  return value >= 1 && value <= 9 ? value : 6;
}

std::string ReadPageSizeFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool SavePageSize(int value) {
  if (value < 1 || value > 9)
    return false;
  auto* rime = rime_get_api();
  auto* levers =
      reinterpret_cast<RimeLeversApi*>(rime->find_module("levers")->get_api());
  std::vector<std::string> config_ids{"default"};
  for (const auto& id : SelectedSchemaIds())
    config_ids.push_back(id + ".schema");
  struct Target {
    RimeCustomSettings* settings = nullptr;
    std::filesystem::path path;
    std::filesystem::path backup;
    std::string original;
    bool existed = false;
  };
  std::vector<Target> targets;
  const auto cleanup = [&] {
    for (auto& target : targets)
      levers->custom_settings_destroy(target.settings);
  };
  const auto rollback = [&] {
    for (const auto& target : targets) {
      if (target.backup.empty())
        continue;
      if (target.existed)
        ::CopyFileW(target.backup.c_str(), target.path.c_str(), FALSE);
      else
        ::DeleteFileW(target.path.c_str());
    }
  };
  FILETIME now{};
  ::GetSystemTimeAsFileTime(&now);
  const auto stamp =
      (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  for (const auto& id : config_ids) {
    auto* settings =
        levers->custom_settings_init(id.c_str(), "Weasel::LayoutSettings");
    if (!settings) {
      cleanup();
      return false;
    }
    Target target;
    target.settings = settings;
    const auto file_id =
        id.size() > 7 && id.compare(id.size() - 7, 7, ".schema") == 0
            ? id.substr(0, id.size() - 7)
            : id;
    target.path = WeaselUserDataPath() / u8tow(file_id + ".custom.yaml");
    target.existed = std::filesystem::exists(target.path);
    target.original = ReadPageSizeFile(target.path);
    targets.push_back(std::move(target));
    if (!levers->load_settings(settings) && !targets.back().original.empty()) {
      cleanup();
      return false;
    }
    if (!levers->customize_int(settings, "menu/page_size", value)) {
      cleanup();
      return false;
    }
  }
  for (size_t i = 0; i < targets.size(); ++i) {
    auto& target = targets[i];
    target.backup = target.path.wstring() + L".before-page-size-" +
                    std::to_wstring(stamp) + L"-" + std::to_wstring(i) +
                    L".bak";
    if (target.existed &&
        !::CopyFileW(target.path.c_str(), target.backup.c_str(), TRUE)) {
      target.backup.clear();
      rollback();
      cleanup();
      return false;
    }
    if (ReadPageSizeFile(target.path) != target.original ||
        !levers->save_settings(target.settings)) {
      rollback();
      cleanup();
      return false;
    }
    RimeConfig verify{};
    const std::string updated = ReadPageSizeFile(target.path);
    const bool valid =
        !updated.empty() && rime->config_load_string(&verify, updated.c_str());
    if (verify.ptr)
      rime->config_close(&verify);
    if (!valid) {
      rollback();
      cleanup();
      return false;
    }
  }
  cleanup();
  return true;
}

constexpr const char* kLayoutTypes[] = {"",
                                        "horizontal",
                                        "vertical",
                                        "vertical_text",
                                        "vertical+fullscreen",
                                        "horizontal+fullscreen"};
constexpr const char* kAlignTypes[] = {"top", "center", "bottom"};
constexpr const char* kWindowOptions[] = {"vertical_text_left_to_right",
                                          "vertical_text_with_wrap",
                                          "vertical_auto_reverse"};
constexpr int kWindowOptionIds[] = {IDC_LAYOUT_TEXT_LEFT_TO_RIGHT,
                                    IDC_LAYOUT_TEXT_WRAP,
                                    IDC_LAYOUT_AUTO_REVERSE};
constexpr const char* kWindowKeys[] = {"fullscreen",
                                       "horizontal",
                                       "vertical_text",
                                       "vertical_text_left_to_right",
                                       "vertical_text_with_wrap",
                                       "vertical_auto_reverse"};
constexpr const wchar_t* kWindowZh[] = {L"候选窗口全屏显示",
                                        L"候选项横排",
                                        L"竖排文本",
                                        L"竖排方向从左到右",
                                        L"文本竖排模式下自动换行",
                                        L"候选窗口位于光标上方时倒序排列"};
constexpr const wchar_t* kWindowTw[] = {L"候選視窗全螢幕顯示",
                                        L"候選項橫排",
                                        L"直排文字",
                                        L"直排方向從左到右",
                                        L"文字直排模式下自動換行",
                                        L"候選視窗位於游標上方時倒序排列"};
constexpr const wchar_t* kWindowEn[] = {L"Fullscreen candidate window",
                                        L"Horizontal candidates",
                                        L"Vertical text",
                                        L"Vertical text left to right",
                                        L"Wrap vertical text",
                                        L"Reverse above cursor"};
constexpr const char* kOtherBoolKeys[] = {
    "ascii_tip_follow_cursor", "enhanced_position", "display_tray_icon",
    "paging_on_scroll", "click_to_capture"};
constexpr const wchar_t* kOtherZh[] = {
    L"标签格式",          L"标记字符",     L"ASCII 提示跟随鼠标",
    L"增强候选框定位",    L"显示托盘图标", L"次像素反锯齿设定",
    L"候选项略写长度",    L"鼠标悬停动作", L"滚轮用于翻页",
    L"单击候选项创建截图"};
constexpr const wchar_t* kOtherTw[] = {
    L"標籤格式",           L"標記字元",
    L"ASCII 提示跟隨滑鼠", L"增強候選框定位",
    L"顯示系統匣圖示",     L"次像素反鋸齒設定",
    L"候選項略寫長度",     L"滑鼠懸停動作",
    L"滾輪用於翻頁",       L"按一下候選項建立截圖"};
constexpr const wchar_t* kOtherEn[] = {L"Label format",
                                       L"Mark text",
                                       L"ASCII tip follows pointer",
                                       L"Enhanced positioning",
                                       L"Display tray icon",
                                       L"Antialias mode",
                                       L"Candidate abbreviation length",
                                       L"Hover action",
                                       L"Scroll to page",
                                       L"Click candidate to capture"};
constexpr const char* kPreeditTypes[] = {"composition", "preview",
                                         "preview_all"};
constexpr const char* kAntialiasModes[] = {"default", "force_dword",
                                           "cleartype", "grayscale", "aliased"};
constexpr const char* kHoverTypes[] = {"none", "hilite", "semi_hilite"};
constexpr int kLayoutRowFields[] = {15, 16, 18, 11, 17, 10, 0,  2,  3, 9,
                                    5,  8,  19, 6,  7,  13, 14, 12, 1, 4};

int ComboIndex(const std::string& value,
               const char* const* options,
               int count) {
  for (int index = 0; index < count; ++index)
    if (value == options[index])
      return index;
  return 0;
}
bool EffectiveHorizontal(const std::string& type,
                         bool horizontal,
                         bool inherited_vertical_text) {
  return type.empty() ? horizontal && !inherited_vertical_text
                      : type == "horizontal" || type == "horizontal+fullscreen";
}

std::wstring FormatPreviewLabel(const std::wstring& format, int number) {
  const std::wstring value = std::to_wstring(number);
  std::wstring result = format;
  size_t position = 0;
  bool replaced = false;
  while ((position = result.find(L"%s", position)) != std::wstring::npos) {
    result.replace(position, 2, value);
    position += value.size();
    replaced = true;
  }
  return replaced ? result : value + result;
}

std::wstring AbbreviatePreviewCandidate(const std::wstring& value,
                                        int maximum) {
  if (maximum <= 0 || value.size() <= static_cast<size_t>(maximum))
    return value;
  if (maximum == 1)
    return L"…";
  return value.substr(0, static_cast<size_t>(maximum - 1)) + L"…";
}

void DrawLayoutScrollbar(HWND window, HDC dc) {
  RECT bounds{};
  ::GetClientRect(window, &bounds);
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  ::FillRect(dc, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
  if (width <= 0 || height <= 0)
    return;

  const int arrow_height = (std::min)(width, height / 4);
  const int rail_width = (std::max)(3, width / 3);
  const int rail_left = (width - rail_width) / 2;
  const int rail_top = arrow_height + 2;
  const int rail_bottom = height - arrow_height - 2;
  if (rail_bottom <= rail_top)
    return;

  const HGDIOBJ old_pen = ::SelectObject(dc, ::GetStockObject(NULL_PEN));
  HBRUSH rail_brush =
      ::CreateSolidBrush(settings_theme::GetColor(COLOR_BTNFACE));
  const HGDIOBJ old_brush = ::SelectObject(dc, rail_brush);
  ::RoundRect(dc, rail_left, rail_top, rail_left + rail_width, rail_bottom,
              rail_width, rail_width);

  SCROLLINFO info{sizeof(info)};
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  ::GetScrollInfo(window, SB_CTL, &info);
  const int count = (std::max)(1, info.nMax - info.nMin + 1);
  const int track_height = rail_bottom - rail_top;
  const int thumb_height = (std::min)(
      track_height,
      (std::max)(width,
                 ::MulDiv(track_height, static_cast<int>(info.nPage), count)));
  const int max_position = (std::max)(0, count - static_cast<int>(info.nPage));
  const int position = std::clamp(info.nPos - info.nMin, 0, max_position);
  const int thumb_top =
      rail_top + (max_position ? ::MulDiv(track_height - thumb_height, position,
                                          max_position)
                               : 0);
  const int thumb_width = (std::max)(rail_width + 2, width - 4);
  const int thumb_left = (width - thumb_width) / 2;
  HBRUSH thumb_brush =
      ::CreateSolidBrush(settings_theme::GetColor(COLOR_3DSHADOW));
  ::SelectObject(dc, thumb_brush);
  ::RoundRect(dc, thumb_left, thumb_top, thumb_left + thumb_width,
              thumb_top + thumb_height, thumb_width, thumb_width);

  const int center = width / 2;
  const int half = (std::max)(2, width / 5);
  const int up_y = arrow_height / 2;
  const int down_y = height - arrow_height / 2;
  POINT up[3]{
      {center, up_y - 2}, {center - half, up_y + 2}, {center + half, up_y + 2}};
  POINT down[3]{{center - half, down_y - 2},
                {center + half, down_y - 2},
                {center, down_y + 2}};
  ::Polygon(dc, up, 3);
  ::Polygon(dc, down, 3);
  ::SelectObject(dc, old_brush);
  ::SelectObject(dc, old_pen);
  ::DeleteObject(thumb_brush);
  ::DeleteObject(rail_brush);
}

LRESULT CALLBACK LayoutScrollbarProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     UINT_PTR,
                                     DWORD_PTR) {
  if (message == WM_ERASEBKGND)
    return 1;
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(window, &paint);
    DrawLayoutScrollbar(window, dc);
    ::EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_PRINTCLIENT) {
    DrawLayoutScrollbar(window, reinterpret_cast<HDC>(wparam));
    return 0;
  }
  if (message == WM_NCDESTROY)
    ::RemoveWindowSubclass(window, LayoutScrollbarProc, 10);
  const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
  if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
      message == WM_MOUSEMOVE || message == WM_CAPTURECHANGED ||
      message == WM_THEMECHANGED)
    ::InvalidateRect(window, nullptr, FALSE);
  return result;
}

}  // namespace

std::wstring LayoutEffectsSettingsDialog::LocalText(const wchar_t* zh,
                                                    const wchar_t* tw,
                                                    const wchar_t* en) const {
  return settings_navigation::LocalText(zh, tw, en);
}

void LayoutEffectsSettingsDialog::CreateControls() {
  const auto create = [&](const wchar_t* kind, const std::wstring& caption,
                          DWORD style, WORD id, int x, int y, int w, int h) {
    const RECT box = settings_navigation::MapDialogUnits(m_hWnd, x, y, w, h);
    HWND control = settings_navigation::Create(
        m_hWnd, kind, caption, style, id, box.left, box.top,
        box.right - box.left, box.bottom - box.top);
    if (control && _wcsicmp(kind, L"EDIT") == 0)
      settings_navigation::StyleVerticallyCenteredInput(m_hWnd, id, 0);
    return control;
  };
  const DWORD combo_style = CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE |
                            CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP;
  create(L"BUTTON", L"", BS_OWNERDRAW, IDC_LAYOUT_FRAME_CARD, 14, 36, 316, 212);

  const std::wstring categories[] = {
      LocalText(L"窗口", L"視窗", L"Window"),
      LocalText(L"预编辑区", L"預編輯區", L"Preedit"),
      LocalText(L"其他选项", L"其他選項", L"Other"),
      LocalText(L"布局", L"佈局", L"Layout")};
  for (int i = 0; i < 4; ++i) {
    create(L"BUTTON", categories[i],
           BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0) | WS_TABSTOP,
           static_cast<WORD>(IDC_LAYOUT_CATEGORY_WINDOW + i), 14 + i * 60, 14,
           60, 14);
    settings_navigation::StyleSegmentedToggle(
        m_hWnd, static_cast<WORD>(IDC_LAYOUT_CATEGORY_WINDOW + i),
        i == 0   ? settings_navigation::ToggleState::Segment::Left
        : i == 3 ? settings_navigation::ToggleState::Segment::Right
                 : settings_navigation::ToggleState::Segment::Middle,
        settings_navigation::ToggleState::Background::ButtonFace);
  }

  for (int i = 0; i < 6; ++i) {
    create(L"STATIC", LocalText(kWindowZh[i], kWindowTw[i], kWindowEn[i]),
           SS_CENTERIMAGE, static_cast<WORD>(IDC_LAYOUT_WINDOW_LABEL_BASE + i),
           28, 115, 220, 14);
    create(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
           static_cast<WORD>(IDC_LAYOUT_WINDOW_BOOL_BASE + i), 268, 115, 48,
           14);
    settings_navigation::StyleSwitch(
        m_hWnd, static_cast<WORD>(IDC_LAYOUT_WINDOW_BOOL_BASE + i));
  }

  create(L"STATIC",
         LocalText(L"行内显示预编辑区", L"行內顯示預編輯區", L"Inline preedit"),
         SS_CENTERIMAGE, IDC_LAYOUT_PREEDIT_LABEL_BASE, 28, 115, 220, 14);
  create(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
         IDC_LAYOUT_PREEDIT_INLINE, 268, 115, 48, 14);
  settings_navigation::StyleSwitch(m_hWnd, IDC_LAYOUT_PREEDIT_INLINE);
  create(
      L"STATIC",
      LocalText(L"预编辑区显示内容", L"預編輯區顯示內容", L"Preedit content"),
      SS_CENTERIMAGE, IDC_LAYOUT_PREEDIT_LABEL_BASE + 1, 28, 115, 174, 14);
  create(L"COMBOBOX", L"", combo_style, IDC_LAYOUT_PREEDIT_TYPE, 207, 115, 109,
         90);

  for (int i = 0; i < 10; ++i)
    create(L"STATIC", LocalText(kOtherZh[i], kOtherTw[i], kOtherEn[i]),
           SS_CENTERIMAGE, static_cast<WORD>(IDC_LAYOUT_OTHER_LABEL_BASE + i),
           28, 115, 174, 14);
  create(L"EDIT", L"%s", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
         IDC_LAYOUT_LABEL_FORMAT, 207, 117, 109, kInputHeight);
  create(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
         IDC_LAYOUT_MARK_TEXT, 207, 117, 109, kInputHeight);
  for (int i = 0; i < 5; ++i) {
    create(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
           static_cast<WORD>(IDC_LAYOUT_OTHER_BOOL_BASE + i), 268, 115, 48, 14);
    settings_navigation::StyleSwitch(
        m_hWnd, static_cast<WORD>(IDC_LAYOUT_OTHER_BOOL_BASE + i));
  }
  create(L"COMBOBOX", L"", combo_style, IDC_LAYOUT_ANTIALIAS_MODE, 207, 115,
         109, 110);
  create(L"EDIT", L"0",
         WS_TABSTOP | WS_BORDER | WS_CLIPSIBLINGS | ES_CENTER | ES_AUTOHSCROLL,
         IDC_LAYOUT_ABBREVIATE_LENGTH, 268, 117, 48, kInputHeight);
  create(L"COMBOBOX", L"", combo_style, IDC_LAYOUT_HOVER_TYPE, 207, 115, 109,
         80);

  create(L"STATIC", LocalText(L"布局类型", L"佈局類型", L"Layout type"),
         SS_CENTERIMAGE, 1413, 28, 115, 174, 14);
  create(L"STATIC",
         LocalText(L"候选词个数", L"候選詞個數", L"Candidates per page"),
         SS_CENTERIMAGE, 1415, 28, 115, 174, 14);
  create(L"EDIT", L"6",
         WS_TABSTOP | WS_BORDER | WS_CLIPSIBLINGS | ES_CENTER | ES_AUTOHSCROLL,
         IDC_LAYOUT_PAGE_SIZE, 268, 117, 48, kInputHeight);
  create(L"COMBOBOX", L"", combo_style, IDC_LAYOUT_TYPE, 207, 115, 109, 100);
  create(L"STATIC", LocalText(L"对齐方式", L"對齊方式", L"Alignment"),
         SS_CENTERIMAGE, 1414, 28, 115, 174, 14);
  create(L"COMBOBOX", L"", combo_style, IDC_LAYOUT_ALIGN, 207, 115, 109, 70);
  for (int field = 0; field < static_cast<int>(kFields.size()); ++field) {
    const auto& item = kFields[field];
    create(L"STATIC", LocalText(item.zh, item.tw, item.en), SS_CENTERIMAGE,
           static_cast<WORD>(1340 + field), 28, 115, 174, 14);
    create(
        L"EDIT", std::to_wstring(item.fallback),
        WS_TABSTOP | WS_BORDER | WS_CLIPSIBLINGS | ES_CENTER | ES_AUTOHSCROLL,
        static_cast<WORD>(IDC_LAYOUT_VALUE_BASE + field), 268, 117, 48,
        kInputHeight);
  }

  create(L"SCROLLBAR", L"", SBS_VERT | WS_TABSTOP, IDC_LAYOUT_SCROLLBAR, 320,
         46, 7, 193);
  if (HWND scrollbar = ::GetDlgItem(m_hWnd, IDC_LAYOUT_SCROLLBAR)) {
    ::SetWindowTheme(
        scrollbar,
        settings_theme::Colors().dark ? L"DarkMode_Explorer" : L"Explorer",
        nullptr);
    ::SetWindowSubclass(scrollbar, LayoutScrollbarProc, 10, 0);
  }

  for (const std::wstring& text :
       {LocalText(L"未设置", L"未設定", L"Not set"),
        LocalText(L"横向", L"橫向", L"Horizontal"),
        LocalText(L"竖直", L"直向", L"Vertical"),
        LocalText(L"文本竖直", L"文字直排", L"Vertical text"),
        LocalText(L"竖向全屏", L"直向全螢幕", L"Vertical fullscreen"),
        LocalText(L"横向全屏", L"橫向全螢幕", L"Horizontal fullscreen")})
    ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_TYPE, CB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(text.c_str()));
  for (const std::wstring& text : {LocalText(L"顶部", L"頂部", L"Top"),
                                   LocalText(L"居中", L"置中", L"Center"),
                                   LocalText(L"底部", L"底部", L"Bottom")})
    ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_ALIGN, CB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(text.c_str()));
  for (const std::wstring& text :
       {LocalText(L"编码", L"編碼", L"Composition"),
        LocalText(L"高亮候选", L"高亮候選", L"Preview"),
        LocalText(L"全部候选", L"全部候選", L"Preview all")})
    ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_PREEDIT_TYPE, CB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(text.c_str()));
  for (const std::wstring& text :
       {LocalText(L"默认", L"預設", L"Default"),
        LocalText(L"强制 DirectWrite", L"強制 DirectWrite",
                  L"Force DirectWrite"),
        std::wstring(L"ClearType"),
        LocalText(L"灰度抗锯齿", L"灰階反鋸齒", L"Grayscale"),
        LocalText(L"无抗锯齿", L"無反鋸齒", L"Aliased")})
    ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_ANTIALIAS_MODE, CB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(text.c_str()));
  for (const std::wstring& text :
       {LocalText(L"无动作", L"無動作", L"None"),
        LocalText(L"选中候选", L"選中候選", L"Hilite"),
        LocalText(L"高亮候选", L"高亮候選", L"Semi-hilite")})
    ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_HOVER_TYPE, CB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(text.c_str()));

  for (WORD id : {IDC_LAYOUT_TYPE, IDC_LAYOUT_ALIGN, IDC_LAYOUT_PREEDIT_TYPE,
                  IDC_LAYOUT_ANTIALIAS_MODE, IDC_LAYOUT_HOVER_TYPE})
    settings_navigation::StyleCombo(m_hWnd, id);

  create(L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS, 1314,
         kPreviewColumnLeftDlu, settings_navigation::kFirstCardTopDlu,
         kPreviewColumnWidthDlu, 113);
  create(L"STATIC", L"", SS_OWNERDRAW | WS_CLIPSIBLINGS, 1315,
         kPreviewColumnLeftDlu, 135, kPreviewColumnWidthDlu, 113);
  create(L"BUTTON",
         LocalText(L"恢复本页默认", L"還原本頁預設", L"Restore this page"),
         BS_PUSHBUTTON | WS_TABSTOP, IDC_LAYOUT_RESTORE, 14, 258, 80, 18);
  create(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_LAYOUT_APPLY, 0, 0, 80,
         18);
  settings_navigation::PrepareCard(m_hWnd, IDC_LAYOUT_FRAME_CARD);
  for (WORD id : {IDC_LAYOUT_RESTORE})
    settings_navigation::StyleActionButton(m_hWnd, id);
  for (WORD id : {1314, 1315}) {
    HWND preview = ::GetDlgItem(m_hWnd, id);
    const LONG_PTR style = ::GetWindowLongPtrW(preview, GWL_STYLE);
    ::SetWindowLongPtrW(preview, GWL_STYLE,
                        style & ~static_cast<LONG_PTR>(WS_BORDER));
  }
}
bool LayoutEffectsSettingsDialog::LoadValues() {
  if (!settings_ || !settings_->LoadAppearance())
    return false;
  for (size_t index = 0; index < kFields.size(); ++index) {
    const auto& field = kFields[index];
    initial_[index] =
        settings_->PreviewLayoutSignedInt(field.key, field.fallback);
  }
  draft_ = initial_;
  for (size_t i = 0; i < window_values_.size(); ++i)
    window_values_[i] = initial_window_values_[i] =
        settings_->PreviewStyleBool(kWindowKeys[i], false);
  initial_horizontal_ = horizontal_ = window_values_[1];
  inherited_vertical_text_ = window_values_[2];
  inherited_fullscreen_ = window_values_[0];
  initial_layout_type_ = settings_->PreviewLayoutString("type", "");
  layout_type_ = initial_layout_type_;
  initial_align_type_ = settings_->PreviewLayoutString("align_type", "center");
  align_type_ = initial_align_type_;
  for (size_t i = 0; i < window_options_.size(); ++i)
    window_options_[i] = initial_window_options_[i] = window_values_[i + 3];
  inline_preedit_ = initial_inline_preedit_ =
      settings_->PreviewStyleBool("inline_preedit", false);
  preedit_type_ = initial_preedit_type_ =
      wtou8(settings_->PreviewStyleString("preedit_type", L"composition"));
  label_format_ = initial_label_format_ =
      settings_->PreviewStyleString("label_format", L"%s");
  mark_text_ = initial_mark_text_ =
      settings_->PreviewStyleString("mark_text", L"");
  for (size_t i = 0; i < other_values_.size(); ++i)
    other_values_[i] = initial_other_values_[i] =
        settings_->PreviewStyleBool(kOtherBoolKeys[i], false);
  antialias_mode_ = initial_antialias_mode_ =
      wtou8(settings_->PreviewStyleString("antialias_mode", L"default"));
  candidate_abbreviate_length_ = initial_candidate_abbreviate_length_ =
      settings_->PreviewStyleInt("candidate_abbreviate_length", 0);
  hover_type_ = initial_hover_type_ =
      wtou8(settings_->PreviewStyleString("hover_type", L"none"));
  page_size_ = initial_page_size_ = CurrentPageSize();
  valid_ = true;
  ShowValues();
  return true;
}

void LayoutEffectsSettingsDialog::ShowValues() {
  updating_ = true;
  for (size_t index = 0; index < draft_.size(); ++index)
    ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_VALUE_BASE + index,
                      std::to_wstring(draft_[index]).c_str());
  ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_TYPE, CB_SETCURSEL,
                        ComboIndex(layout_type_, kLayoutTypes, 6), 0);
  ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_ALIGN, CB_SETCURSEL,
                        ComboIndex(align_type_, kAlignTypes, 3), 0);
  for (size_t i = 0; i < window_values_.size(); ++i)
    ::CheckDlgButton(m_hWnd, IDC_LAYOUT_WINDOW_BOOL_BASE + i,
                     window_values_[i] ? BST_CHECKED : BST_UNCHECKED);
  for (size_t i = 0; i < window_options_.size(); ++i)
    ::CheckDlgButton(m_hWnd, kWindowOptionIds[i],
                     window_options_[i] ? BST_CHECKED : BST_UNCHECKED);
  ::CheckDlgButton(m_hWnd, IDC_LAYOUT_PREEDIT_INLINE,
                   inline_preedit_ ? BST_CHECKED : BST_UNCHECKED);
  ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_PREEDIT_TYPE, CB_SETCURSEL,
                        ComboIndex(preedit_type_, kPreeditTypes, 3), 0);
  ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_LABEL_FORMAT, label_format_.c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_MARK_TEXT, mark_text_.c_str());
  for (size_t i = 0; i < other_values_.size(); ++i)
    ::CheckDlgButton(m_hWnd, IDC_LAYOUT_OTHER_BOOL_BASE + i,
                     other_values_[i] ? BST_CHECKED : BST_UNCHECKED);
  ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_ANTIALIAS_MODE, CB_SETCURSEL,
                        ComboIndex(antialias_mode_, kAntialiasModes, 5), 0);
  ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_ABBREVIATE_LENGTH,
                    std::to_wstring(candidate_abbreviate_length_).c_str());
  ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_PAGE_SIZE,
                    std::to_wstring(page_size_).c_str());
  ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_HOVER_TYPE, CB_SETCURSEL,
                        ComboIndex(hover_type_, kHoverTypes, 3), 0);
  updating_ = false;
  RefreshState();
  RefreshPreview();
}

void LayoutEffectsSettingsDialog::ReadValues() {
  if (updating_)
    return;
  auto next = draft_;
  valid_ = true;
  for (size_t index = 0; index < kFields.size(); ++index) {
    wchar_t text[32]{};
    ::GetDlgItemTextW(m_hWnd, IDC_LAYOUT_VALUE_BASE + index, text,
                      static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(text, &end, 10);
    while (end && *end == L' ')
      ++end;
    if (errno || end == text || !end || *end ||
        parsed < kFields[index].minimum || parsed > kFields[index].maximum) {
      valid_ = false;
      break;
    }
    next[index] = static_cast<int>(parsed);
  }
  wchar_t abbreviation[32]{};
  ::GetDlgItemTextW(m_hWnd, IDC_LAYOUT_ABBREVIATE_LENGTH, abbreviation,
                    static_cast<int>(std::size(abbreviation)));
  wchar_t* abbreviation_end = nullptr;
  errno = 0;
  const long abbreviation_value =
      std::wcstol(abbreviation, &abbreviation_end, 10);
  if (valid_ && (errno || abbreviation_end == abbreviation ||
                 !abbreviation_end || *abbreviation_end ||
                 abbreviation_value < 0 || abbreviation_value > 10000))
    valid_ = false;
  wchar_t page_size_text[32]{};
  ::GetDlgItemTextW(m_hWnd, IDC_LAYOUT_PAGE_SIZE, page_size_text,
                    static_cast<int>(std::size(page_size_text)));
  wchar_t* page_size_end = nullptr;
  errno = 0;
  const long page_size_value = std::wcstol(page_size_text, &page_size_end, 10);
  if (valid_ && (errno || page_size_end == page_size_text || !page_size_end ||
                 *page_size_end || page_size_value < 1 || page_size_value > 9))
    valid_ = false;
  if (valid_ && next[11] != 0 && next[11] < next[10])
    valid_ = false;
  if (valid_ && next[18] != 0 && next[18] < next[17])
    valid_ = false;
  if (valid_ && next[19] != draft_[19]) {
    next[6] = next[19];
    next[7] = next[19];
    updating_ = true;
    ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_VALUE_BASE + 6,
                      std::to_wstring(next[6]).c_str());
    ::SetDlgItemTextW(m_hWnd, IDC_LAYOUT_VALUE_BASE + 7,
                      std::to_wstring(next[7]).c_str());
    updating_ = false;
  }
  if (valid_) {
    draft_ = next;
    candidate_abbreviate_length_ = static_cast<int>(abbreviation_value);
    page_size_ = static_cast<int>(page_size_value);
    wchar_t text[512]{};
    ::GetDlgItemTextW(m_hWnd, IDC_LAYOUT_LABEL_FORMAT, text,
                      static_cast<int>(std::size(text)));
    label_format_ = text;
    ::GetDlgItemTextW(m_hWnd, IDC_LAYOUT_MARK_TEXT, text,
                      static_cast<int>(std::size(text)));
    mark_text_ = text;
  }
  RefreshState();
  if (valid_)
    RefreshPreview();
}

void LayoutEffectsSettingsDialog::ShowGroup() {
  struct Placement {
    HWND window;
    RECT bounds{};
    bool visible = false;
  };
  std::vector<Placement> placements;
  const auto hide = [&](int id) {
    if (HWND control = ::GetDlgItem(m_hWnd, id))
      placements.push_back({control});
  };
  for (int i = 0; i < 6; ++i) {
    hide(IDC_LAYOUT_WINDOW_LABEL_BASE + i);
    hide(IDC_LAYOUT_WINDOW_BOOL_BASE + i);
  }
  for (int i = 0; i < 2; ++i)
    hide(IDC_LAYOUT_PREEDIT_LABEL_BASE + i);
  hide(IDC_LAYOUT_PREEDIT_INLINE);
  hide(IDC_LAYOUT_PREEDIT_TYPE);
  for (int i = 0; i < 10; ++i)
    hide(IDC_LAYOUT_OTHER_LABEL_BASE + i);
  for (int i = 0; i < 5; ++i)
    hide(IDC_LAYOUT_OTHER_BOOL_BASE + i);
  for (int id : {IDC_LAYOUT_LABEL_FORMAT, IDC_LAYOUT_MARK_TEXT,
                 IDC_LAYOUT_ANTIALIAS_MODE, IDC_LAYOUT_ABBREVIATE_LENGTH,
                 IDC_LAYOUT_HOVER_TYPE, 1415, IDC_LAYOUT_PAGE_SIZE, 1413,
                 IDC_LAYOUT_TYPE, 1414, IDC_LAYOUT_ALIGN})
    hide(id);
  for (int i = 0; i < static_cast<int>(kFields.size()); ++i)
    for (int id : {1340 + i, IDC_LAYOUT_VALUE_BASE + i})
      hide(id);

  const int rows = GroupRowCount(active_group_, layout_type_);
  const int max_scroll = (std::max)(0, rows - kVisibleRows);
  int& scroll = group_scroll_[active_group_];
  scroll = std::clamp(scroll, 0, max_scroll);
  const int sidebar_x =
      settings_navigation::MapDialogUnits(
          m_hWnd, 0, 0, settings_navigation::kSidebarWidthDlu, 0)
          .right;
  const auto place = [&](int id, int x, int y, int w, int h) {
    const RECT box = settings_navigation::MapDialogUnits(m_hWnd, x, y, w, h);
    if (HWND control = ::GetDlgItem(m_hWnd, id)) {
      for (auto& placement : placements) {
        if (placement.window != control)
          continue;
        placement.bounds = {box.left + sidebar_x, box.top,
                            box.right + sidebar_x, box.bottom};
        placement.visible = true;
        break;
      }
    }
  };
  const auto place_number = [&](int label, int edit, int y) {
    place(label, 28, y, 174, 14);
    place(edit, 268, y + kInputOffsetY, 48, kInputHeight);
  };

  for (int row = scroll; row < (std::min)(rows, scroll + kVisibleRows); ++row) {
    const int y = kFirstRowY + (row - scroll) * kRowHeight + 2;
    if (active_group_ == 0) {
      place(IDC_LAYOUT_WINDOW_LABEL_BASE + row, 28, y, 220, 14);
      place(IDC_LAYOUT_WINDOW_BOOL_BASE + row, 268, y, 48, 14);
    } else if (active_group_ == 1) {
      place(IDC_LAYOUT_PREEDIT_LABEL_BASE + row, 28, y, 174, 14);
      place(row == 0 ? IDC_LAYOUT_PREEDIT_INLINE : IDC_LAYOUT_PREEDIT_TYPE,
            row == 0 ? 268 : 207, y, row == 0 ? 48 : 109, row == 0 ? 14 : 90);
    } else if (active_group_ == 2) {
      place(IDC_LAYOUT_OTHER_LABEL_BASE + row, 28, y, 174, 14);
      switch (row) {
        case 0:
          place(IDC_LAYOUT_LABEL_FORMAT, 207, y + kInputOffsetY, 109,
                kInputHeight);
          break;
        case 1:
          place(IDC_LAYOUT_MARK_TEXT, 207, y + kInputOffsetY, 109,
                kInputHeight);
          break;
        case 2:
          place(IDC_LAYOUT_OTHER_BOOL_BASE + 0, 268, y, 48, 14);
          break;
        case 3:
          place(IDC_LAYOUT_OTHER_BOOL_BASE + 1, 268, y, 48, 14);
          break;
        case 4:
          place(IDC_LAYOUT_OTHER_BOOL_BASE + 2, 268, y, 48, 14);
          break;
        case 5:
          place(IDC_LAYOUT_ANTIALIAS_MODE, 207, y, 109, 110);
          break;
        case 6:
          place_number(IDC_LAYOUT_OTHER_LABEL_BASE + row,
                       IDC_LAYOUT_ABBREVIATE_LENGTH, y);
          break;
        case 7:
          place(IDC_LAYOUT_HOVER_TYPE, 207, y, 109, 80);
          break;
        case 8:
          place(IDC_LAYOUT_OTHER_BOOL_BASE + 3, 268, y, 48, 14);
          break;
        case 9:
          place(IDC_LAYOUT_OTHER_BOOL_BASE + 4, 268, y, 48, 14);
          break;
      }
    } else if (row == 0) {
      place_number(1415, IDC_LAYOUT_PAGE_SIZE, y);
    } else if (row == 1) {
      place(1413, 28, y, 174, 14);
      place(IDC_LAYOUT_TYPE, 207, y, 109, 100);
    } else if (row == 2) {
      place(1414, 28, y, 174, 14);
      place(IDC_LAYOUT_ALIGN, 207, y, 109, 70);
    } else {
      const int field = kLayoutRowFields[row - 3];
      place_number(1340 + field, IDC_LAYOUT_VALUE_BASE + field, y);
    }
  }
  // Keep overlapping rows visible throughout the move. One deferred update
  // changes only the rows entering/leaving the viewport, without an empty
  // frame.
  HDWP batch = ::BeginDeferWindowPos(static_cast<int>(placements.size()));
  bool deferred = batch != nullptr;
  for (const auto& placement : placements) {
    const bool visible =
        (::GetWindowLongPtrW(placement.window, GWL_STYLE) & WS_VISIBLE) != 0;
    if (!placement.visible && !visible)
      continue;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
    if (!placement.visible)
      flags |= SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE;
    else if (!visible)
      flags |= SWP_SHOWWINDOW;
    const RECT& box = placement.bounds;
    if (deferred) {
      batch =
          ::DeferWindowPos(batch, placement.window, nullptr, box.left, box.top,
                           box.right - box.left, box.bottom - box.top, flags);
      if (!batch) {
        deferred = false;
        break;
      }
    }
  }
  if (deferred)
    deferred = ::EndDeferWindowPos(batch) != FALSE;
  if (!deferred) {
    for (const auto& placement : placements) {
      const RECT& box = placement.bounds;
      ::SetWindowPos(
          placement.window, nullptr, box.left, box.top, box.right - box.left,
          box.bottom - box.top,
          SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW |
              (placement.visible ? SWP_SHOWWINDOW
                                 : SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE));
    }
  }
  if (HWND scrollbar = ::GetDlgItem(m_hWnd, IDC_LAYOUT_SCROLLBAR)) {
    SCROLLINFO info{sizeof(info)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = rows - 1;
    info.nPage = kVisibleRows;
    info.nPos = scroll;
    ::SetScrollInfo(scrollbar, SB_CTL, &info, FALSE);
    const bool visible =
        (::GetWindowLongPtrW(scrollbar, GWL_STYLE) & WS_VISIBLE) != 0;
    if (visible != (max_scroll > 0))
      ::ShowWindow(scrollbar, max_scroll ? SW_SHOWNA : SW_HIDE);
    ::InvalidateRect(scrollbar, nullptr, FALSE);
  }
  for (int index = 0; index < 4; ++index) {
    const int button = IDC_LAYOUT_CATEGORY_WINDOW + index;
    const WPARAM desired = index == active_group_ ? BST_CHECKED : BST_UNCHECKED;
    if (::SendDlgItemMessageW(m_hWnd, button, BM_GETCHECK, 0, 0) != desired)
      ::SendDlgItemMessageW(m_hWnd, button, BM_SETCHECK, desired, 0);
  }
  UpdateDependencies();
  RECT dirty{};
  if (HWND card = ::GetDlgItem(m_hWnd, IDC_LAYOUT_FRAME_CARD)) {
    ::GetWindowRect(card, &dirty);
    ::MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&dirty),
                      2);
    ::InflateRect(&dirty, 1, 1);
    ::RedrawWindow(m_hWnd, &dirty, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  }
}

void LayoutEffectsSettingsDialog::UpdateDependencies() {
  const auto enable = [&](int id, bool enabled) {
    const HWND control = ::GetDlgItem(m_hWnd, id);
    if (control && (::IsWindowEnabled(control) != FALSE) != enabled)
      ::EnableWindow(control, enabled);
  };
  const bool explicit_type = !layout_type_.empty();
  const bool vertical_text =
      explicit_type ? layout_type_ == "vertical_text" : window_values_[2];
  const bool fullscreen =
      explicit_type ? layout_type_.find("fullscreen") != std::string::npos
                    : window_values_[0];
  for (int index : {0, 1, 2})
    enable(IDC_LAYOUT_WINDOW_BOOL_BASE + index,
           !explicit_type && !(window_values_[2] && index != 2));
  for (int index : {3, 4, 5})
    enable(IDC_LAYOUT_WINDOW_BOOL_BASE + index, vertical_text);
  enable(IDC_LAYOUT_PREEDIT_INLINE, !fullscreen);
  enable(IDC_LAYOUT_VALUE_BASE + 9, !fullscreen && !inline_preedit_);
  enable(IDC_LAYOUT_VALUE_BASE + 11, !fullscreen);
  const bool shadow = !fullscreen && draft_[12] > 0;
  enable(IDC_LAYOUT_VALUE_BASE + 13, shadow);
  enable(IDC_LAYOUT_VALUE_BASE + 14, shadow);
}

void LayoutEffectsSettingsDialog::RefreshState() {
  const bool dirty = HasUnappliedChanges();
  settings_navigation::SetUnappliedChanges(
      m_hWnd, settings_navigation::Page::Layout, dirty);
  if (HWND apply = ::GetDlgItem(m_hWnd, IDC_LAYOUT_APPLY))
    ::EnableWindow(apply, valid_ && dirty);
  ::SetDlgItemTextW(
      m_hWnd, IDC_LAYOUT_MESSAGE,
      !valid_ ? LocalText(L"请输入范围内的整数", L"請輸入範圍內的整數",
                          L"Enter a valid number")
                    .c_str()
      : (layout_type_.empty()
             ? inherited_fullscreen_
             : layout_type_.find("fullscreen") != std::string::npos)
          ? LocalText(L"全屏不使用最大宽度和阴影",
                      L"全螢幕不使用最大寬度和陰影",
                      L"Fullscreen ignores max width and shadow")
                .c_str()
      : (layout_type_.empty() ? inherited_vertical_text_
                              : layout_type_ == "vertical_text")
          ? LocalText(L"竖排文本预览以应用后为准", L"直排文字預覽以套用後為準",
                      L"Vertical text preview is illustrative")
                .c_str()
          : L"");
}

void LayoutEffectsSettingsDialog::RefreshPreview() {
  for (WORD id : {1314, 1315})
    if (HWND view = ::GetDlgItem(m_hWnd, id))
      ::InvalidateRect(view, nullptr, FALSE);
}

LRESULT LayoutEffectsSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  // Buffer the page and its native child controls as one paint transaction.
  ::SetWindowLongPtrW(
      m_hWnd, GWL_EXSTYLE,
      ::GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) | WS_EX_COMPOSITED);
  ::SetWindowLongPtrW(m_hWnd, GWL_STYLE,
                      ::GetWindowLongPtrW(m_hWnd, GWL_STYLE) | WS_CLIPCHILDREN);
  CreateControls();
  if (!LoadValues()) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(L"无法读取当前布局配置。", L"無法讀取目前佈局設定。",
                  L"Could not load layout settings.")
            .c_str(),
        L"Weasel", MB_OK | MB_ICONERROR);
    EndDialog(IDCANCEL);
    return settings_navigation::HostedPageInitResult();
  }
  settings_navigation::Install(
      m_hWnd, settings_navigation::Page::Layout,
      {IDC_LAYOUT_APPLY, IDCANCEL, WeaselDisplayUserDataPath().wstring(), {}});
  ShowGroup();
  ::SetWindowPos(::GetDlgItem(m_hWnd, IDC_LAYOUT_FRAME_CARD), HWND_BOTTOM, 0, 0,
                 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  RefreshState();
  CenterWindow();
  return settings_navigation::HostedPageInitResult();
}

bool LayoutEffectsSettingsDialog::PrepareForDisplay() {
  if (!HasUnappliedChanges() && settings_->AppearanceSourcesChanged()) {
    if (!LoadValues())
      return false;
    ShowGroup();
    return true;
  }
  RefreshPreview();
  return true;
}

bool LayoutEffectsSettingsDialog::HasUnappliedChanges() const {
  return draft_ != initial_ || horizontal_ != initial_horizontal_ ||
         layout_type_ != initial_layout_type_ ||
         align_type_ != initial_align_type_ ||
         window_options_ != initial_window_options_ ||
         window_values_ != initial_window_values_ ||
         inline_preedit_ != initial_inline_preedit_ ||
         preedit_type_ != initial_preedit_type_ ||
         label_format_ != initial_label_format_ ||
         mark_text_ != initial_mark_text_ ||
         other_values_ != initial_other_values_ ||
         antialias_mode_ != initial_antialias_mode_ ||
         candidate_abbreviate_length_ != initial_candidate_abbreviate_length_ ||
         page_size_ != initial_page_size_ ||
         hover_type_ != initial_hover_type_ || needs_deploy_ || !valid_;
}

bool LayoutEffectsSettingsDialog::ApplyChanges() {
  ReadValues();
  if (!valid_)
    return false;
  if (!HasUnappliedChanges())
    return true;
  std::vector<std::pair<std::string, int>> values;
  std::vector<std::pair<std::string, int>> expected;
  values.reserve(kFields.size());
  expected.reserve(kFields.size());
  for (size_t index = 0; index < kFields.size(); ++index) {
    values.emplace_back(kFields[index].key, draft_[index]);
    expected.emplace_back(kFields[index].key, initial_[index]);
  }
  const std::vector<std::pair<std::string, int>> style_ints{
      {"candidate_abbreviate_length", candidate_abbreviate_length_}};
  const std::vector<std::pair<std::string, int>> expected_style_ints{
      {"candidate_abbreviate_length", initial_candidate_abbreviate_length_}};
  std::vector<std::pair<std::string, bool>> style_bools{
      {"fullscreen", window_values_[0]},
      {"vertical_text", window_values_[2]},
      {"inline_preedit", inline_preedit_}};
  std::vector<std::pair<std::string, bool>> expected_style_bools{
      {"fullscreen", initial_window_values_[0]},
      {"vertical_text", initial_window_values_[2]},
      {"inline_preedit", initial_inline_preedit_}};
  for (size_t i = 0; i < other_values_.size(); ++i) {
    style_bools.emplace_back(kOtherBoolKeys[i], other_values_[i]);
    expected_style_bools.emplace_back(kOtherBoolKeys[i],
                                      initial_other_values_[i]);
  }
  const std::vector<std::pair<std::string, std::string>> style_strings{
      {"preedit_type", preedit_type_},
      {"label_format", wtou8(label_format_)},
      {"mark_text", wtou8(mark_text_)},
      {"antialias_mode", antialias_mode_},
      {"hover_type", hover_type_}};
  const std::vector<std::pair<std::string, std::string>> expected_style_strings{
      {"preedit_type", initial_preedit_type_},
      {"label_format", wtou8(initial_label_format_)},
      {"mark_text", wtou8(initial_mark_text_)},
      {"antialias_mode", initial_antialias_mode_},
      {"hover_type", initial_hover_type_}};
  if (!needs_deploy_ &&
      !settings_->SaveLayout(
          values, expected, horizontal_, initial_horizontal_, layout_type_,
          initial_layout_type_, align_type_, initial_align_type_,
          window_options_, initial_window_options_, style_ints,
          expected_style_ints, style_bools, expected_style_bools, style_strings,
          expected_style_strings)) {
    ::MessageBoxW(
        m_hWnd,
        LocalText(
            L"无法保存布局设置。请确认配置文件未在其他位置修改。",
            L"無法儲存佈局設定。請確認設定檔未在其他位置修改。",
            L"Could not save layout settings. Check for external changes.")
            .c_str(),
        L"Weasel", MB_OK | MB_ICONERROR);
    return false;
  }
  const bool page_size_changed = page_size_ != initial_page_size_;
  if (page_size_changed && !SavePageSize(page_size_)) {
    ::MessageBoxW(m_hWnd,
                  LocalText(L"无法保存候选词个数。请检查方案配置文件。",
                            L"無法儲存候選詞個數。請檢查方案設定檔。",
                            L"Could not save candidates per page.")
                      .c_str(),
                  L"Weasel", MB_OK | MB_ICONERROR);
    return false;
  }
  needs_deploy_ =
      settings_->configuration_changed() || page_size_changed || needs_deploy_;
  if (needs_deploy_ && Configurator().UpdateWorkspace(true) != 0)
    return false;
  needs_deploy_ = false;
  if (!settings_->LoadAppearance())
    return false;
  if (CurrentPageSize() != page_size_)
    return false;
  for (size_t index = 0; index < kFields.size(); ++index) {
    if (settings_->PreviewLayoutSignedInt(
            kFields[index].key, kFields[index].fallback) != draft_[index])
      return false;
  }
  if (settings_->PreviewStyleBool("horizontal", false) != horizontal_ ||
      settings_->PreviewLayoutString("type", "") != layout_type_ ||
      settings_->PreviewLayoutString("align_type", "center") != align_type_)
    return false;
  for (size_t i = 0; i < window_options_.size(); ++i)
    if (settings_->PreviewStyleBool(kWindowOptions[i], false) !=
        window_options_[i])
      return false;
  if (settings_->PreviewStyleBool("fullscreen", false) != window_values_[0] ||
      settings_->PreviewStyleBool("vertical_text", false) !=
          window_values_[2] ||
      settings_->PreviewStyleBool("inline_preedit", false) != inline_preedit_ ||
      wtou8(settings_->PreviewStyleString("preedit_type", L"composition")) !=
          preedit_type_ ||
      settings_->PreviewStyleString("label_format", L"%s") != label_format_ ||
      settings_->PreviewStyleString("mark_text", L"") != mark_text_ ||
      wtou8(settings_->PreviewStyleString("antialias_mode", L"default")) !=
          antialias_mode_ ||
      settings_->PreviewStyleInt("candidate_abbreviate_length", 0) !=
          candidate_abbreviate_length_ ||
      wtou8(settings_->PreviewStyleString("hover_type", L"none")) !=
          hover_type_)
    return false;
  for (size_t i = 0; i < other_values_.size(); ++i)
    if (settings_->PreviewStyleBool(kOtherBoolKeys[i], false) !=
        other_values_[i])
      return false;
  initial_ = draft_;
  initial_horizontal_ = horizontal_;
  initial_layout_type_ = layout_type_;
  initial_align_type_ = align_type_;
  initial_window_options_ = window_options_;
  initial_window_values_ = window_values_;
  initial_inline_preedit_ = inline_preedit_;
  initial_preedit_type_ = preedit_type_;
  initial_label_format_ = label_format_;
  initial_mark_text_ = mark_text_;
  initial_other_values_ = other_values_;
  initial_antialias_mode_ = antialias_mode_;
  initial_candidate_abbreviate_length_ = candidate_abbreviate_length_;
  initial_page_size_ = page_size_;
  initial_hover_type_ = hover_type_;
  RefreshState();
  RefreshPreview();
  return true;
}

void LayoutEffectsSettingsDialog::DrawPreview(const DRAWITEMSTRUCT& draw,
                                              bool dark) {
  weasel::AppearancePreview preview{};
  preview.dpi = ::GetDeviceCaps(draw.hDC, LOGPIXELSX);
  preview.dark = dark;
  preview.acrylic = weasel::UserSettings::Load().acrylic;
  preview.horizontal =
      EffectiveHorizontal(layout_type_, horizontal_, inherited_vertical_text_);
  preview.vertical_text = layout_type_.empty()
                              ? inherited_vertical_text_
                              : layout_type_ == "vertical_text";
  preview.fullscreen = layout_type_.empty() ? inherited_fullscreen_
                                            : layout_type_.find("fullscreen") !=
                                                  std::string::npos;
  preview.vertical_text_left_to_right = window_values_[3];
  preview.vertical_text_with_wrap = window_values_[4];
  preview.reverse_candidates = preview.vertical_text && window_values_[5];
  preview.inline_preedit = inline_preedit_ && !preview.fullscreen;
  preview.layout_effects_preview = true;
  const auto colors = settings_->ActiveAppearance();
  const std::string scheme = colors[(preview.acrylic ? 0 : 2) + (dark ? 1 : 0)];
  const auto color = [&](const char* key, COLORREF light, COLORREF dim) {
    return settings_->PreviewColor(scheme, key, dark ? dim : light);
  };
  preview.background = color("back_color", RGB(249, 249, 249), RGB(44, 44, 44));
  preview.border = color("border_color", RGB(213, 213, 213), RGB(80, 80, 80));
  preview.text =
      color("candidate_text_color", RGB(32, 32, 32), RGB(242, 242, 242));
  preview.label = color("label_color", RGB(104, 104, 104), RGB(176, 176, 176));
  preview.highlight = color("hilited_candidate_back_color", RGB(229, 229, 229),
                            RGB(66, 66, 66));
  preview.highlighted_text = color("hilited_candidate_text_color",
                                   RGB(17, 17, 17), RGB(255, 255, 255));
  preview.highlighted_label =
      color("hilited_label_color", RGB(0, 103, 192), RGB(96, 205, 255));
  preview.mark =
      color("hilited_mark_color", RGB(0, 103, 192), RGB(96, 205, 255));
  preview.border_width = static_cast<float>(draft_[0]);
  preview.radius = static_cast<float>(draft_[1]);
  preview.margin_x = draft_[2];
  preview.margin_y = draft_[3];
  preview.highlight_radius = static_cast<float>(draft_[4]);
  preview.candidate_spacing = draft_[5];
  preview.hilite_padding_x = draft_[6];
  preview.hilite_padding_y = draft_[7];
  preview.hilite_spacing = draft_[8];
  preview.spacing = draft_[9];
  preview.baseline = draft_[15];
  preview.linespacing = draft_[16];
  preview.min_width = draft_[10];
  preview.max_width = draft_[11];
  preview.min_height = draft_[17];
  preview.max_height = draft_[18];
  preview.align_type = ComboIndex(align_type_, kAlignTypes, 3);
  preview.shadow_radius = draft_[12];
  preview.shadow_offset_x = draft_[13];
  preview.shadow_offset_y = draft_[14];
  preview.shadow_rgba = settings_->PaletteRgba(scheme, 2, dark);
  preview.hover_type = ComboIndex(hover_type_, kHoverTypes, 3);
  preview.text_quality =
      antialias_mode_ == "aliased" ? NONANTIALIASED_QUALITY
      : (antialias_mode_ == "cleartype" || antialias_mode_ == "force_dword")
          ? CLEARTYPE_QUALITY
          : ANTIALIASED_QUALITY;
  preview.font_point = settings_->PreviewStyleInt("font_point", 11);
  preview.label_font_point = settings_->PreviewStyleInt("label_font_point", 9);
  preview.font_face =
      settings_->PreviewStyleString("font_face", L"Microsoft YaHei");
  preview.label_font_face = settings_->PreviewStyleString(
      "label_font_face", preview.font_face.c_str());
  preview.page_background = settings_theme::GetColor(COLOR_BTNFACE);
  preview.title = LocalText(dark ? L"预览 • 深色" : L"预览 • 浅色",
                            dark ? L"預覽 • 深色" : L"預覽 • 淺色",
                            dark ? L"Preview • Dark" : L"Preview • Light");
  preview.preedit = preedit_type_ == "preview" ? L"你好"
                    : preedit_type_ == "preview_all" ? L"你好　拟好　输入方案"
                                                     : L"ni hao";
  preview.mark_text = mark_text_;
  const std::array<std::wstring, 9> sample_candidates{
      L"你好", L"输入方案", L"候选文字", L"配色",    L"小狼毫",
      L"设置", L"字体",     L"布局",     L"语法模型"};
  preview.candidates.resize(page_size_);
  preview.labels.resize(page_size_);
  for (size_t index = 0; index < preview.candidates.size(); ++index) {
    preview.candidates[index] = AbbreviatePreviewCandidate(
        sample_candidates[index], candidate_abbreviate_length_);
    preview.labels[index] =
        FormatPreviewLabel(label_format_, static_cast<int>(index + 1));
  }
  const HFONT font =
      reinterpret_cast<HFONT>(::SendMessageW(m_hWnd, WM_GETFONT, 0, 0));
  weasel::DrawAppearancePreview(draw.hDC, draw.rcItem, font, preview);
}

LRESULT LayoutEffectsSettingsDialog::OnDrawItem(UINT,
                                                WPARAM,
                                                LPARAM parameter,
                                                BOOL& handled) {
  const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(parameter);
  if (!draw) {
    handled = FALSE;
    return 0;
  }
  if (draw->CtlID == IDC_LAYOUT_TYPE || draw->CtlID == IDC_LAYOUT_ALIGN ||
      draw->CtlID == IDC_LAYOUT_PREEDIT_TYPE ||
      draw->CtlID == IDC_LAYOUT_ANTIALIAS_MODE ||
      draw->CtlID == IDC_LAYOUT_HOVER_TYPE) {
    settings_navigation::DrawComboItem(*draw);
    return TRUE;
  }
  if (draw->CtlID == IDC_LAYOUT_FRAME_CARD) {
    settings_navigation::DrawCard(*draw);
    return TRUE;
  }
  if (draw->CtlID == 1314 || draw->CtlID == 1315) {
    DrawPreview(*draw, draw->CtlID == 1315);
    return TRUE;
  }
  handled = FALSE;
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnMeasureItem(UINT,
                                                   WPARAM,
                                                   LPARAM parameter,
                                                   BOOL& handled) {
  auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(parameter);
  if (!measure || (measure->CtlID != IDC_LAYOUT_TYPE &&
                   measure->CtlID != IDC_LAYOUT_ALIGN &&
                   measure->CtlID != IDC_LAYOUT_PREEDIT_TYPE &&
                   measure->CtlID != IDC_LAYOUT_ANTIALIAS_MODE &&
                   measure->CtlID != IDC_LAYOUT_HOVER_TYPE)) {
    handled = FALSE;
    return 0;
  }
  measure->itemHeight = settings_navigation::MeasureComboItemHeight(m_hWnd);
  return TRUE;
}

LRESULT LayoutEffectsSettingsDialog::OnStaticColor(UINT,
                                                   WPARAM dc,
                                                   LPARAM window,
                                                   BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id >= 1310 && id <= 1315) || (id >= 1412 && id <= 1417) ||
      (id >= IDC_LAYOUT_WINDOW_LABEL_BASE &&
       id < IDC_LAYOUT_WINDOW_LABEL_BASE + 6) ||
      (id >= IDC_LAYOUT_PREEDIT_LABEL_BASE &&
       id < IDC_LAYOUT_PREEDIT_LABEL_BASE + 2) ||
      (id >= IDC_LAYOUT_OTHER_LABEL_BASE &&
       id < IDC_LAYOUT_OTHER_LABEL_BASE + 10) ||
      (id >= 1340 && id < 1340 + static_cast<int>(kFields.size())) ||
      id == IDC_LAYOUT_MESSAGE) {
    ::SetBkMode(reinterpret_cast<HDC>(dc), TRANSPARENT);
    ::SetTextColor(reinterpret_cast<HDC>(dc),
                   id == IDC_LAYOUT_MESSAGE
                       ? settings_theme::GetColor(COLOR_GRAYTEXT)
                       : settings_theme::GetColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
  }
  handled = FALSE;
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnEditColor(UINT,
                                                 WPARAM dc,
                                                 LPARAM window,
                                                 BOOL& handled) {
  const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(window));
  if ((id >= IDC_LAYOUT_VALUE_BASE &&
       id < IDC_LAYOUT_VALUE_BASE + static_cast<int>(kFields.size())) ||
      id == IDC_LAYOUT_LABEL_FORMAT || id == IDC_LAYOUT_MARK_TEXT ||
      id == IDC_LAYOUT_ABBREVIATE_LENGTH) {
    ::SetBkColor(reinterpret_cast<HDC>(dc),
                 settings_theme::GetColor(COLOR_WINDOW));
    ::SetTextColor(reinterpret_cast<HDC>(dc),
                   settings_theme::GetColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
  }
  handled = FALSE;
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnValueChanged(WORD code,
                                                    WORD,
                                                    HWND,
                                                    BOOL&) {
  if (code == EN_CHANGE)
    ReadValues();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnGroupChanged(WORD code,
                                                    WORD id,
                                                    HWND,
                                                    BOOL&) {
  if (code == BN_CLICKED) {
    active_group_ = id - IDC_LAYOUT_GROUP_FRAME;
    ShowGroup();
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnCategoryChanged(WORD code,
                                                       WORD id,
                                                       HWND,
                                                       BOOL&) {
  if (code == BN_CLICKED) {
    active_group_ = id - IDC_LAYOUT_CATEGORY_WINDOW;
    ShowGroup();
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnStyleBoolChanged(WORD code,
                                                        WORD id,
                                                        HWND,
                                                        BOOL&) {
  if (code != BN_CLICKED || updating_)
    return 0;
  if (id >= IDC_LAYOUT_WINDOW_BOOL_BASE &&
      id < IDC_LAYOUT_WINDOW_BOOL_BASE + 6) {
    const int index = id - IDC_LAYOUT_WINDOW_BOOL_BASE;
    window_values_[index] = ::IsDlgButtonChecked(m_hWnd, id) == BST_CHECKED;
    horizontal_ = window_values_[1];
    inherited_vertical_text_ = window_values_[2];
    inherited_fullscreen_ = window_values_[0];
    for (int i = 0; i < 3; ++i)
      window_options_[i] = window_values_[i + 3];
  } else if (id == IDC_LAYOUT_PREEDIT_INLINE) {
    inline_preedit_ = ::IsDlgButtonChecked(m_hWnd, id) == BST_CHECKED;
  } else if (id >= IDC_LAYOUT_OTHER_BOOL_BASE &&
             id < IDC_LAYOUT_OTHER_BOOL_BASE + 5) {
    other_values_[id - IDC_LAYOUT_OTHER_BOOL_BASE] =
        ::IsDlgButtonChecked(m_hWnd, id) == BST_CHECKED;
  }
  UpdateDependencies();
  RefreshState();
  RefreshPreview();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnStyleEnumChanged(WORD code,
                                                        WORD id,
                                                        HWND,
                                                        BOOL&) {
  if (code != CBN_SELCHANGE || updating_)
    return 0;
  const int selected =
      static_cast<int>(::SendDlgItemMessageW(m_hWnd, id, CB_GETCURSEL, 0, 0));
  if (id == IDC_LAYOUT_PREEDIT_TYPE && selected >= 0 && selected < 3)
    preedit_type_ = kPreeditTypes[selected];
  else if (id == IDC_LAYOUT_ANTIALIAS_MODE && selected >= 0 && selected < 5)
    antialias_mode_ = kAntialiasModes[selected];
  else if (id == IDC_LAYOUT_HOVER_TYPE && selected >= 0 && selected < 3)
    hover_type_ = kHoverTypes[selected];
  RefreshState();
  RefreshPreview();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnStyleTextChanged(WORD code,
                                                        WORD,
                                                        HWND,
                                                        BOOL&) {
  if (code == EN_CHANGE)
    ReadValues();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnRestore(WORD, WORD, HWND, BOOL&) {
  for (size_t index = 0; index < kFields.size(); ++index)
    draft_[index] = kFields[index].fallback;
  window_values_ = {};
  horizontal_ = false;
  inherited_fullscreen_ = false;
  inherited_vertical_text_ = false;
  layout_type_.clear();
  align_type_ = "center";
  window_options_ = {};
  inline_preedit_ = false;
  preedit_type_ = "composition";
  label_format_ = L"%s";
  mark_text_.clear();
  other_values_ = {};
  antialias_mode_ = "default";
  candidate_abbreviate_length_ = 0;
  page_size_ = 6;
  hover_type_ = "none";
  valid_ = true;
  ShowValues();
  ShowGroup();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnApply(WORD, WORD, HWND, BOOL&) {
  if (!settings_navigation::RequestApply(m_hWnd))
    ApplyChanges();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnPreviewMode(WORD, WORD id, HWND, BOOL&) {
  horizontal_ = id == IDC_LAYOUT_PREVIEW_HORIZONTAL;
  RefreshState();
  RefreshPreview();
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnLayoutType(WORD code,
                                                  WORD,
                                                  HWND,
                                                  BOOL&) {
  if (code == CBN_SELCHANGE) {
    const int selected = static_cast<int>(
        ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_TYPE, CB_GETCURSEL, 0, 0));
    if (selected >= 0 && selected < 6) {
      layout_type_ = kLayoutTypes[selected];
      UpdateDependencies();
      RefreshState();
      RefreshPreview();
    }
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnAlignType(WORD code, WORD, HWND, BOOL&) {
  if (code == CBN_SELCHANGE) {
    const int selected = static_cast<int>(
        ::SendDlgItemMessageW(m_hWnd, IDC_LAYOUT_ALIGN, CB_GETCURSEL, 0, 0));
    if (selected >= 0 && selected < 3) {
      align_type_ = kAlignTypes[selected];
      RefreshState();
      RefreshPreview();
    }
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnVScroll(UINT,
                                               WPARAM command,
                                               LPARAM source,
                                               BOOL& handled) {
  const HWND scrollbar = ::GetDlgItem(m_hWnd, IDC_LAYOUT_SCROLLBAR);
  if (reinterpret_cast<HWND>(source) != scrollbar) {
    handled = FALSE;
    return 0;
  }
  int next = group_scroll_[active_group_];
  switch (LOWORD(command)) {
    case SB_LINEUP:
      --next;
      break;
    case SB_LINEDOWN:
      ++next;
      break;
    case SB_PAGEUP:
      next -= kVisibleRows;
      break;
    case SB_PAGEDOWN:
      next += kVisibleRows;
      break;
    case SB_TOP:
      next = 0;
      break;
    case SB_BOTTOM:
      next = GroupRowCount(active_group_, layout_type_) - kVisibleRows;
      break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
      SCROLLINFO info{sizeof(info)};
      info.fMask = SIF_TRACKPOS;
      ::GetScrollInfo(scrollbar, SB_CTL, &info);
      next = info.nTrackPos;
      break;
    }
    default:
      return 0;
  }
  next = std::clamp(
      next, 0,
      (std::max)(0, GroupRowCount(active_group_, layout_type_) - kVisibleRows));
  if (next != group_scroll_[active_group_]) {
    group_scroll_[active_group_] = next;
    ShowGroup();
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnMouseWheel(UINT,
                                                  WPARAM wheel,
                                                  LPARAM position,
                                                  BOOL& handled) {
  RECT card{};
  ::GetWindowRect(::GetDlgItem(m_hWnd, IDC_LAYOUT_FRAME_CARD), &card);
  const POINT point{static_cast<short>(LOWORD(position)),
                    static_cast<short>(HIWORD(position))};
  const int rows = GroupRowCount(active_group_, layout_type_);
  if (!::PtInRect(&card, point) || rows <= kVisibleRows) {
    handled = FALSE;
    return 0;
  }
  wheel_delta_ += static_cast<short>(HIWORD(wheel));
  const int steps = wheel_delta_ / WHEEL_DELTA;
  wheel_delta_ %= WHEEL_DELTA;
  if (steps) {
    const int next = std::clamp(group_scroll_[active_group_] - steps, 0,
                                rows - kVisibleRows);
    if (next != group_scroll_[active_group_]) {
      group_scroll_[active_group_] = next;
      ShowGroup();
    }
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnWindowOption(WORD code,
                                                    WORD id,
                                                    HWND,
                                                    BOOL&) {
  if (code == BN_CLICKED) {
    for (size_t i = 0; i < window_options_.size(); ++i)
      if (id == kWindowOptionIds[i])
        window_options_[i] = ::IsDlgButtonChecked(m_hWnd, id) == BST_CHECKED;
    RefreshState();
    RefreshPreview();
  }
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  if (!settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) {
  if (!settings_navigation::RequestClose(m_hWnd, IDCANCEL))
    EndDialog(IDCANCEL);
  return 0;
}

LRESULT LayoutEffectsSettingsDialog::OnNavigate(WORD, WORD id, HWND, BOOL&) {
  if (settings_navigation::PageFromCommand(id) ==
      settings_navigation::Page::Layout)
    return 0;
  if (!settings_navigation::RequestNavigate(m_hWnd, id))
    EndDialog(id);
  return 0;
}
