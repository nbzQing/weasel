#pragma once

#include "resource.h"
#include "SettingsNavigation.h"

#include <array>
#include <string>

class UIStyleSettings;

class LayoutEffectsSettingsDialog
    : public settings_navigation::HostedDialogImpl<
          LayoutEffectsSettingsDialog> {
 public:
  enum { IDD = IDD_LAYOUT_EFFECTS_SETTING };
  explicit LayoutEffectsSettingsDialog(UIStyleSettings* settings)
      : settings_(settings) {}

  bool PrepareForDisplay();
  bool HasUnappliedChanges() const;
  bool CanApply() const { return valid_; }
  bool ApplyChanges();

  BEGIN_MSG_MAP(LayoutEffectsSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnStaticColor)
  MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnEditColor)
  MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
  MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
  COMMAND_RANGE_HANDLER(IDC_LAYOUT_VALUE_BASE,
                        IDC_LAYOUT_VALUE_BASE + 19,
                        OnValueChanged)
  COMMAND_RANGE_HANDLER(IDC_LAYOUT_GROUP_FRAME,
                        IDC_LAYOUT_GROUP_EFFECTS,
                        OnGroupChanged)
  COMMAND_RANGE_HANDLER(IDC_LAYOUT_CATEGORY_WINDOW,
                        IDC_LAYOUT_CATEGORY_LAYOUT,
                        OnCategoryChanged)
  COMMAND_RANGE_HANDLER(IDC_LAYOUT_WINDOW_BOOL_BASE,
                        IDC_LAYOUT_WINDOW_BOOL_BASE + 5,
                        OnStyleBoolChanged)
  COMMAND_ID_HANDLER(IDC_LAYOUT_PREEDIT_INLINE, OnStyleBoolChanged)
  COMMAND_RANGE_HANDLER(IDC_LAYOUT_OTHER_BOOL_BASE,
                        IDC_LAYOUT_OTHER_BOOL_BASE + 4,
                        OnStyleBoolChanged)
  COMMAND_RANGE_HANDLER(IDC_LAYOUT_PREEDIT_TYPE,
                        IDC_LAYOUT_HOVER_TYPE,
                        OnStyleEnumChanged)
  COMMAND_ID_HANDLER(IDC_LAYOUT_LABEL_FORMAT, OnStyleTextChanged)
  COMMAND_ID_HANDLER(IDC_LAYOUT_MARK_TEXT, OnStyleTextChanged)
  COMMAND_ID_HANDLER(IDC_LAYOUT_ABBREVIATE_LENGTH, OnStyleTextChanged)
  COMMAND_ID_HANDLER(IDC_LAYOUT_PAGE_SIZE, OnValueChanged)
  COMMAND_ID_HANDLER(IDC_LAYOUT_RESTORE, OnRestore)
  COMMAND_ID_HANDLER(IDC_LAYOUT_APPLY, OnApply)
  COMMAND_ID_HANDLER(IDC_LAYOUT_PREVIEW_HORIZONTAL, OnPreviewMode)
  COMMAND_ID_HANDLER(IDC_LAYOUT_PREVIEW_VERTICAL, OnPreviewMode)
  COMMAND_ID_HANDLER(IDC_LAYOUT_TYPE, OnLayoutType)
  COMMAND_ID_HANDLER(IDC_LAYOUT_ALIGN, OnAlignType)
  COMMAND_ID_HANDLER(IDC_LAYOUT_TEXT_LEFT_TO_RIGHT, OnWindowOption)
  COMMAND_ID_HANDLER(IDC_LAYOUT_TEXT_WRAP, OnWindowOption)
  COMMAND_ID_HANDLER(IDC_LAYOUT_AUTO_REVERSE, OnWindowOption)
  COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
  COMMAND_RANGE_HANDLER(settings_navigation::kInput,
                        settings_navigation::kStatusIcons,
                        OnNavigate)
  END_MSG_MAP()

 private:
  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMeasureItem(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnStaticColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnEditColor(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnValueChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnGroupChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnCategoryChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnStyleBoolChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnStyleEnumChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnStyleTextChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnRestore(WORD, WORD, HWND, BOOL&);
  LRESULT OnApply(WORD, WORD, HWND, BOOL&);
  LRESULT OnPreviewMode(WORD, WORD, HWND, BOOL&);
  LRESULT OnLayoutType(WORD, WORD, HWND, BOOL&);
  LRESULT OnAlignType(WORD, WORD, HWND, BOOL&);
  LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnWindowOption(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD, HWND, BOOL&);

  void CreateControls();
  bool LoadValues();
  void ShowValues();
  void ReadValues();
  void ShowGroup();
  void UpdateDependencies();
  void RefreshState();
  void RefreshPreview();
  void DrawPreview(const DRAWITEMSTRUCT& draw, bool dark);
  std::wstring LocalText(const wchar_t* zh,
                         const wchar_t* tw,
                         const wchar_t* en) const;

  UIStyleSettings* settings_ = nullptr;
  std::array<int, 20> initial_{};
  std::array<int, 20> draft_{};
  bool updating_ = false;
  bool valid_ = true;
  bool horizontal_ = false;
  bool initial_horizontal_ = false;
  bool inherited_vertical_text_ = false;
  bool inherited_fullscreen_ = false;
  std::array<bool, 6> window_values_{};
  std::array<bool, 6> initial_window_values_{};
  bool inline_preedit_ = false;
  bool initial_inline_preedit_ = false;
  std::string preedit_type_ = "composition";
  std::string initial_preedit_type_ = "composition";
  std::wstring label_format_ = L"%s";
  std::wstring initial_label_format_ = L"%s";
  std::wstring mark_text_;
  std::wstring initial_mark_text_;
  std::array<bool, 5> other_values_{};
  std::array<bool, 5> initial_other_values_{};
  std::string antialias_mode_ = "default";
  std::string initial_antialias_mode_ = "default";
  int candidate_abbreviate_length_ = 0;
  int initial_candidate_abbreviate_length_ = 0;
  int page_size_ = 6;
  int initial_page_size_ = 6;
  std::string hover_type_ = "none";
  std::string initial_hover_type_ = "none";
  std::string layout_type_;
  std::string initial_layout_type_;
  std::string align_type_ = "center";
  std::string initial_align_type_ = "center";
  std::array<bool, 3> window_options_{};
  std::array<bool, 3> initial_window_options_{};
  int active_group_ = 0;
  int group_scroll_[4] = {0, 0, 0, 0};
  int wheel_delta_ = 0;
  bool needs_deploy_ = false;
};
