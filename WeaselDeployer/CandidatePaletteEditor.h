#pragma once

#include "CandidatePalette.h"
#include "CandidatePaletteExport.h"
#include "SettingsColor.h"
#include "SettingsColorPicker.h"
#include "SettingsNavigation.h"
#include "PaletteDisplayOrder.h"
#include "UIStyleSettings.h"
#include "resource.h"
#include <WeaselUtility.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <vector>

// Controls for the new custom-colors tab only. The released scheme selector
// remains owned by UIStyleSettingsDialog and is not rebuilt here.
class CandidatePaletteEditor {
 public:
  static constexpr int kName = 50100, kLight = 50101, kDark = 50102,
                       kList = 50103, kModel = 50104, kCode = 50105,
                       kSwatch = 50106, kPick = 50107, kReset = 50108,
                       kSource = 50110, kCategory = 50120,
                       kChannelLabel = 50130, kChannelLetter = 50170,
                       kChannelSlider = 50140, kChannelNumber = 50150,
                       kAlphaLabel = 50160, kAlphaSlider = 50161,
                       kAlphaNumber = 50162, kAlphaPercent = 50163;

  HWND window() const { return window_; }
  bool editing_dark() const { return dark_; }
  bool picker_visible() const {
    return popup_host_ && ::IsWindowVisible(popup_host_);
  }
  bool screen_picking() const { return picking_; }
  void CancelScreenPick() {
    const bool was_picking = picking_;
    picking_ = false;
    pending_pick_.reset();
    pick_preview_.reset();
    HWND overlay = screen_overlay_;
    screen_overlay_ = nullptr;
    if (overlay && ::IsWindow(overlay))
      ::DestroyWindow(overlay);
    if (was_picking)
      ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
    if (window_ && ::IsWindow(window_))
      ::InvalidateRect(::GetDlgItem(window_, kSwatch), nullptr, FALSE);
  }
  void HidePicker() {
    if (popup_host_) {
      ::KillTimer(popup_host_, 1);
      ::ShowWindow(popup_host_, SW_HIDE);
    }
  }
  const candidate_palette::Draft& draft() const { return draft_; }
  const std::vector<std::string>& deleted_schemes() const {
    return deleted_schemes_;
  }
  const std::vector<std::string>& deleted_groups() const {
    return deleted_groups_;
  }
  const candidate_palette::ImportPlan& imported() const {
    return pending_import_;
  }
  bool colors_changed() const {
    return std::any_of(draft_.changed.begin(), draft_.changed.end(),
                       [](bool changed) { return changed; });
  }
  std::string Selection(size_t slot) const {
    return slot < 4 && draft_.changed[slot] ? "custom:" + draft_.ids[slot]
                                            : selections_[slot];
  }
  bool changed() const {
    return colors_changed() || !deleted_schemes_.empty() ||
           !pending_import_.schemes.empty();
  }
  static bool EditorOwned(const std::string& selection, size_t slot) {
    if (weasel::PaletteSource(selection) != "custom")
      return false;
    const auto id = weasel::PaletteId(selection);
    const std::string tail = std::string(slot < 2 ? "_acrylic" : "_normal") +
                             (slot % 2 ? "_dark" : "_light");
    const std::string single_tail =
        std::string(slot < 2 ? "_acrylic" : "_normal") + "_single";
    return id.rfind("weasel_user_", 0) == 0 &&
           ((id.size() > tail.size() &&
             id.compare(id.size() - tail.size(), tail.size(), tail) == 0) ||
            (id.size() > single_tail.size() &&
             id.compare(id.size() - single_tail.size(), single_tail.size(),
                        single_tail) == 0));
  }
  void Create(HWND parent,
              UIStyleSettings* settings,
              HFONT font,
              const RECT& bounds,
              std::function<void(size_t)> changed,
              std::function<void(size_t, int)> selected,
              std::function<void()> source_switched) {
    settings_ = settings;
    font_ = font;
    changed_ = std::move(changed);
    selected_ = std::move(selected);
    source_switched_ = std::move(source_switched);
    HDC dc = ::GetDC(parent);
    dpi_ = dc ? ::GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
      ::ReleaseDC(parent, dc);
    WNDCLASSW type{};
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.lpfnWndProc = Proc;
    type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = L"Weasel.CandidatePaletteEditor";
    ::RegisterClassW(&type);
    window_ = ::CreateWindowExW(
        WS_EX_CONTROLPARENT, type.lpszClassName, L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, parent, nullptr,
        type.hInstance, this);
    Init();
  }
  void Load(const std::array<std::string, 4>& selections,
            bool force_new = false) {
    selections_ = selections;
    base_selections_ = selections;
    const auto stamp = std::to_string(::GetTickCount64()) + "_" +
                       std::to_string(::GetCurrentProcessId());
    const std::string base = "weasel_user_" + stamp;
    draft_ = {};
    draft_.name = L"我的配色";
    draft_.sources = selections;
    for (size_t i = 0; i < 4; ++i) {
      const std::string suffix = i % 2 ? "_dark" : "_light";
      draft_.ids[i] = base + (i < 2 ? "_acrylic" : "_normal") + suffix;
      if (!force_new && EditorOwned(selections[i], i))
        draft_.ids[i] = weasel::PaletteId(selections[i]);
      for (size_t role = 0; role < candidate_palette::kRoles.size(); ++role)
        draft_.colors[i][role] =
            settings_->PaletteRgba(selections[i], role, i % 2 != 0);
    }
    for (const auto& group : settings_->groups()) {
      if (group.custom && group.light == selections[material_ * 2] &&
          group.dark == selections[material_ * 2 + 1]) {
        draft_.name = u8tow(group.name);
        if (material_ == 1) {
          const std::wstring suffix = L" · 普通";
          if (draft_.name.size() > suffix.size() &&
              draft_.name.compare(draft_.name.size() - suffix.size(),
                                  suffix.size(), suffix) == 0)
            draft_.name.resize(draft_.name.size() - suffix.size());
        }
        break;
      }
    }
    if (draft_.name == L"我的配色") {
      for (const auto& scheme : settings_->schemes()) {
        if (scheme.custom && scheme.color_scheme_id ==
                                 selections[material_ * 2 + (dark_ ? 1 : 0)]) {
          draft_.name = u8tow(scheme.name);
          const auto suffix =
              dark_ ? std::wstring(L" · 深色") : std::wstring(L" · 浅色");
          if (draft_.name.size() > suffix.size() &&
              draft_.name.compare(draft_.name.size() - suffix.size(),
                                  suffix.size(), suffix) == 0)
            draft_.name.resize(draft_.name.size() - suffix.size());
          break;
        }
      }
    }
    saved_name_ = draft_.name;
    name_slot_ = Slot();
    saved_colors_ = draft_.colors;
    for (size_t material = 0; material < 2; ++material) {
      paired_material_[material] =
          std::any_of(settings_->groups().begin(), settings_->groups().end(),
                      [&](const weasel::PaletteGroup& group) {
                        return group.light == selections[material * 2] &&
                               group.dark == selections[material * 2 + 1];
                      });
      source_themes_[material] = weasel::PaletteTheme::Unspecified;
      new_sources_[material] = false;
      const auto& selected = selections[material * 2 + (dark_ ? 1 : 0)];
      for (const auto& scheme : settings_->schemes()) {
        if (scheme.color_scheme_id == selected) {
          source_themes_[material] = scheme.theme;
          break;
        }
      }
    }
    syncing_ = true;
    ::SetWindowTextW(::GetDlgItem(window_, kName), draft_.name.c_str());
    FillSources();
    syncing_ = false;
    Refresh();
  }
  void SetMaterial(bool acrylic) {
    material_ = acrylic ? 0 : 1;
    if (staged_preview_ && pending_import_.acrylic != acrylic) {
      staged_preview_.reset();
      Load(base_selections_);
    }
    FillSources();
    Refresh();
  }
  void SetTheme(bool dark) {
    dark_ = dark;
    if (new_sources_[material_])
      source_themes_[material_] =
          dark ? weasel::PaletteTheme::Dark : weasel::PaletteTheme::Light;
    else if (staged_preview_)
      source_themes_[material_] = staged_preview_->theme;
    else {
      source_themes_[material_] = weasel::PaletteTheme::Unspecified;
      for (const auto& scheme : settings_->schemes()) {
        if (scheme.color_scheme_id == selections_[Slot()]) {
          source_themes_[material_] = scheme.theme;
          break;
        }
      }
    }
    Refresh();
  }
  bool NameAffects(size_t slot) const {
    return slot == name_slot_ ||
           (slot / 2 == name_slot_ / 2 && paired_material_[name_slot_ / 2]);
  }
  void ResetAll() {
    if (staged_preview_) {
      ::MessageBoxW(window_, L"待导入方案正在预览。应用导入后即可编辑颜色。",
                    L"预览待导入方案", MB_OK | MB_ICONINFORMATION);
      return;
    }
    for (size_t slot = 0; slot < 4; ++slot) {
      for (size_t role = 0; role < candidate_palette::kRoles.size(); ++role) {
        const auto& definition = candidate_palette::kRoles[role];
        draft_.colors[slot][role] =
            slot % 2 ? definition.dark : definition.light;
      }
      draft_.changed[slot] = draft_.colors[slot] != saved_colors_[slot] ||
                             (NameAffects(slot) && draft_.name != saved_name_);
      for (size_t role = 0; role < candidate_palette::kRoles.size(); ++role)
        draft_.edited[slot][role] =
            draft_.colors[slot][role] != saved_colors_[slot][role];
      if (changed_)
        changed_(slot);
    }
    Refresh();
  }
  void Discard(size_t slot) {
    if (slot >= 4)
      return;
    draft_.colors[slot] = saved_colors_[slot];
    draft_.changed[slot] = false;
    draft_.edited[slot].fill(false);
    if (slot == name_slot_) {
      draft_.name = saved_name_;
      syncing_ = true;
      ::SetWindowTextW(::GetDlgItem(window_, kName), draft_.name.c_str());
      syncing_ = false;
    }
    Refresh();
  }
  void DiscardAll() {
    deleted_schemes_.clear();
    deleted_groups_.clear();
    pending_import_ = {};
    staged_preview_.reset();
    for (size_t slot = 0; slot < 4; ++slot)
      Discard(slot);
    FillSources();
  }
  bool ValidName(std::wstring* error = nullptr) const {
    auto name = Normalize(draft_.name);
    if (name.empty()) {
      if (error)
        *error = L"请输入方案名称";
      return false;
    }
    for (const auto& group : settings_->groups()) {
      const bool own_group =
          group.custom && ((group.light == "custom:" + draft_.ids[0] &&
                            group.dark == "custom:" + draft_.ids[1]) ||
                           (group.light == "custom:" + draft_.ids[2] &&
                            group.dark == "custom:" + draft_.ids[3]));
      if (!own_group && Normalize(u8tow(group.name)) == name) {
        if (error)
          *error = L"方案名称已存在，请换一个";
        return false;
      }
    }
    for (const auto& scheme : settings_->schemes()) {
      const bool own_scheme = std::any_of(
          draft_.ids.begin(), draft_.ids.end(), [&](const std::string& id) {
            return scheme.color_scheme_id == "custom:" + id;
          });
      if (!own_scheme && Normalize(u8tow(scheme.name)) == name) {
        if (error)
          *error = L"方案名称已存在，请换一个";
        return false;
      }
    }
    return true;
  }
  void Saved(const std::array<std::string, 4>& selections) {
    deleted_schemes_.clear();
    deleted_groups_.clear();
    pending_import_ = {};
    staged_preview_.reset();
    Load(selections);
  }

 private:
  HWND window_ = nullptr, list_ = nullptr, warning_tip_ = nullptr,
       popup_ = nullptr, popup_host_ = nullptr;
  int list_width_ = 0;
  UIStyleSettings* settings_ = nullptr;
  HFONT font_ = nullptr;
  UINT dpi_ = 96;
  bool updating_from_picker_ = false;
  candidate_palette::Draft draft_;
  std::array<candidate_palette::Colors, 4> saved_colors_{};
  std::array<std::string, 4> selections_{};
  std::array<std::string, 4> base_selections_{};
  std::wstring saved_name_;
  size_t name_slot_ = 0;
  std::array<bool, 2> paired_material_{};
  std::array<bool, 2> new_sources_{};
  std::array<weasel::PaletteTheme, 2> source_themes_{};
  std::vector<std::string> deleted_schemes_;
  std::vector<std::string> deleted_groups_;
  candidate_palette::ImportPlan pending_import_;
  settings_color::Picker picker_;
  std::function<void(size_t)> changed_;
  std::function<void(size_t, int)> selected_;
  std::function<void()> source_switched_;
  struct SourceItem {
    std::wstring label;
    std::wstring name;
    std::wstring detail;
    std::string light;
    std::string dark;
    bool paired = false;
    bool own = false;
    bool removal = false;
    bool importing = false;
    bool staged = false;
    bool cancel_import = false;
    weasel::PaletteTheme theme = weasel::PaletteTheme::Unspecified;
    bool exporting = false;
  };
  std::vector<SourceItem> source_items_;
  std::optional<SourceItem> removal_target_;
  std::optional<SourceItem> staged_preview_;
  HWND source_tip_ = nullptr;
  std::wstring source_tip_text_;
  std::wstring source_list_tip_text_;
  int source_list_hover_index_ = -1;
  void UpdateSourcePresentation(HWND combo, int selected) {
    if (!combo)
      return;
    if (source_tip_ && selected >= 0 &&
        static_cast<size_t>(selected) < source_items_.size()) {
      source_tip_text_ = source_items_[selected].label;
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
      tool.hwnd = window_;
      tool.uId = reinterpret_cast<UINT_PTR>(combo);
      tool.lpszText = source_tip_text_.data();
      ::SendMessageW(source_tip_, TTM_UPDATETIPTEXTW, 0,
                     reinterpret_cast<LPARAM>(&tool));
    }
  }
  bool SourceNameTaken(const std::wstring& name) const {
    for (const auto& group : settings_->groups())
      if (Normalize(u8tow(group.name)) == Normalize(name))
        return true;
    for (const auto& scheme : settings_->schemes())
      if (Normalize(u8tow(scheme.name)) == Normalize(name))
        return true;
    return false;
  }
  std::wstring CopyName(std::wstring original) const {
    if (original.empty())
      original = L"我的配色";
    if (!SourceNameTaken(original))
      return original;
    const auto base = original + L" 副本";
    if (!SourceNameTaken(base))
      return base;
    for (int suffix = 2; suffix < 10000; ++suffix) {
      const auto candidate = base + L" " + std::to_wstring(suffix);
      if (!SourceNameTaken(candidate))
        return candidate;
    }
    return base;
  }
  void FillSources() {
    if (!window_ || !::GetDlgItem(window_, kSource) || !settings_)
      return;
    HWND combo = ::GetDlgItem(window_, kSource);
    const bool old_sync = syncing_;
    syncing_ = true;
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    source_items_.clear();
    source_items_.push_back(
        {L"新建配色方案", L"新建配色方案", L"", {}, {}, false, false});
    source_items_.push_back({L"从 TXT 或 YAML 文件识别并导入配色方案",
                             L"导入配色文件…",
                             L"",
                             {},
                             {},
                             false,
                             false,
                             false,
                             true});
    SourceItem export_action{};
    export_action.label = L"将当前编辑的配色导出为可重新导入的 YAML 文件";
    export_action.name = L"导出当前配色…";
    export_action.exporting = true;
    source_items_.push_back(std::move(export_action));
    if (!pending_import_.schemes.empty()) {
      source_items_.push_back({L"取消所有待导入方案",
                               L"取消待导入方案…",
                               L"",
                               {},
                               {},
                               false,
                               false,
                               false,
                               false,
                               false,
                               true});
    }
    const size_t staged_begin = source_items_.size();
    if (!pending_import_.schemes.empty() &&
        pending_import_.acrylic == (material_ == 0)) {
      std::set<std::string> grouped;
      for (const auto& group : pending_import_.groups) {
        const std::wstring name = u8tow(group.name);
        source_items_.push_back(
            {L"待导入 · " + name + L" · 成组浅色/深色", name, L"待导入 · 预览",
             "custom:" + group.light_id, "custom:" + group.dark_id, true, false,
             false, false, true});
        grouped.insert(group.light_id);
        grouped.insert(group.dark_id);
      }
      for (const auto& scheme : pending_import_.schemes) {
        if (grouped.count(scheme.target_id))
          continue;
        const std::wstring name = u8tow(scheme.name);
        source_items_.push_back(
            {L"待导入 · " + name +
                 (scheme.unspecified ? L" · 单独未限定浅深"
                  : scheme.dark      ? L" · 单独深色"
                                     : L" · 单独浅色"),
             name, L"待导入 · 预览",
             scheme.dark ? std::string{} : "custom:" + scheme.target_id,
             scheme.dark ? "custom:" + scheme.target_id : std::string{}, false,
             false, false, false, true, false,
             scheme.unspecified ? weasel::PaletteTheme::Unspecified
             : scheme.dark      ? weasel::PaletteTheme::Dark
                                : weasel::PaletteTheme::Light});
      }
    }
    const auto source_id = [](const SourceItem& item) -> const std::string& {
      return item.light.empty() ? item.dark : item.light;
    };
    const auto by_name = [&](const SourceItem& left, const SourceItem& right) {
      return palette_display::NameLess(left.name, source_id(left), right.name,
                                       source_id(right));
    };
    std::stable_sort(source_items_.begin() + staged_begin, source_items_.end(),
                     [&](const SourceItem& left, const SourceItem& right) {
                       if (left.paired != right.paired)
                         return left.paired;
                       return by_name(left, right);
                     });
    const size_t persisted_begin = source_items_.size();
    for (const auto& group : settings_->groups()) {
      if (group.light.empty() || group.dark.empty())
        continue;
      if (std::find(deleted_schemes_.begin(), deleted_schemes_.end(),
                    weasel::PaletteId(group.light)) != deleted_schemes_.end() ||
          std::find(deleted_schemes_.begin(), deleted_schemes_.end(),
                    weasel::PaletteId(group.dark)) != deleted_schemes_.end())
        continue;
      const bool own = group.custom &&
                       EditorOwned(group.light, material_ * 2) &&
                       EditorOwned(group.dark, material_ * 2 + 1);
      const std::wstring name = u8tow(group.name);
      const std::wstring kind = group.custom ? L"自定义" : L"内置";
      const std::wstring action = own ? L"更新" : L"仅可另存";
      const std::wstring detail = kind + L" · " + action;
      source_items_.push_back({kind + L" · " + name + L" · 成组 · " + action,
                               name, detail, group.light, group.dark, true,
                               own});
    }
    for (const auto& scheme : settings_->schemes()) {
      if (std::find(deleted_schemes_.begin(), deleted_schemes_.end(),
                    weasel::PaletteId(scheme.color_scheme_id)) !=
          deleted_schemes_.end())
        continue;
      bool grouped = false;
      for (const auto& group : settings_->groups()) {
        if (group.light == scheme.color_scheme_id ||
            group.dark == scheme.color_scheme_id) {
          grouped = true;
          break;
        }
      }
      if (grouped)
        continue;
      const bool own =
          scheme.custom &&
          EditorOwned(
              scheme.color_scheme_id,
              material_ * 2 + (scheme.theme == weasel::PaletteTheme::Dark));
      const std::wstring name = u8tow(scheme.name);
      const std::wstring kind = scheme.custom ? L"自定义" : L"内置";
      const std::wstring action = own ? L"更新" : L"仅可另存";
      const std::wstring detail = kind + L" · " + action;
      const wchar_t* availability = scheme.theme == weasel::PaletteTheme::Light
                                        ? L"单独浅色"
                                    : scheme.theme == weasel::PaletteTheme::Dark
                                        ? L"单独深色"
                                        : L"单独未明确浅深（可分别选用）";
      source_items_.push_back(
          {kind + L" · " + name + L" · " + availability + L" · " + action, name,
           detail,
           scheme.theme == weasel::PaletteTheme::Dark ? std::string{}
                                                      : scheme.color_scheme_id,
           scheme.theme == weasel::PaletteTheme::Dark ? scheme.color_scheme_id
                                                      : std::string{},
           false, own, false, false, false, false, scheme.theme});
    }
    std::stable_sort(source_items_.begin() + persisted_begin,
                     source_items_.end(),
                     [&](const SourceItem& left, const SourceItem& right) {
                       const bool left_custom =
                           weasel::PaletteSource(source_id(left)) == "custom";
                       const bool right_custom =
                           weasel::PaletteSource(source_id(right)) == "custom";
                       if (left_custom != right_custom)
                         return !left_custom;
                       if (left.paired != right.paired)
                         return left.paired;
                       return by_name(left, right);
                     });
    int selected = 0;
    const size_t first = material_ * 2;
    for (size_t i = 0; i < source_items_.size(); ++i) {
      const auto& item = source_items_[i];
      if (item.paired && item.light == selections_[first] &&
          item.dark == selections_[first + 1]) {
        selected = static_cast<int>(i);
        break;
      }
      if (!item.paired && i &&
          ((!item.light.empty() && item.light == selections_[first]) ||
           (!item.dark.empty() && item.dark == selections_[first + 1])))
        selected = static_cast<int>(i);
    }
    if (staged_preview_) {
      for (size_t i = 0; i < source_items_.size(); ++i) {
        const auto& item = source_items_[i];
        if (item.staged && item.light == staged_preview_->light &&
            item.dark == staged_preview_->dark) {
          selected = static_cast<int>(i);
          break;
        }
      }
    }
    removal_target_.reset();
    if (pending_import_.schemes.empty() && selected > 2 &&
        source_items_[selected].own) {
      removal_target_ = source_items_[selected];
      source_items_.insert(source_items_.begin() + 3,
                           {L"删除当前自定义方案（应用后生效）",
                            L"删除当前方案…",
                            L"",
                            {},
                            {},
                            false,
                            false,
                            true});
      ++selected;
    }
    for (const auto& item : source_items_) {
      const std::wstring text =
          item.staged ? L"待导入 · " + item.name : item.name;
      ::SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text.c_str()));
    }
    ::SendMessageW(combo, CB_SETCURSEL, selected, 0);
    UpdateSourcePresentation(combo, selected);
    syncing_ = old_sync;
  }
  void SwitchSource(int selected) {
    if (selected < 0 || static_cast<size_t>(selected) >= source_items_.size())
      return;
    if (source_items_[selected].importing) {
      ImportFromText();
      FillSources();
      return;
    }
    if (source_items_[selected].exporting) {
      ExportCurrent();
      FillSources();
      return;
    }
    if (source_items_[selected].cancel_import) {
      if (::MessageBoxW(window_, L"取消本次所有待导入方案？", L"取消导入",
                        MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES) {
        const auto base = base_selections_;
        pending_import_ = {};
        staged_preview_.reset();
        Load(base);
        if (source_switched_)
          source_switched_();
      }
      FillSources();
      return;
    }
    if (source_items_[selected].staged) {
      const SourceItem item = source_items_[selected];
      PreviewStaged(item);
      return;
    }
    if (source_items_[selected].removal) {
      if (removal_target_) {
        const SourceItem target = *removal_target_;
        DeleteSelectedSource(target);
      }
      FillSources();
      return;
    }
    if (!deleted_schemes_.empty()) {
      ::MessageBoxW(window_, L"请先应用或撤销待删除的配色方案，再切换方案。",
                    L"删除配色方案", MB_OK | MB_ICONINFORMATION);
      FillSources();
      return;
    }
    if (!pending_import_.schemes.empty()) {
      ::MessageBoxW(window_, L"请先应用或撤销待导入的配色方案，再切换方案。",
                    L"导入配色方案", MB_OK | MB_ICONINFORMATION);
      FillSources();
      return;
    }
    if (colors_changed() &&
        ::MessageBoxW(window_,
                      L"切换配色方案会放弃尚未应用的自定义颜色修改。是否继续？",
                      L"切换配色方案", MB_YESNO | MB_ICONQUESTION) != IDYES) {
      FillSources();
      return;
    }
    const SourceItem item = source_items_[selected];
    const auto base = base_selections_;
    auto selections = base;
    const size_t first = material_ * 2;
    if (selected != 0) {
      if (item.paired) {
        selections[first] = item.light;
        selections[first + 1] = item.dark;
      } else if (!item.light.empty()) {
        selections[first] = item.light;
        dark_ = false;
      } else {
        selections[first + 1] = item.dark;
        dark_ = true;
      }
    }
    Load(selections, selected == 0 || !item.own);
    base_selections_ = base;
    paired_material_[material_] = item.paired;
    new_sources_[material_] = selected == 0;
    source_themes_[material_] =
        selected == 0
            ? (dark_ ? weasel::PaletteTheme::Dark : weasel::PaletteTheme::Light)
            : item.theme;
    if (selected == 0 || !item.own) {
      std::wstring source_name;
      if (selected != 0) {
        source_name = item.name;
        if (!item.paired) {
          for (const std::wstring suffix : {L" · 浅色", L" · 深色"}) {
            if (source_name.size() > suffix.size() &&
                source_name.compare(source_name.size() - suffix.size(),
                                    suffix.size(), suffix) == 0) {
              source_name.resize(source_name.size() - suffix.size());
              break;
            }
          }
        }
      }
      draft_.name = CopyName(source_name);
      saved_name_ = draft_.name;
      syncing_ = true;
      ::SetWindowTextW(::GetDlgItem(window_, kName), draft_.name.c_str());
      syncing_ = false;
    }
    ::SendDlgItemMessageW(window_, kSource, CB_SETCURSEL, selected, 0);
    UpdateSourcePresentation(::GetDlgItem(window_, kSource), selected);
    if (source_switched_)
      source_switched_();
    Refresh();
  }
  std::string BuiltinFallback(size_t slot) const {
    const bool dark = slot % 2 != 0;
    const std::string preferred = dark ? "Fluent_dark" : "Fluent_light";
    for (const auto& scheme : settings_->schemes())
      if (!scheme.custom && scheme.color_scheme_id == "base:" + preferred)
        return scheme.color_scheme_id;
    for (const auto& scheme : settings_->schemes())
      if (!scheme.custom &&
          scheme.theme ==
              (dark ? weasel::PaletteTheme::Dark : weasel::PaletteTheme::Light))
        return scheme.color_scheme_id;
    for (const auto& scheme : settings_->schemes())
      if (!scheme.custom && scheme.theme == weasel::PaletteTheme::Unspecified)
        return scheme.color_scheme_id;
    return {};
  }
  void PreviewStaged(const SourceItem& item) {
    const auto base = base_selections_;
    staged_preview_.reset();
    Load(base);
    staged_preview_ = item;
    for (const auto& scheme : pending_import_.schemes) {
      const std::string selection = "custom:" + scheme.target_id;
      if (selection != item.light && selection != item.dark)
        continue;
      const size_t slot = material_ * 2 + (scheme.dark ? 1 : 0);
      draft_.colors[slot] = scheme.colors;
      saved_colors_[slot] = scheme.colors;
      draft_.changed[slot] = false;
      draft_.edited[slot].fill(false);
    }
    if (!item.paired)
      dark_ = !item.dark.empty();
    paired_material_[material_] = item.paired;
    new_sources_[material_] = false;
    source_themes_[material_] = item.theme;
    draft_.name = item.name;
    saved_name_ = item.name;
    name_slot_ = Slot();
    syncing_ = true;
    ::SetWindowTextW(::GetDlgItem(window_, kName), draft_.name.c_str());
    syncing_ = false;
    FillSources();
    Refresh();
    if (source_switched_)
      source_switched_();
  }
  void ExportCurrent() {
    if (Normalize(draft_.name).empty()) {
      ::MessageBoxW(window_, L"请先填写方案命名。", L"导出配色方案",
                    MB_OK | MB_ICONINFORMATION);
      return;
    }
    candidate_palette::ExportPlan plan;
    plan.name = wtou8(draft_.name);
    const size_t first = material_ * 2;
    plan.identity = draft_.sources[first] + "|" + draft_.sources[first + 1] +
                    (material_ ? "|normal" : "|acrylic");
    plan.light = draft_.colors[first];
    plan.dark = draft_.colors[first + 1];
    plan.paired = paired_material_[material_];
    plan.theme = source_themes_[material_];
    if (!plan.paired && plan.theme == weasel::PaletteTheme::Unspecified)
      plan.light = draft_.colors[Slot()];
    const std::string yaml = candidate_palette::BuildExportYaml(plan);
    std::wstring default_name = draft_.name;
    for (auto& character : default_name) {
      if (character < 32 || wcschr(L"<>:\"/\\|?*", character))
        character = L'_';
    }
    while (!default_name.empty() &&
           (default_name.back() == L'.' || default_name.back() == L' '))
      default_name.pop_back();
    if (default_name.empty())
      default_name = L"配色方案";
    if (default_name.size() > 80)
      default_name.resize(80);
    default_name += L".yaml";
    wchar_t filename[32768]{};
    std::copy(default_name.begin(), default_name.end(), filename);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter =
        L"YAML 配色文件 (*.yaml)\0*.yaml\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.lpstrDefExt = L"yaml";
    dialog.lpstrTitle = L"导出当前配色方案";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&dialog))
      return;
    const std::wstring temporary = std::wstring(filename) + L".tmp-" +
                                   std::to_wstring(::GetCurrentProcessId()) +
                                   L"-" + std::to_wstring(::GetTickCount64());
    HANDLE file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool saved = file != INVALID_HANDLE_VALUE;
    if (saved) {
      DWORD written = 0;
      saved = ::WriteFile(file, yaml.data(), static_cast<DWORD>(yaml.size()),
                          &written, nullptr) &&
              written == yaml.size() && ::FlushFileBuffers(file);
      ::CloseHandle(file);
      if (saved)
        saved = ::MoveFileExW(temporary.c_str(), filename,
                              MOVEFILE_REPLACE_EXISTING |
                                  MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!saved) {
      ::DeleteFileW(temporary.c_str());
      ::MessageBoxW(window_, L"无法保存配色文件，请检查目标位置。",
                    L"导出配色方案", MB_OK | MB_ICONERROR);
    }
  }
  void ImportFromText() {
    if (colors_changed() || !deleted_schemes_.empty() ||
        !pending_import_.schemes.empty()) {
      ::MessageBoxW(window_, L"请先应用或撤销当前修改，再导入配色文件。",
                    L"导入配色方案", MB_OK | MB_ICONINFORMATION);
      return;
    }
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter =
        L"配色文件 (*.txt;*.yaml;*.yml)\0*.txt;*.yaml;*.yml\0所有文件 "
        L"(*.*)\0*.*\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!::GetOpenFileNameW(&dialog))
      return;
    candidate_palette::ImportPlan plan;
    std::wstring error;
    if (!settings_->PreparePaletteImport(std::filesystem::path(filename),
                                         material_ == 0, &plan, &error)) {
      ::MessageBoxW(window_, error.c_str(), L"导入配色方案",
                    MB_OK | MB_ICONWARNING);
      return;
    }
    const size_t singles = plan.schemes.size() - plan.groups.size() * 2;
    std::wstring summary = L"已识别 " + std::to_wstring(plan.groups.size()) +
                           L" 组浅深色方案、" + std::to_wstring(singles) +
                           L" 个单独方案。\n同名方案会自动加“导入”后缀。\n\n按“"
                           L"是”后可在方案列表逐个预览，点击“应用”才会导入。";
    if (::MessageBoxW(window_, summary.c_str(), L"导入配色方案",
                      MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
      return;
    pending_import_ = std::move(plan);
    FillSources();
    const auto staged =
        std::find_if(source_items_.begin(), source_items_.end(),
                     [](const SourceItem& item) { return item.staged; });
    if (staged != source_items_.end()) {
      const SourceItem item = *staged;
      PreviewStaged(item);
    }
    if (changed_)
      changed_(Slot());
  }
  void DeleteSelectedSource(const SourceItem& item) {
    if (!item.own)
      return;
    if (colors_changed()) {
      ::MessageBoxW(window_, L"请先应用或撤销当前颜色修改，再删除方案。",
                    L"删除配色方案", MB_OK | MB_ICONINFORMATION);
      return;
    }
    std::string group_id;
    if (item.paired) {
      const std::string light = weasel::PaletteId(item.light);
      if (light.size() <= 6 ||
          light.compare(light.size() - 6, 6, "_light") != 0 ||
          weasel::PaletteId(item.dark) !=
              light.substr(0, light.size() - 6) + "_dark")
        return;
      group_id = light.substr(0, light.size() - 6);
    }
    const auto used = [&](const std::string& selection) {
      return !selection.empty() &&
             std::find(base_selections_.begin(), base_selections_.end(),
                       selection) != base_selections_.end();
    };
    const bool in_use = used(item.light) || used(item.dark);
    std::wstring question = L"删除“" + item.name + L"”" +
                            (item.paired ? L"的浅色与深色方案？" : L"？");
    if (in_use)
      question += L"\n正在使用的配色将切换到内置方案。";
    question += L"\n确认后按“应用”生效，关闭页面可撤销。";
    if (::MessageBoxW(window_, question.c_str(), L"删除配色方案",
                      MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES)
      return;
    auto replacement = base_selections_;
    const auto replace = [&](const std::string& selection) {
      if (selection.empty())
        return true;
      for (size_t slot = 0; slot < replacement.size(); ++slot) {
        if (replacement[slot] != selection)
          continue;
        replacement[slot] = BuiltinFallback(slot);
        if (replacement[slot].empty())
          return false;
      }
      return true;
    };
    if (!replace(item.light) || !replace(item.dark)) {
      ::MessageBoxW(window_, L"找不到可替代的内置配色，方案未删除。",
                    L"删除配色方案", MB_OK | MB_ICONERROR);
      return;
    }
    if (!item.light.empty())
      deleted_schemes_.push_back(weasel::PaletteId(item.light));
    if (!item.dark.empty() && item.dark != item.light)
      deleted_schemes_.push_back(weasel::PaletteId(item.dark));
    if (item.paired)
      deleted_groups_.push_back(group_id);
    Load(replacement);
    for (size_t slot = 0; slot < replacement.size(); ++slot)
      if (changed_)
        changed_(slot);
  }
  std::vector<int> list_roles_;
  static constexpr int kRoleHeight = 24;
  static constexpr int kVisibleRoles = 6;
  settings_color::Model model_ = settings_color::Model::Rgb;
  int material_ = 0, group_ = 2, role_ = 7;
  int warning_role_ = -1;
  bool warning_shown_ = false;
  std::wstring pager_warning_text_;
  bool dark_ = false, syncing_ = false, picking_ = false,
       updating_alpha_number_ = false;
  HWND screen_overlay_ = nullptr;
  std::optional<uint32_t> pending_pick_;
  std::optional<uint32_t> pick_preview_;
  static constexpr int kPickerTop = 66;
  void PositionPicker() {
    if (!window_ || !popup_host_)
      return;
    POINT position{Scale(list_width_ + 16), Scale(kPickerTop)};
    ::ClientToScreen(window_, &position);
    RECT current{};
    if (::GetWindowRect(popup_host_, &current) &&
        (current.left != position.x || current.top != position.y))
      ::SetWindowPos(popup_host_, nullptr, position.x, position.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  static LRESULT CALLBACK PopupHostProc(HWND host,
                                        UINT message,
                                        WPARAM w,
                                        LPARAM l) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(
        ::GetWindowLongPtrW(host, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<CandidatePaletteEditor*>(
          reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      ::SetWindowLongPtrW(host, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return ::DefWindowProcW(host, message, w, l);
    if (message == WM_MOUSEACTIVATE)
      return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND || message == WM_PAINT) {
      PAINTSTRUCT paint{};
      HDC dc = message == WM_PAINT ? ::BeginPaint(host, &paint)
                                   : reinterpret_cast<HDC>(w);
      RECT bounds{};
      ::GetClientRect(host, &bounds);
      ::FillRect(dc, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
      const COLORREF border =
          settings_navigation::Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                                   settings_theme::GetColor(COLOR_WINDOW), 70);
      HBRUSH border_brush = ::CreateSolidBrush(border);
      ::FrameRect(dc, &bounds, border_brush);
      ::DeleteObject(border_brush);
      if (message == WM_PAINT)
        ::EndPaint(host, &paint);
      return message == WM_PAINT ? 0 : TRUE;
    }
    if (message == WM_TIMER && w == 1) {
      if (!::IsWindowVisible(self->window_))
        self->HidePicker();
      else
        self->PositionPicker();
      return 0;
    }
    if (message == WM_KEYDOWN && w == VK_ESCAPE) {
      self->HidePicker();
      return 0;
    }
    if (message == WM_NCDESTROY && self->popup_host_ == host) {
      self->popup_host_ = nullptr;
      self->popup_ = nullptr;
    }
    return ::DefWindowProcW(host, message, w, l);
  }
  static LRESULT CALLBACK ScreenOverlayProc(HWND overlay,
                                            UINT message,
                                            WPARAM w,
                                            LPARAM l) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(
        ::GetWindowLongPtrW(overlay, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<CandidatePaletteEditor*>(
          reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      ::SetWindowLongPtrW(overlay, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return ::DefWindowProcW(overlay, message, w, l);
    POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    switch (message) {
      case WM_NCHITTEST:
        return HTCLIENT;
      case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
      case WM_SETCURSOR:
        ::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));
        return TRUE;
      case WM_MOUSEMOVE:
        ::ClientToScreen(overlay, &point);
        self->PreviewScreenColor(point);
        return 0;
      case WM_LBUTTONDOWN:
        ::ClientToScreen(overlay, &point);
        self->pending_pick_ = self->ScreenColor(point);
        return 0;
      case WM_LBUTTONUP: {
        ::ClientToScreen(overlay, &point);
        auto picked = self->pending_pick_;
        if (!picked)
          picked = self->ScreenColor(point);
        self->CancelScreenPick();
        if (picked)
          self->SetValue(*picked);
        return 0;
      }
      case WM_RBUTTONDOWN:
        return 0;
      case WM_RBUTTONUP:
        self->CancelScreenPick();
        return 0;
      case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
          self->CancelScreenPick();
          return 0;
        }
        break;
      case WM_TIMER: {
        HWND foreground = ::GetForegroundWindow();
        DWORD process = 0;
        if (foreground)
          ::GetWindowThreadProcessId(foreground, &process);
        if (foreground && process != ::GetCurrentProcessId())
          self->CancelScreenPick();
        return 0;
      }
      case WM_ERASEBKGND:
        return TRUE;
      case WM_NCDESTROY:
        if (self->screen_overlay_ == overlay) {
          self->screen_overlay_ = nullptr;
          self->CancelScreenPick();
        }
        break;
      default:
        break;
    }
    return ::DefWindowProcW(overlay, message, w, l);
  }
  int Scale(int n) const { return ::MulDiv(n, dpi_, 96); }
  size_t Slot() const { return material_ * 2 + (dark_ ? 1 : 0); }
  uint32_t Value() const { return draft_.colors[Slot()][role_]; }
  std::optional<uint32_t> ScreenColor(POINT point) const {
    HDC desktop = ::GetDC(nullptr);
    if (!desktop)
      return std::nullopt;
    const COLORREF sampled = ::GetPixel(desktop, point.x, point.y);
    ::ReleaseDC(nullptr, desktop);
    if (sampled == CLR_INVALID)
      return std::nullopt;
    return (uint32_t(GetRValue(sampled)) << 24) |
           (uint32_t(GetGValue(sampled)) << 16) |
           (uint32_t(GetBValue(sampled)) << 8) | (Value() & 255);
  }
  void PreviewScreenColor(POINT point) {
    const auto sample = ScreenColor(point);
    if (sample && pick_preview_ != sample) {
      pick_preview_ = sample;
      ::RedrawWindow(::GetDlgItem(window_, kSwatch), nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW);
    }
  }
  void PreviewScreenColor() {
    POINT point{};
    if (::GetCursorPos(&point))
      PreviewScreenColor(point);
  }
  std::wstring PagerWarningText() const {
    if (!settings_)
      return {};
    if (settings_->PreviewStyleBool("inline_preedit", false))
      return L"仅预览；当前启用行内预编辑，实际不显示翻页箭头。";
    const size_t slot = Slot();
    const bool configured =
        (settings_->PaletteRgba(selections_[slot], 16, dark_) & 255) != 0 &&
        (settings_->PaletteRgba(selections_[slot], 17, dark_) & 255) != 0;
    if (!configured)
      return L"仅预览；当前方案未同时设置两侧箭头颜色，实际不显示。";
    if ((draft_.colors[slot][16] & 255) == 0 ||
        (draft_.colors[slot][17] & 255) == 0)
      return L"仅预览；编辑中的箭头颜色透明，应用后实际不显示。";
    return {};
  }
  bool PagerMismatch() const { return !pager_warning_text_.empty(); }
  static constexpr UINT_PTR kWarningTimer = 82;
  RECT WarningRect(HDC dc, int role, const RECT& row) const {
    const auto label = candidate_palette::kRoles[role].label;
    SIZE extent{};
    ::GetTextExtentPoint32W(dc, label, static_cast<int>(std::wcslen(label)),
                            &extent);
    const int name_left = row.left + Scale(35);
    const int name_right = row.right - Scale(78);
    const int x = (std::min)(name_left + int(extent.cx) + Scale(5),
                             name_right - Scale(13));
    return {x, row.top, x + Scale(11), row.bottom};
  }
  int WarningAtPoint(POINT point) const {
    if (!list_ || !PagerMismatch())
      return -1;
    const DWORD item = static_cast<DWORD>(::SendMessageW(
        list_, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y)));
    const int index = LOWORD(item);
    if (HIWORD(item) || index < 0 || index >= int(list_roles_.size()))
      return -1;
    const int role = list_roles_[index];
    if (role != 16 && role != 17)
      return -1;
    RECT row{};
    if (::SendMessageW(list_, LB_GETITEMRECT, index,
                       reinterpret_cast<LPARAM>(&row)) == LB_ERR)
      return -1;
    HDC dc = ::GetDC(list_);
    if (!dc)
      return -1;
    HGDIOBJ previous = font_ ? ::SelectObject(dc, font_) : nullptr;
    const RECT mark = WarningRect(dc, role, row);
    if (previous)
      ::SelectObject(dc, previous);
    ::ReleaseDC(list_, dc);
    return ::PtInRect(&mark, point) ? role : -1;
  }
  void HideWarningTip() {
    if (list_ && ::IsWindow(list_))
      ::KillTimer(list_, kWarningTimer);
    if (warning_tip_ && ::IsWindow(warning_tip_) && warning_shown_) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.hwnd = list_;
      tool.uId = 1;
      ::SendMessageW(warning_tip_, TTM_TRACKACTIVATE, FALSE,
                     reinterpret_cast<LPARAM>(&tool));
    }
    warning_role_ = -1;
    warning_shown_ = false;
  }
  void UpdateWarningHover(POINT point) {
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, list_, 0};
    ::TrackMouseEvent(&tracking);
    const int role = WarningAtPoint(point);
    if (role == warning_role_)
      return;
    HideWarningTip();
    if (role >= 0) {
      warning_role_ = role;
      ::SetTimer(list_, kWarningTimer, 450, nullptr);
    }
  }
  void ShowWarningTip() {
    if (!warning_tip_ || warning_role_ < 0)
      return;
    POINT cursor{};
    if (!::GetCursorPos(&cursor))
      return;
    POINT local = cursor;
    ::ScreenToClient(list_, &local);
    if (WarningAtPoint(local) != warning_role_) {
      HideWarningTip();
      return;
    }
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.hwnd = list_;
    tool.uId = 1;
    tool.lpszText = pager_warning_text_.data();
    ::SendMessageW(warning_tip_, TTM_UPDATETIPTEXTW, 0,
                   reinterpret_cast<LPARAM>(&tool));
    ::SendMessageW(warning_tip_, TTM_TRACKPOSITION, 0,
                   MAKELPARAM(cursor.x + Scale(12), cursor.y + Scale(18)));
    ::SendMessageW(warning_tip_, TTM_TRACKACTIVATE, TRUE,
                   reinterpret_cast<LPARAM>(&tool));
    warning_shown_ = true;
  }
  static LRESULT CALLBACK ListProc(HWND window,
                                   UINT message,
                                   WPARAM w,
                                   LPARAM l,
                                   UINT_PTR,
                                   DWORD_PTR data) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(data);
    if (message == WM_MOUSEMOVE)
      self->UpdateWarningHover({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
    else if (message == WM_TIMER && w == kWarningTimer) {
      ::KillTimer(window, kWarningTimer);
      self->ShowWarningTip();
      return 0;
    } else if (message == WM_MOUSELEAVE || message == WM_MOUSEWHEEL ||
               message == WM_VSCROLL || message == WM_LBUTTONDOWN)
      self->HideWarningTip();
    else if (message == WM_NCDESTROY) {
      self->HideWarningTip();
      ::RemoveWindowSubclass(window, ListProc, 82);
    }
    return ::DefSubclassProc(window, message, w, l);
  }
  static std::wstring Normalize(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
      return {};
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return value;
  }
  static std::wstring Text(HWND window) {
    const int length = ::GetWindowTextLengthW(window);
    std::wstring value(length + 1, L'\0');
    ::GetWindowTextW(window, value.data(), length + 1);
    value.resize(length);
    return value;
  }
  HWND Add(const wchar_t* type,
           const wchar_t* text,
           DWORD style,
           int id,
           int x,
           int y,
           int w,
           int h) {
    const bool edit = _wcsicmp(type, L"EDIT") == 0;
    HWND control = settings_navigation::Create(
        window_, type, text, style | (edit ? WS_BORDER : 0), WORD(id), Scale(x),
        Scale(y), Scale(w), Scale(h));
    ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    if (edit) {
      settings_navigation::StyleInput(window_, WORD(id));
      settings_navigation::StyleVerticallyCenteredInput(window_, WORD(id),
                                                        dpi_);
    }
    return control;
  }
  static LRESULT CALLBACK SourceComboProc(HWND combo,
                                          UINT message,
                                          WPARAM w,
                                          LPARAM l,
                                          UINT_PTR,
                                          DWORD_PTR data) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(data);
    if (message == CB_SHOWDROPDOWN && w && self && self->source_tip_) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
      tool.hwnd = self->window_;
      tool.uId = reinterpret_cast<UINT_PTR>(combo);
      ::SendMessageW(self->source_tip_, TTM_POP, 0, 0);
      ::SendMessageW(self->source_tip_, TTM_DELTOOLW, 0,
                     reinterpret_cast<LPARAM>(&tool));
    }
    if (message == CB_SHOWDROPDOWN && w) {
      COMBOBOXINFO info{sizeof(info)};
      if (::GetComboBoxInfo(combo, &info) && info.hwndList)
        ::SetWindowTheme(
            info.hwndList,
            settings_theme::Colors().dark ? L"DarkMode_Explorer" : L"Explorer",
            nullptr);
    }
    const LRESULT result = ::DefSubclassProc(combo, message, w, l);
    if (message == CB_SHOWDROPDOWN && w)
      ::SendMessageW(combo, CB_SETTOPINDEX, 0, 0);
    if (message == CB_SHOWDROPDOWN && !w && self && self->source_tip_) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
      tool.hwnd = self->window_;
      tool.uId = reinterpret_cast<UINT_PTR>(combo);
      tool.lpszText = self->source_tip_text_.data();
      ::SendMessageW(self->source_tip_, TTM_ADDTOOLW, 0,
                     reinterpret_cast<LPARAM>(&tool));
    }
    if (message == WM_NCDESTROY)
      ::RemoveWindowSubclass(combo, SourceComboProc, 84);
    return result;
  }
  static LRESULT CALLBACK SourceListProc(HWND list,
                                         UINT message,
                                         WPARAM w,
                                         LPARAM l,
                                         UINT_PTR,
                                         DWORD_PTR data) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(data);
    if (message == WM_MOUSEMOVE && self && self->source_tip_) {
      const DWORD hit = static_cast<DWORD>(
          ::SendMessageW(list, LB_ITEMFROMPOINT, 0,
                         MAKELPARAM(GET_X_LPARAM(l), GET_Y_LPARAM(l))));
      const int index = HIWORD(hit) ? -1 : LOWORD(hit);
      if (index != self->source_list_hover_index_) {
        self->source_list_hover_index_ = index;
        self->source_list_tip_text_ =
            index >= 0 &&
                    static_cast<size_t>(index) < self->source_items_.size()
                ? self->source_items_[index].label
                : L"";
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = self->window_;
        tool.uId = reinterpret_cast<UINT_PTR>(list);
        tool.lpszText = self->source_list_tip_text_.data();
        ::SendMessageW(self->source_tip_, TTM_POP, 0, 0);
        ::SendMessageW(self->source_tip_, TTM_UPDATETIPTEXTW, 0,
                       reinterpret_cast<LPARAM>(&tool));
      }
    }
    if (message == WM_SHOWWINDOW && !w && self) {
      self->source_list_hover_index_ = -1;
      if (self->source_tip_)
        ::SendMessageW(self->source_tip_, TTM_POP, 0, 0);
    }
    if (message == WM_NCDESTROY)
      ::RemoveWindowSubclass(list, SourceListProc, 85);
    return ::DefSubclassProc(list, message, w, l);
  }
  void RebuildRoleList() {
    if (!list_)
      return;
    HideWarningTip();
    ::SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ::SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    list_roles_.clear();
    for (size_t role = 0; role < candidate_palette::kRoles.size(); ++role) {
      if (candidate_palette::kRoles[role].group != unsigned(group_))
        continue;
      list_roles_.push_back(static_cast<int>(role));
      ::SendMessageW(
          list_, LB_ADDSTRING, 0,
          reinterpret_cast<LPARAM>(candidate_palette::kRoles[role].label));
    }
    ::SetWindowPos(list_, nullptr, 0, 0, Scale(list_width_ - 2),
                   kVisibleRoles * Scale(kRoleHeight),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ::SendMessageW(list_, LB_SETTOPINDEX, 0, 0);
    ::SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(list_, nullptr, FALSE);
  }
  int CurrentRoleRow() const {
    const auto row = std::find(list_roles_.begin(), list_roles_.end(), role_);
    return row == list_roles_.end()
               ? -1
               : static_cast<int>(row - list_roles_.begin());
  }
  void AlignComboHeight(HWND combo, int target_height) const {
    RECT bounds{};
    if (!combo || !::GetWindowRect(combo, &bounds))
      return;
    const int item_height = static_cast<int>(
        ::SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0));
    if (item_height != CB_ERR)
      ::SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                     (std::max)(1, item_height + Scale(target_height) -
                                       int(bounds.bottom - bounds.top)));
  }
  void Init() {
    RECT bounds{};
    ::GetClientRect(window_, &bounds);
    const int width = ::MulDiv(bounds.right, 96, dpi_);
    const int gap = 16;
    // The former three-pixel gaps are now part of the adjacent segments, so
    // the five-button group's outer width stays unchanged.
    constexpr int category_widths[] = {54, 48, 62, 62, 38};
    int list_width = 0;
    for (int segment_width : category_widths)
      list_width += segment_width;
    list_width_ = list_width;
    const int right = list_width + gap;
    const int right_width = width - right;
    Add(L"STATIC", L"配色方案", SS_CENTERIMAGE, 0, 0, 36, 58, 24);
    HWND source = Add(L"COMBOBOX", L"",
                      CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE |
                          CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                      kSource, 62, 36, list_width - 62, 140);
    settings_navigation::StyleCombo(window_, kSource);
    AlignComboHeight(source, 24);
    ::SetWindowSubclass(source, SourceComboProc, 84,
                        reinterpret_cast<DWORD_PTR>(this));
    source_tip_ = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                    WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    CW_USEDEFAULT, window_, nullptr,
                                    ::GetModuleHandleW(nullptr), nullptr);
    if (source_tip_) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
      tool.hwnd = window_;
      tool.uId = reinterpret_cast<UINT_PTR>(source);
      tool.lpszText = const_cast<wchar_t*>(L"");
      ::SendMessageW(source_tip_, TTM_ADDTOOLW, 0,
                     reinterpret_cast<LPARAM>(&tool));
      COMBOBOXINFO info{sizeof(info)};
      if (::GetComboBoxInfo(source, &info) && info.hwndList) {
        tool.uId = reinterpret_cast<UINT_PTR>(info.hwndList);
        ::SendMessageW(source_tip_, TTM_ADDTOOLW, 0,
                       reinterpret_cast<LPARAM>(&tool));
        ::SetWindowSubclass(info.hwndList, SourceListProc, 85,
                            reinterpret_cast<DWORD_PTR>(this));
      }
    }
    Add(L"STATIC", L"方案命名", SS_CENTERIMAGE, 0, 0, 72, 57, 24);
    Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kName, 60, 72,
        list_width - 60, 24);
    Add(L"STATIC", L"编辑配色", SS_CENTERIMAGE, 0, 0, 108, 59, 24);
    Add(L"BUTTON", L"浅色", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, kLight,
        61, 108, (list_width - 61) / 2, 24);
    Add(L"BUTTON", L"深色", BS_AUTORADIOBUTTON | WS_TABSTOP, kDark,
        61 + (list_width - 61) / 2, 108,
        list_width - 61 - (list_width - 61) / 2, 24);
    int x = 0;
    for (int group = 0; group < 5; ++group) {
      Add(L"BUTTON", candidate_palette::kGroups[group],
          BS_OWNERDRAW | WS_TABSTOP, kCategory + group, x, 144,
          category_widths[group], 24);
      x += category_widths[group];
    }
    list_ = Add(L"LISTBOX", L"",
                LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT |
                    LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
                kList, 1, 180, list_width - 2, kRoleHeight);
    ::SetWindowSubclass(list_, ListProc, 82, reinterpret_cast<DWORD_PTR>(this));
    RebuildRoleList();
    warning_tip_ = ::CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr,
        ::GetModuleHandleW(nullptr), nullptr);
    if (warning_tip_) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
      tool.hwnd = list_;
      tool.uId = 1;
      tool.lpszText = const_cast<wchar_t*>(L"");
      ::SendMessageW(warning_tip_, TTM_ADDTOOLW, 0,
                     reinterpret_cast<LPARAM>(&tool));
    }
    Add(L"BUTTON", L"恢复默认", BS_OWNERDRAW | WS_TABSTOP, kReset, right, 36,
        66, 24);
    Add(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, kSwatch,
        right + right_width - 68, 36, 30, 24);
    Add(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, kPick,
        right + right_width - 34, 36, 34, 24);
    Add(L"STATIC", L"颜色模式", SS_CENTERIMAGE, 0, right, 72, 58, 24);
    int mode_label_right = 54;
    HDC label_dc = ::GetDC(window_);
    if (label_dc) {
      HGDIOBJ old_font = font_ ? ::SelectObject(label_dc, font_) : nullptr;
      SIZE extent{};
      if (::GetTextExtentPoint32W(label_dc, L"颜色模式", 4, &extent))
        mode_label_right = std::clamp(::MulDiv(extent.cx, 96, dpi_), 48, 58);
      if (old_font)
        ::SelectObject(label_dc, old_font);
      ::ReleaseDC(window_, label_dc);
    }
    HWND mode = Add(L"COMBOBOX", L"",
                    CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE | CBS_HASSTRINGS |
                        WS_VSCROLL | WS_TABSTOP,
                    kModel, right + 62, 72, right_width - 62, 120);
    for (const auto* label : {L"HEX", L"RGB", L"HSV", L"HSL", L"CMYK"})
      ::SendDlgItemMessageW(window_, kModel, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(label));
    ::SendDlgItemMessageW(window_, kModel, CB_SETCURSEL, 1, 0);
    settings_navigation::StyleCombo(window_, kModel);
    AlignComboHeight(mode, 24);
    const int alpha_y = 180 + kVisibleRoles * kRoleHeight - 20;
    const int code_y = alpha_y - 36;
    const int channel_step = 36;
    for (int i = 0; i < 4; ++i) {
      const int row_y = 108 + i * channel_step;
      Add(L"STATIC", L"", SS_CENTERIMAGE, kChannelLabel + i, right, row_y,
          mode_label_right - 12, 20);
      Add(L"STATIC", L"", SS_CENTERIMAGE | SS_RIGHT, kChannelLetter + i,
          right + mode_label_right - 12, row_y, 12, 20);
      HWND slider =
          Add(L"STATIC", L"", SS_NOTIFY | WS_TABSTOP, kChannelSlider + i,
              right + 62, row_y, right_width - 114, 20);
      ::SetWindowSubclass(slider, SliderProc, 81,
                          reinterpret_cast<DWORD_PTR>(this));
      Add(L"EDIT", L"", ES_NUMBER | ES_CENTER | WS_TABSTOP, kChannelNumber + i,
          right + right_width - 44, row_y, 44, 20);
    }
    Add(L"STATIC", L"颜色代码", SS_CENTERIMAGE, 0, right, code_y, 58, 23);
    Add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, kCode, right + 62, code_y,
        right_width - 62, 23);
    Add(L"STATIC", L"不透明度", SS_CENTERIMAGE, kAlphaLabel, right, alpha_y, 58,
        20);
    HWND alpha = Add(L"STATIC", L"", SS_NOTIFY | WS_TABSTOP, kAlphaSlider,
                     right + 62, alpha_y, right_width - 114, 20);
    ::SetWindowSubclass(alpha, SliderProc, 81,
                        reinterpret_cast<DWORD_PTR>(this));
    HWND alpha_number =
        Add(L"EDIT", L"", ES_NUMBER | ES_CENTER | WS_TABSTOP, kAlphaNumber,
            right + right_width - 44, alpha_y, 32, 20);
    ::SendMessageW(alpha_number, EM_SETLIMITTEXT, 3, 0);
    Add(L"STATIC", L"%", SS_CENTERIMAGE | SS_RIGHT, kAlphaPercent,
        right + right_width - 12, alpha_y, 12, 20);
    ::SetWindowPos(::GetDlgItem(window_, kModel), nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    // Cover the lower right section completely while leaving the swatch and
    // pipette row above the popup available to close or change the picker.
    WNDCLASSW popup_type{};
    popup_type.hInstance = ::GetModuleHandleW(nullptr);
    popup_type.lpfnWndProc = PopupHostProc;
    popup_type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    popup_type.lpszClassName = L"Weasel.CandidateColorPickerHost";
    ::RegisterClassW(&popup_type);
    const int popup_left = Scale(right);
    const int popup_top = Scale(kPickerTop);
    const int popup_width = bounds.right - popup_left;
    const int popup_height = bounds.bottom - popup_top;
    POINT popup_position{popup_left, popup_top};
    ::ClientToScreen(window_, &popup_position);
    popup_host_ = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_COMPOSITED,
        popup_type.lpszClassName, L"", WS_POPUP | WS_CLIPCHILDREN,
        popup_position.x, popup_position.y, popup_width, popup_height,
        ::GetAncestor(window_, GA_ROOT), nullptr, popup_type.hInstance, this);
    picker_.Create(popup_host_, font_, 0, 0, popup_width, dpi_,
                   [this](settings_color::Color color) {
                     const uint32_t old = Value();
                     updating_from_picker_ = true;
                     SetValue((uint32_t(color.r) << 24) |
                              (uint32_t(color.g) << 16) |
                              (uint32_t(color.b) << 8) | (old & 0xff));
                     updating_from_picker_ = false;
                   });
    popup_ = picker_.window();
    picker_.SetHueHorizontal(true);
    for (int id : {40100, 40103, 40104, 40105, 40106, 40110, 40111, 40112,
                   40113, 40120, 40121, 40122, 40123})
      ::ShowWindow(::GetDlgItem(popup_, id), SW_HIDE);
    const int margin = Scale(6);
    const int hue_gap = Scale(8);
    const int hue_height = Scale(20);
    const int surface_width = popup_width - margin * 2;
    const int plane_height = popup_height - margin * 2 - hue_gap - hue_height;
    ::SetWindowPos(::GetDlgItem(popup_, 40101), nullptr, margin, margin,
                   surface_width, plane_height, SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(::GetDlgItem(popup_, 40102), nullptr, margin,
                   margin + plane_height + hue_gap, surface_width, hue_height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowLongPtrW(
        popup_, GWL_STYLE,
        ::GetWindowLongPtrW(popup_, GWL_STYLE) | WS_CLIPSIBLINGS);
    ::SetWindowPos(popup_, HWND_TOP, 0, 0, popup_width, popup_height,
                   SWP_FRAMECHANGED | SWP_NOACTIVATE);
    ::ShowWindow(popup_host_, SW_HIDE);
    settings_navigation::StyleSegmentedToggle(
        window_, kLight, settings_navigation::ToggleState::Segment::Left);
    settings_navigation::StyleSegmentedToggle(
        window_, kDark, settings_navigation::ToggleState::Segment::Right);
    Refresh();
  }

  static settings_color::Color Rgb(uint32_t rgba) {
    return {int((rgba >> 24) & 255), int((rgba >> 16) & 255),
            int((rgba >> 8) & 255)};
  }
  static std::wstring Hex(uint32_t rgba) {
    wchar_t text[12]{};
    swprintf_s(text, L"#%08X", rgba);
    return text;
  }
  static std::optional<uint32_t> Parse(std::wstring text, uint32_t old) {
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](wchar_t c) { return std::iswspace(c) != 0; }),
               text.end());
    const bool eight = text.size() == 9 && text[0] == L'#';
    if (eight) {
      try {
        size_t end = 0;
        const auto parsed = std::stoull(text.substr(1), &end, 16);
        if (end == 8 && parsed <= 0xffffffffull)
          return static_cast<uint32_t>(parsed);
      } catch (...) {
      }
      return std::nullopt;
    }
    const auto parsed = settings_color::Parse(text);
    if (!parsed)
      return std::nullopt;
    return (uint32_t(parsed->r) << 24) | (uint32_t(parsed->g) << 16) |
           (uint32_t(parsed->b) << 8) | (old & 255);
  }
  int Count() const {
    return model_ == settings_color::Model::Hex    ? 0
           : model_ == settings_color::Model::Cmyk ? 4
                                                   : 3;
  }
  int Limit(int channel) const {
    return model_ == settings_color::Model::Rgb                    ? 255
           : channel == 0 && model_ != settings_color::Model::Cmyk ? 360
                                                                   : 100;
  }
  void SetValue(uint32_t value) {
    if (staged_preview_)
      return;
    if (Value() == value)
      return;
    draft_.colors[Slot()][role_] = value;
    draft_.changed[Slot()] =
        draft_.colors[Slot()] != saved_colors_[Slot()] ||
        (NameAffects(Slot()) && draft_.name != saved_name_);
    draft_.edited[Slot()][role_] = value != saved_colors_[Slot()][role_];
    Refresh();
    if (changed_)
      changed_(Slot());
  }
  void Refresh() {
    if (!window_ || !list_)
      return;
    HideWarningTip();
    pager_warning_text_ = PagerWarningText();
    syncing_ = true;
    ::SendDlgItemMessageW(window_, kLight, BM_SETCHECK,
                          dark_ ? BST_UNCHECKED : BST_CHECKED, 0);
    ::SendDlgItemMessageW(window_, kDark, BM_SETCHECK,
                          dark_ ? BST_CHECKED : BST_UNCHECKED, 0);
    for (int i = 0; i < 5; ++i)
      ::InvalidateRect(::GetDlgItem(window_, kCategory + i), nullptr, FALSE);
    ::SendMessageW(list_, LB_SETCURSEL, CurrentRoleRow(), 0);
    ::InvalidateRect(list_, nullptr, FALSE);
    ::SetWindowTextW(::GetDlgItem(window_, kCode), Hex(Value()).c_str());
    const auto channels = settings_color::Values(Rgb(Value()), model_);
    const wchar_t* labels[][4] = {{L"", L"", L"", L""},
                                  {L"红色", L"绿色", L"蓝色", L""},
                                  {L"色相", L"饱和度", L"明度", L""},
                                  {L"色相", L"饱和度", L"亮度", L""},
                                  {L"青色", L"品红", L"黄色", L"黑色"}};
    const wchar_t* letters[][4] = {{L"", L"", L"", L""},
                                   {L"R", L"G", L"B", L""},
                                   {L"H", L"S", L"V", L""},
                                   {L"H", L"S", L"L", L""},
                                   {L"C", L"M", L"Y", L"K"}};
    RECT exposed_rows{};
    bool repaint_exposed_rows = false;
    for (int i = 0; i < 4; ++i) {
      const bool visible = i < Count();
      bool row_hidden = false;
      for (int id : {kChannelLabel + i, kChannelLetter + i, kChannelSlider + i,
                     kChannelNumber + i}) {
        HWND control = ::GetDlgItem(window_, id);
        const bool shown =
            (::GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
        if (shown != visible) {
          row_hidden |= shown && !visible;
          ::ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
        }
      }
      if (row_hidden) {
        RECT label{}, number{};
        ::GetWindowRect(::GetDlgItem(window_, kChannelLabel + i), &label);
        ::GetWindowRect(::GetDlgItem(window_, kChannelNumber + i), &number);
        ::MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&label),
                          2);
        ::MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&number),
                          2);
        RECT row{label.left, label.top, number.right, number.bottom};
        ::InflateRect(&row, Scale(2), Scale(2));
        if (repaint_exposed_rows)
          ::UnionRect(&exposed_rows, &exposed_rows, &row);
        else
          exposed_rows = row;
        repaint_exposed_rows = true;
      }
      if (!visible)
        continue;
      ::SetWindowTextW(::GetDlgItem(window_, kChannelLabel + i),
                       labels[static_cast<int>(model_)][i]);
      ::SetWindowTextW(::GetDlgItem(window_, kChannelLetter + i),
                       letters[static_cast<int>(model_)][i]);
      ::SetWindowTextW(::GetDlgItem(window_, kChannelNumber + i),
                       std::to_wstring(int(std::lround(channels[i]))).c_str());
      ::InvalidateRect(::GetDlgItem(window_, kChannelSlider + i), nullptr,
                       FALSE);
    }
    if (repaint_exposed_rows)
      ::RedrawWindow(window_, &exposed_rows, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    if (!updating_alpha_number_)
      ::SetWindowTextW(
          ::GetDlgItem(window_, kAlphaNumber),
          std::to_wstring(int(std::lround((Value() & 255) * 100.0 / 255)))
              .c_str());
    ::InvalidateRect(::GetDlgItem(window_, kAlphaSlider), nullptr, FALSE);
    ::InvalidateRect(::GetDlgItem(window_, kSwatch), nullptr, FALSE);
    if (picker_visible() && !updating_from_picker_) {
      picker_.Set(Rgb(Value()), true);
      ::ShowWindow(::GetDlgItem(popup_, 40100), SW_HIDE);
      ::RedrawWindow(popup_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    const bool editable = !staged_preview_.has_value();
    for (int id :
         {kName, kCode, kAlphaNumber, kAlphaSlider, kReset, kSwatch, kPick})
      ::EnableWindow(::GetDlgItem(window_, id), editable);
    for (int index = 0; index < 4; ++index) {
      ::EnableWindow(::GetDlgItem(window_, kChannelSlider + index), editable);
      ::EnableWindow(::GetDlgItem(window_, kChannelNumber + index), editable);
    }
    syncing_ = false;
  }
  void UpdateComponents() {
    if (syncing_)
      return;
    std::array<double, 4> values{};
    for (int i = 0; i < Count(); ++i) {
      const auto parsed = settings_color::Number(
          Text(::GetDlgItem(window_, kChannelNumber + i)));
      if (!parsed || *parsed < 0 || *parsed > Limit(i))
        return;
      values[i] = *parsed;
    }
    const auto color = settings_color::FromValues(model_, values);
    if (!color)
      return;
    SetValue((uint32_t(color->r) << 24) | (uint32_t(color->g) << 16) |
             (uint32_t(color->b) << 8) | (Value() & 255));
  }
  void UpdateSlider(HWND slider, int x) {
    RECT bounds{};
    ::GetClientRect(slider, &bounds);
    const int inset = Scale(5);
    const int track_left = inset;
    const int track_right =
        (std::max)(track_left, int(bounds.right) - inset - 1);
    const double fraction = std::clamp(
        double(x - track_left) / (std::max)(1, track_right - track_left), 0.0,
        1.0);
    const int id = ::GetDlgCtrlID(slider);
    if (id == kAlphaSlider) {
      const int alpha = int(std::lround(fraction * 255));
      SetValue((Value() & 0xffffff00u) | uint32_t(alpha));
      return;
    }
    const int index = id - kChannelSlider;
    if (index < 0 || index >= Count())
      return;
    auto channels = settings_color::Values(Rgb(Value()), model_);
    channels[index] = std::round(fraction * Limit(index));
    const auto color = settings_color::FromValues(model_, channels);
    if (color)
      SetValue((uint32_t(color->r) << 24) | (uint32_t(color->g) << 16) |
               (uint32_t(color->b) << 8) | (Value() & 255));
  }
  static COLORREF Mix(settings_color::Color color,
                      COLORREF surface,
                      int alpha) {
    return RGB((color.r * alpha + GetRValue(surface) * (255 - alpha)) / 255,
               (color.g * alpha + GetGValue(surface) * (255 - alpha)) / 255,
               (color.b * alpha + GetBValue(surface) * (255 - alpha)) / 255);
  }
  static void DrawPipetteLayer(Gdiplus::Graphics& graphics,
                               int resource_id,
                               COLORREF color,
                               const Gdiplus::Rect& destination) {
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC resource =
        ::FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource)
      return;
    const DWORD size = ::SizeofResource(module, resource);
    const void* source = ::LockResource(::LoadResource(module, resource));
    if (!source || !size)
      return;
    HGLOBAL copy = ::GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy)
      return;
    void* bytes = ::GlobalLock(copy);
    if (!bytes) {
      ::GlobalFree(copy);
      return;
    }
    std::memcpy(bytes, source, size);
    ::GlobalUnlock(copy);
    IStream* stream = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(copy, TRUE, &stream))) {
      ::GlobalFree(copy);
      return;
    }
    {
      std::unique_ptr<Gdiplus::Bitmap> mask(
          Gdiplus::Bitmap::FromStream(stream));
      if (mask && mask->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::ColorMatrix tint{};
        tint.m[3][3] = 1.0f;
        tint.m[4][0] = GetRValue(color) / 255.0f;
        tint.m[4][1] = GetGValue(color) / 255.0f;
        tint.m[4][2] = GetBValue(color) / 255.0f;
        tint.m[4][4] = 1.0f;
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&tint);
        graphics.DrawImage(mask.get(), destination, 0, 0, mask->GetWidth(),
                           mask->GetHeight(), Gdiplus::UnitPixel, &attributes);
      }
    }
    stream->Release();
  }
  void DrawSourceItem(const DRAWITEMSTRUCT& draw) const {
    if ((draw.itemState & ODS_COMBOBOXEDIT) != 0) {
      settings_navigation::DrawComboItem(draw);
      return;
    }
    ::FillRect(draw.hDC, &draw.rcItem, settings_theme::GetBrush(COLOR_WINDOW));
    if (draw.itemID >= source_items_.size())
      return;
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    if (selected) {
      const COLORREF fill =
          settings_navigation::Mix(settings_theme::GetColor(COLOR_HIGHLIGHT),
                                   settings_theme::GetColor(COLOR_WINDOW), 25);
      RECT selection = draw.rcItem;
      const int horizontal_inset = (std::max)(3, Scale(3));
      const int vertical_inset = (std::max)(1, Scale(1));
      ::InflateRect(&selection, -horizontal_inset, -vertical_inset);
      HBRUSH brush = ::CreateSolidBrush(fill);
      HPEN pen = ::CreatePen(PS_NULL, 0, fill);
      HGDIOBJ old_brush = ::SelectObject(draw.hDC, brush);
      HGDIOBJ old_pen = ::SelectObject(draw.hDC, pen);
      const int radius = (std::max)(6, Scale(8));
      ::RoundRect(draw.hDC, selection.left, selection.top, selection.right,
                  selection.bottom, radius, radius);
      ::SelectObject(draw.hDC, old_pen);
      ::SelectObject(draw.hDC, old_brush);
      ::DeleteObject(pen);
      ::DeleteObject(brush);

      const int marker_width = (std::max)(2, Scale(3));
      RECT marker{selection.left + horizontal_inset,
                  selection.top + (selection.bottom - selection.top) / 4,
                  selection.left + horizontal_inset + marker_width,
                  selection.bottom - (selection.bottom - selection.top) / 4};
      HBRUSH marker_brush =
          ::CreateSolidBrush(settings_theme::GetColor(COLOR_HIGHLIGHT));
      HRGN marker_region =
          ::CreateRoundRectRgn(marker.left, marker.top, marker.right,
                               marker.bottom, marker_width, marker_width);
      if (marker_region) {
        ::FillRgn(draw.hDC, marker_region, marker_brush);
        ::DeleteObject(marker_region);
      }
      ::DeleteObject(marker_brush);
    }
    RECT text = draw.rcItem;
    text.left += selected ? (std::max)(13, Scale(13)) : (std::max)(7, Scale(7));
    text.right -= (std::max)(7, Scale(7));

    const SourceItem& item = source_items_[draw.itemID];
    if (item.removal) {
      const COLORREF line =
          settings_navigation::Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                                   settings_theme::GetColor(COLOR_WINDOW), 75);
      HPEN pen = ::CreatePen(PS_SOLID, 1, line);
      HGDIOBJ old_pen = ::SelectObject(draw.hDC, pen);
      ::MoveToEx(draw.hDC, draw.rcItem.left + Scale(7), draw.rcItem.top,
                 nullptr);
      ::LineTo(draw.hDC, draw.rcItem.right - Scale(7), draw.rcItem.top);
      ::SelectObject(draw.hDC, old_pen);
      ::DeleteObject(pen);
    }
    HGDIOBJ previous = font_ ? ::SelectObject(draw.hDC, font_) : nullptr;
    ::SetBkMode(draw.hDC, TRANSPARENT);
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    ::SetTextColor(draw.hDC,
                   settings_theme::GetColor(disabled       ? COLOR_GRAYTEXT
                                            : item.removal ? COLOR_HOTLIGHT
                                                           : COLOR_WINDOWTEXT));
    RECT name = text;
    const bool scheme =
        item.staged || !item.light.empty() || !item.dark.empty();
    if (scheme) {
      const int swatch = Scale(10);
      const int top =
          draw.rcItem.top + (draw.rcItem.bottom - draw.rcItem.top - swatch) / 2;
      const COLORREF outline =
          settings_navigation::Mix(settings_theme::GetColor(COLOR_WINDOWTEXT),
                                   settings_theme::GetColor(COLOR_WINDOW), 45);
      HBRUSH border_brush = ::CreateSolidBrush(outline);
      const auto tile = [&](int x, COLORREF first, COLORREF second) {
        RECT bounds{x, top, x + swatch, top + swatch};
        HBRUSH first_brush = ::CreateSolidBrush(first);
        ::FillRect(draw.hDC, &bounds, first_brush);
        ::DeleteObject(first_brush);
        if (first != second) {
          HBRUSH second_brush = ::CreateSolidBrush(second);
          HGDIOBJ old_brush = ::SelectObject(draw.hDC, second_brush);
          HGDIOBJ old_pen =
              ::SelectObject(draw.hDC, ::GetStockObject(NULL_PEN));
          POINT triangle[3] = {{bounds.right, bounds.top},
                               {bounds.right, bounds.bottom},
                               {bounds.left, bounds.bottom}};
          ::Polygon(draw.hDC, triangle, 3);
          ::SelectObject(draw.hDC, old_pen);
          ::SelectObject(draw.hDC, old_brush);
          ::DeleteObject(second_brush);
        }
        ::FrameRect(draw.hDC, &bounds, border_brush);
      };
      const COLORREF light = RGB(218, 224, 230);
      const COLORREF dark = RGB(55, 60, 66);
      if (item.paired) {
        tile(text.left, light, dark);
      } else {
        if (item.theme == weasel::PaletteTheme::Light)
          tile(text.left, light, light);
        else if (item.theme == weasel::PaletteTheme::Dark)
          tile(text.left, dark, dark);
        else {
          RECT bounds{text.left, top, text.left + swatch, top + swatch};
          ::FrameRect(draw.hDC, &bounds, border_brush);
          const int dash_width = (std::max)(Scale(4), 3);
          const int dash_height = (std::max)(Scale(1), 1);
          const int dash_left = bounds.left + (swatch - dash_width) / 2;
          const int dash_top = bounds.top + (swatch - dash_height) / 2;
          RECT dash{dash_left, dash_top, dash_left + dash_width,
                    dash_top + dash_height};
          ::FillRect(draw.hDC, &dash, border_brush);
        }
      }
      ::DeleteObject(border_brush);
      name.left += swatch + Scale(7);
    }
    if (!item.detail.empty()) {
      const std::wstring badge =
          item.detail.substr(0, item.detail.find(L" · "));
      SIZE badge_size{};
      ::GetTextExtentPoint32W(draw.hDC, badge.c_str(),
                              static_cast<int>(badge.size()), &badge_size);
      RECT badge_bounds = text;
      badge_bounds.left = badge_bounds.right - badge_size.cx - Scale(2);
      name.right = (std::max)(name.left, badge_bounds.left - Scale(8));
      ::SetTextColor(draw.hDC, settings_theme::GetColor(COLOR_GRAYTEXT));
      ::DrawTextW(draw.hDC, badge.c_str(), -1, &badge_bounds,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
      ::SetTextColor(draw.hDC,
                     settings_theme::GetColor(disabled ? COLOR_GRAYTEXT
                                                       : COLOR_WINDOWTEXT));
    }
    ::DrawTextW(draw.hDC, item.name.c_str(), -1, &name,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previous)
      ::SelectObject(draw.hDC, previous);
  }
  void DrawCustomButton(const DRAWITEMSTRUCT& draw) {
    const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
    const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
    ::FillRect(draw.hDC, &draw.rcItem, settings_theme::GetBrush(COLOR_WINDOW));
    if (draw.CtlID == kReset) {
      ::SetTextColor(draw.hDC, accent);
      ::SetBkMode(draw.hDC, TRANSPARENT);
      const HGDIOBJ previous =
          font_ ? ::SelectObject(draw.hDC, font_) : nullptr;
      RECT label = draw.rcItem;
      ::DrawTextW(draw.hDC, L"恢复默认", -1, &label,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT);
      if (previous)
        ::SelectObject(draw.hDC, previous);
      return;
    }
    const bool category = draw.CtlID >= kCategory && draw.CtlID < kCategory + 5;
    const bool active = category && int(draw.CtlID - kCategory) == group_;
    const COLORREF fill =
        active ? settings_navigation::Mix(accent, surface, 20)
               : settings_navigation::Mix(
                     settings_theme::GetColor(COLOR_BTNFACE), surface, 30);
    const COLORREF border =
        active ? accent
               : settings_navigation::Mix(
                     settings_theme::GetColor(COLOR_3DSHADOW), surface, 72);
    Gdiplus::Graphics graphics(draw.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const auto& bounds = draw.rcItem;
    Gdiplus::GraphicsPath path;
    settings_navigation::AddControlPath(
        path,
        Gdiplus::RectF(float(bounds.left) + 0.5f, float(bounds.top) + 0.5f,
                       float(bounds.right - bounds.left - 1),
                       float(bounds.bottom - bounds.top - 1)),
        float(Scale(5)), !category || draw.CtlID == kCategory,
        !category || draw.CtlID == kCategory + 4);
    Gdiplus::SolidBrush brush(settings_navigation::GdiPlusColor(fill));
    Gdiplus::Pen outline(settings_navigation::GdiPlusColor(border), 1.0f);
    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&outline, &path);
    if (draw.CtlID == kPick) {
      graphics.SetInterpolationMode(
          Gdiplus::InterpolationModeHighQualityBicubic);
      const int icon_size = Scale(24);
      const Gdiplus::Rect icon{(bounds.left + bounds.right - icon_size) / 2,
                               (bounds.top + bounds.bottom - icon_size) / 2,
                               icon_size, icon_size};
      const COLORREF hover_fill = settings_navigation::Mix(accent, surface, 25);
      DrawPipetteLayer(graphics, IDR_PIPETTE_LIGHT_MASK, hover_fill, icon);
      DrawPipetteLayer(graphics, IDR_PIPETTE_MAIN_MASK, accent, icon);
      return;
    }
    ::SetBkMode(draw.hDC, TRANSPARENT);
    ::SetTextColor(draw.hDC, settings_theme::GetColor(COLOR_WINDOWTEXT));
    const HGDIOBJ previous = font_ ? ::SelectObject(draw.hDC, font_) : nullptr;
    RECT label = draw.rcItem;
    ::DrawTextW(draw.hDC, candidate_palette::kGroups[draw.CtlID - kCategory],
                -1, &label, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    if (previous)
      ::SelectObject(draw.hDC, previous);
  }
  void PaintListFrame(HDC dc) const {
    if (!list_ || list_width_ <= 0)
      return;
    RECT frame{};
    if (!::GetWindowRect(list_, &frame))
      return;
    ::MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&frame), 2);
    const int edge = (std::max)(1, Scale(1));
    ::InflateRect(&frame, edge, edge);
    const COLORREF border =
        settings_navigation::Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                                 settings_theme::GetColor(COLOR_WINDOW), 70);
    HBRUSH brush = ::CreateSolidBrush(border);
    const RECT sides[] = {
        {frame.left, frame.top, frame.right, frame.top + edge},
        {frame.left, frame.top, frame.left + edge, frame.bottom},
        {frame.right - edge, frame.top, frame.right, frame.bottom},
        {frame.left, frame.bottom - edge, frame.right, frame.bottom}};
    for (const RECT& side : sides)
      ::FillRect(dc, &side, brush);
    ::DeleteObject(brush);
  }
  void PaintColumnDivider(HDC dc) const {
    RECT bounds{};
    ::GetClientRect(window_, &bounds);
    const int x = Scale(list_width_ + 8);
    const int top = Scale(36);
    const int bottom = (std::min)(int(bounds.bottom) - Scale(4), Scale(324));
    if (bottom <= top)
      return;
    const COLORREF color =
        settings_navigation::Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                                 settings_theme::GetColor(COLOR_WINDOW), 70);
    HBRUSH brush = ::CreateSolidBrush(color);
    RECT line{x, top, x + 1, bottom};
    ::FillRect(dc, &line, brush);
    ::DeleteObject(brush);
  }
  void DrawRole(const DRAWITEMSTRUCT& draw) {
    if (draw.itemID >= list_roles_.size())
      return;
    const int entry = list_roles_[draw.itemID];
    const auto surface = settings_theme::GetColor(COLOR_WINDOW);
    const auto accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
    const auto index = static_cast<size_t>(entry);
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    const COLORREF row_surface =
        selected ? settings_navigation::Mix(accent, surface, 25) : surface;
    HBRUSH brush = ::CreateSolidBrush(row_surface);
    ::FillRect(draw.hDC, &draw.rcItem, brush);
    ::DeleteObject(brush);
    if (selected) {
      RECT marker{draw.rcItem.left + Scale(2), draw.rcItem.top + Scale(5),
                  draw.rcItem.left + Scale(5), draw.rcItem.bottom - Scale(5)};
      brush = ::CreateSolidBrush(accent);
      ::FillRect(draw.hDC, &marker, brush);
      ::DeleteObject(brush);
    }
    const uint32_t rgba = draft_.colors[Slot()][index];
    RECT swatch{draw.rcItem.left + Scale(9), draw.rcItem.top + Scale(5),
                draw.rcItem.left + Scale(27), draw.rcItem.bottom - Scale(5)};
    DrawSwatch(draw.hDC, swatch, rgba);
    const COLORREF swatch_border = settings_navigation::Mix(
        settings_theme::GetColor(COLOR_WINDOWTEXT), row_surface, 60);
    brush = ::CreateSolidBrush(swatch_border);
    RECT swatch_frame{swatch.left - 1, swatch.top - 1, swatch.right + 1,
                      swatch.bottom + 1};
    ::FrameRect(draw.hDC, &swatch_frame, brush);
    ::DeleteObject(brush);
    ::SetBkMode(draw.hDC, TRANSPARENT);
    ::SetTextColor(draw.hDC, settings_theme::GetColor(COLOR_WINDOWTEXT));
    RECT name{swatch.right + Scale(8), draw.rcItem.top,
              draw.rcItem.right - Scale(78), draw.rcItem.bottom};
    ::DrawTextW(draw.hDC, candidate_palette::kRoles[index].label, -1, &name,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if ((entry == 16 || entry == 17) && PagerMismatch()) {
      const wchar_t marker[] = L"!";
      RECT mark = WarningRect(draw.hDC, entry, draw.rcItem);
      ::SetTextColor(draw.hDC, accent);
      ::DrawTextW(draw.hDC, marker, -1, &mark,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
    RECT value{draw.rcItem.right - Scale(76), draw.rcItem.top,
               draw.rcItem.right - Scale(6), draw.rcItem.bottom};
    auto hex = Hex(rgba);
    ::DrawTextW(draw.hDC, hex.c_str(), -1, &value,
                DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    const COLORREF separator = settings_navigation::Mix(
        settings_theme::GetColor(COLOR_3DSHADOW), row_surface, 38);
    brush = ::CreateSolidBrush(separator);
    RECT line{draw.rcItem.left + Scale(8), draw.rcItem.bottom - 1,
              draw.rcItem.right - Scale(8), draw.rcItem.bottom};
    ::FillRect(draw.hDC, &line, brush);
    ::DeleteObject(brush);
  }
  static void DrawSwatch(HDC dc, RECT rect, uint32_t rgba) {
    const int size = (std::max)(2, int((rect.bottom - rect.top) / 3));
    const COLORREF light = RGB(255, 255, 255), dark = RGB(220, 220, 220);
    const int alpha = rgba & 255;
    for (int y = rect.top; y < rect.bottom; y += size) {
      for (int x = rect.left; x < rect.right; x += size) {
        RECT tile{x, y, (std::min)(x + size, int(rect.right)),
                  (std::min)(y + size, int(rect.bottom))};
        const COLORREF checker =
            ((x - rect.left) / size + (y - rect.top) / size) % 2 ? dark : light;
        HBRUSH brush = ::CreateSolidBrush(Mix(Rgb(rgba), checker, alpha));
        ::FillRect(dc, &tile, brush);
        ::DeleteObject(brush);
      }
    }
  }
  void DrawSlider(HWND slider, HDC dc) {
    RECT area{};
    ::GetClientRect(slider, &area);
    const int saved_dc = ::SaveDC(dc);
    ::IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    HBRUSH background = settings_theme::GetBrush(COLOR_WINDOW);
    ::FillRect(dc, &area, background);
    const int id = ::GetDlgCtrlID(slider);
    const int channel = id - kChannelSlider;
    const bool alpha = id == kAlphaSlider;
    auto values = settings_color::Values(Rgb(Value()), model_);
    const int maximum = alpha ? 100 : Limit(channel);
    const double current =
        alpha ? (Value() & 255) * 100.0 / 255 : values[channel];
    const int width = int(area.right - area.left);
    const int track_left = Scale(5);
    const int track_right = (std::max)(track_left, width - Scale(5) - 1);
    for (int x = track_left; x <= track_right; ++x) {
      const double value = (x - track_left) * double(maximum) /
                           (std::max)(1, track_right - track_left);
      settings_color::Color color = Rgb(Value());
      if (alpha) {
        const COLORREF checker =
            (x / Scale(6)) % 2 ? RGB(235, 235, 235) : RGB(255, 255, 255);
        const COLORREF mixed =
            Mix(color, checker, int(std::lround(value * 255 / 100)));
        color = {int(GetRValue(mixed)), int(GetGValue(mixed)),
                 int(GetBValue(mixed))};
      } else {
        auto sample = values;
        sample[channel] = value;
        const auto converted = settings_color::FromValues(model_, sample);
        if (converted)
          color = *converted;
      }
      HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(color.r, color.g, color.b));
      HGDIOBJ previous = ::SelectObject(dc, pen);
      ::MoveToEx(dc, x, Scale(7), nullptr);
      ::LineTo(dc, x, Scale(12));
      ::SelectObject(dc, previous);
      ::DeleteObject(pen);
    }
    const COLORREF track_border =
        settings_navigation::Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                                 settings_theme::GetColor(COLOR_WINDOW), 70);
    HBRUSH border_brush = ::CreateSolidBrush(track_border);
    RECT track_frame{track_left - 1, Scale(7) - 1, track_right + 2,
                     Scale(12) + 1};
    ::FrameRect(dc, &track_frame, border_brush);
    ::DeleteObject(border_brush);
    const int point =
        track_left +
        int(std::lround(current / maximum * (track_right - track_left)));
    POINT triangle[] = {{point, Scale(10)},
                        {point - Scale(4), Scale(19)},
                        {point + Scale(4), Scale(19)}};
    HBRUSH brush = ::CreateSolidBrush(RGB(255, 255, 255));
    const COLORREF outline =
        settings_navigation::Mix(settings_theme::GetColor(COLOR_WINDOWTEXT),
                                 settings_theme::GetColor(COLOR_WINDOW), 115);
    HPEN pen = ::CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ old_brush = ::SelectObject(dc, brush);
    HGDIOBJ old_pen = ::SelectObject(dc, pen);
    ::Polygon(dc, triangle, 3);
    ::SelectObject(dc, old_pen);
    ::SelectObject(dc, old_brush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
    ::RestoreDC(dc, saved_dc);
  }
  static LRESULT CALLBACK SliderProc(HWND window,
                                     UINT message,
                                     WPARAM w,
                                     LPARAM l,
                                     UINT_PTR,
                                     DWORD_PTR data) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(data);
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{};
      HDC dc = ::BeginPaint(window, &paint);
      self->DrawSlider(window, dc);
      ::EndPaint(window, &paint);
      return 0;
    }
    if (message == WM_LBUTTONDOWN)
      ::SetCapture(window);
    if (message == WM_LBUTTONDOWN ||
        (message == WM_MOUSEMOVE && ::GetCapture() == window) ||
        message == WM_LBUTTONUP) {
      if (message == WM_LBUTTONUP && ::GetCapture() == window)
        ::ReleaseCapture();
      self->UpdateSlider(window, GET_X_LPARAM(l));
      return 0;
    }
    if (message == WM_NCDESTROY)
      ::RemoveWindowSubclass(window, SliderProc, 81);
    return ::DefSubclassProc(window, message, w, l);
  }
  void ShowPicker() {
    if (!popup_ || !popup_host_)
      return;
    picker_.Set(Rgb(Value()), true);
    ::ShowWindow(::GetDlgItem(popup_, 40100), SW_HIDE);
    PositionPicker();
    ::SetWindowPos(popup_host_, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetTimer(popup_host_, 1, 100, nullptr);
    ::RedrawWindow(
        popup_host_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }
  void OnCommand(WORD id, WORD notification) {
    if (!syncing_ && id != kSwatch && id != kPick && picker_visible())
      HidePicker();
    if (id == kSource && notification == CBN_SELCHANGE && !syncing_) {
      SwitchSource(static_cast<int>(
          ::SendDlgItemMessageW(window_, kSource, CB_GETCURSEL, 0, 0)));
      return;
    }
    if (id == kName && notification == EN_CHANGE && !syncing_) {
      draft_.name = Text(::GetDlgItem(window_, kName));
      name_slot_ = Slot();
      for (size_t slot = 0; slot < 4; ++slot) {
        if (!NameAffects(slot))
          continue;
        draft_.changed[slot] = draft_.colors[slot] != saved_colors_[slot] ||
                               draft_.name != saved_name_;
        if (changed_)
          changed_(slot);
      }
      return;
    }
    if ((id == kLight || id == kDark) && notification == BN_CLICKED) {
      SetTheme(id == kDark);
      if (changed_)
        changed_(Slot());
      return;
    }
    if (id >= kCategory && id < kCategory + 5 && notification == BN_CLICKED) {
      group_ = id - kCategory;
      for (size_t i = 0; i < candidate_palette::kRoles.size(); ++i) {
        if (candidate_palette::kRoles[i].group == group_) {
          role_ = static_cast<int>(i);
          break;
        }
      }
      RebuildRoleList();
      Refresh();
      if (selected_)
        selected_(Slot(), role_);
      return;
    }
    if (id == kList && notification == LBN_SELCHANGE) {
      const int selected = int(::SendMessageW(list_, LB_GETCURSEL, 0, 0));
      if (selected >= 0 && selected < int(list_roles_.size()) &&
          list_roles_[selected] >= 0) {
        role_ = list_roles_[selected];
        group_ = candidate_palette::kRoles[role_].group;
        Refresh();
        if (selected_)
          selected_(Slot(), role_);
      } else
        ::SendMessageW(list_, LB_SETCURSEL, CurrentRoleRow(), 0);
      return;
    }
    if (id == kList && notification == LBN_DBLCLK) {
      if (staged_preview_)
        return;
      POINT cursor{};
      ::GetCursorPos(&cursor);
      ::ScreenToClient(list_, &cursor);
      const DWORD item = static_cast<DWORD>(::SendMessageW(
          list_, LB_ITEMFROMPOINT, 0, MAKELPARAM(cursor.x, cursor.y)));
      const int index = LOWORD(item);
      if (!HIWORD(item) && index >= 0 && index < int(list_roles_.size()) &&
          list_roles_[index] >= 0) {
        RECT row{};
        ::SendMessageW(list_, LB_GETITEMRECT, index,
                       reinterpret_cast<LPARAM>(&row));
        if (cursor.x >= row.left + Scale(9) &&
            cursor.x <= row.left + Scale(27)) {
          role_ = list_roles_[index];
          group_ = candidate_palette::kRoles[role_].group;
          Refresh();
          ShowPicker();
          if (selected_)
            selected_(Slot(), role_);
        }
      }
      return;
    }
    if (id == kModel && notification == CBN_SELCHANGE) {
      const int selected =
          int(::SendDlgItemMessageW(window_, kModel, CB_GETCURSEL, 0, 0));
      if (selected >= 0 && selected <= 4) {
        model_ = static_cast<settings_color::Model>(selected);
        Refresh();
      }
      return;
    }
    if (id == kCode && notification == EN_CHANGE && !syncing_) {
      auto parsed = Parse(Text(::GetDlgItem(window_, kCode)), Value());
      if (parsed)
        SetValue(*parsed);
      return;
    }
    if (id >= kChannelNumber && id < kChannelNumber + 4 &&
        notification == EN_CHANGE && !syncing_) {
      UpdateComponents();
      return;
    }
    if (id == kAlphaNumber && notification == EN_CHANGE && !syncing_) {
      const auto percent =
          settings_color::Number(Text(::GetDlgItem(window_, kAlphaNumber)));
      if (percent && *percent >= 0 && *percent <= 100 &&
          std::floor(*percent) == *percent) {
        updating_alpha_number_ = true;
        const auto alpha =
            static_cast<uint32_t>(std::lround(*percent * 255.0 / 100.0));
        SetValue((Value() & 0xffffff00u) | alpha);
        updating_alpha_number_ = false;
      }
      return;
    }
    if (id == kAlphaNumber && notification == EN_KILLFOCUS) {
      syncing_ = true;
      ::SetWindowTextW(
          ::GetDlgItem(window_, kAlphaNumber),
          std::to_wstring(int(std::lround((Value() & 255) * 100.0 / 255)))
              .c_str());
      syncing_ = false;
      return;
    }
    if (id == kReset && notification == BN_CLICKED) {
      const auto& role = candidate_palette::kRoles[role_];
      SetValue(dark_ ? role.dark : role.light);
      return;
    }
    if (id == kSwatch && notification == BN_CLICKED) {
      if (picker_visible())
        HidePicker();
      else
        ShowPicker();
      return;
    }
    if (id == kPick && notification == BN_CLICKED) {
      HidePicker();
      CancelScreenPick();
      WNDCLASSW type{};
      type.hInstance = ::GetModuleHandleW(nullptr);
      type.lpfnWndProc = ScreenOverlayProc;
      type.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
      type.lpszClassName = L"Weasel.ScreenColorPicker";
      ::RegisterClassW(&type);
      screen_overlay_ = ::CreateWindowExW(
          WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
              WS_EX_NOREDIRECTIONBITMAP,
          type.lpszClassName, L"", WS_POPUP,
          ::GetSystemMetrics(SM_XVIRTUALSCREEN),
          ::GetSystemMetrics(SM_YVIRTUALSCREEN),
          ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
          ::GetSystemMetrics(SM_CYVIRTUALSCREEN),
          ::GetAncestor(window_, GA_ROOT), nullptr, type.hInstance, this);
      if (!screen_overlay_)
        return;
      picking_ = true;
      ::SetWindowPos(screen_overlay_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      ::SetTimer(screen_overlay_, 1, 100, nullptr);
      PreviewScreenColor();
      ::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));
    }
  }
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<CandidatePaletteEditor*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<CandidatePaletteEditor*>(
          reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      ::SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
      return ::DefWindowProcW(window, message, w, l);
    if (message == WM_COMMAND) {
      self->OnCommand(LOWORD(w), HIWORD(w));
      return 0;
    }
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{};
      HDC dc = ::BeginPaint(window, &paint);
      RECT bounds{};
      ::GetClientRect(window, &bounds);
      ::FillRect(dc, &bounds, settings_theme::GetBrush(COLOR_WINDOW));
      self->PaintListFrame(dc);
      self->PaintColumnDivider(dc);
      ::EndPaint(window, &paint);
      return 0;
    }
    if (message == WM_MEASUREITEM) {
      auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
      if (measure->CtlID == kList) {
        measure->itemHeight = self->Scale(kRoleHeight);
        return TRUE;
      }
      if (measure->CtlID == kModel || measure->CtlID == kSource) {
        measure->itemHeight = self->Scale(26);
        return TRUE;
      }
    }
    if (message == WM_DRAWITEM) {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l);
      if (draw->CtlID == kList) {
        self->DrawRole(*draw);
        return TRUE;
      }
      if (draw->CtlID == kSource) {
        self->DrawSourceItem(*draw);
        return TRUE;
      }
      if (draw->CtlID == kModel) {
        settings_navigation::DrawComboItem(*draw);
        return TRUE;
      }
      if (draw->CtlID == kSwatch) {
        ::FillRect(draw->hDC, &draw->rcItem,
                   settings_theme::GetBrush(COLOR_WINDOW));
        RECT swatch = draw->rcItem;
        ::InflateRect(&swatch, -self->Scale(3), -self->Scale(3));
        DrawSwatch(draw->hDC, swatch,
                   self->pick_preview_.value_or(self->Value()));
        const COLORREF border = settings_navigation::Mix(
            settings_theme::GetColor(COLOR_WINDOWTEXT),
            settings_theme::GetColor(COLOR_WINDOW), 60);
        HBRUSH brush = ::CreateSolidBrush(border);
        RECT frame{swatch.left - 1, swatch.top - 1, swatch.right + 1,
                   swatch.bottom + 1};
        ::FrameRect(draw->hDC, &frame, brush);
        ::DeleteObject(brush);
        return TRUE;
      }
      if (draw->CtlID == kReset || draw->CtlID == kPick ||
          (draw->CtlID >= kCategory && draw->CtlID < kCategory + 5)) {
        self->DrawCustomButton(*draw);
        return TRUE;
      }
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
      HDC dc = reinterpret_cast<HDC>(w);
      ::SetBkMode(dc, TRANSPARENT);
      ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
      return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
    }
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
      HDC dc = reinterpret_cast<HDC>(w);
      ::SetBkColor(dc, settings_theme::GetColor(COLOR_WINDOW));
      ::SetTextColor(dc, settings_theme::GetColor(COLOR_WINDOWTEXT));
      return reinterpret_cast<LRESULT>(settings_theme::GetBrush(COLOR_WINDOW));
    }
    if (message == WM_ERASEBKGND) {
      RECT bounds{};
      ::GetClientRect(window, &bounds);
      ::FillRect(reinterpret_cast<HDC>(w), &bounds,
                 settings_theme::GetBrush(COLOR_WINDOW));
      return TRUE;
    }
    if (message == WM_LBUTTONDOWN && !self->picking_ && self->picker_visible())
      self->HidePicker();
    if (self->picking_ && message == WM_KEYDOWN && w == VK_ESCAPE) {
      self->CancelScreenPick();
      return 0;
    }
    if (message == WM_SETCURSOR && self->picking_) {
      ::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));
      return TRUE;
    }
    if (message == WM_NCDESTROY) {
      self->CancelScreenPick();
      self->HideWarningTip();
      if (self->warning_tip_) {
        ::DestroyWindow(self->warning_tip_);
        self->warning_tip_ = nullptr;
      }
      if (self->popup_host_) {
        ::DestroyWindow(self->popup_host_);
        self->popup_host_ = nullptr;
        self->popup_ = nullptr;
      }
    }
    return ::DefWindowProcW(window, message, w, l);
  }
};
