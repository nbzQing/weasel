#pragma once

#include "resource.h"
#include "SettingsNavigation.h"

#include <array>
#include <string>
#include <vector>

class KeySettingsDialog
    : public settings_navigation::HostedDialogImpl<KeySettingsDialog> {
 public:
  enum { IDD = IDD_KEY_SETTINGS };

  bool HasUnappliedChanges() const;
  bool ApplyChanges();
  bool PrepareForDisplay() { return true; }

  BEGIN_MSG_MAP(KeySettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
  MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnStaticColor)
  COMMAND_RANGE_HANDLER(IDC_KEY_SWITCH_COMBO_BASE,
                        IDC_KEY_SWITCH_COMBO_BASE + 4,
                        OnSwitchChanged)
  COMMAND_ID_HANDLER(IDC_KEY_PAGE_UP_EDIT, OnCaptureClicked)
  COMMAND_ID_HANDLER(IDC_KEY_PAGE_DOWN_EDIT, OnCaptureClicked)
  COMMAND_ID_HANDLER(IDC_KEY_APPLY, OnApply)
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
  LRESULT OnSwitchChanged(WORD, WORD, HWND, BOOL&);
  LRESULT OnCaptureClicked(WORD, WORD, HWND, BOOL&);
  LRESULT OnApply(WORD, WORD, HWND, BOOL&);
  LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
  LRESULT OnNavigate(WORD, WORD, HWND, BOOL&);

  static LRESULT CALLBACK
      CaptureProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
  void CreateControls();
  bool LoadValues();
  void ShowValues();
  void RefreshState();
  void CaptureKey(WORD id, WPARAM key);
  void StopCapture();
  bool Persist();

  std::wstring LocalText(const wchar_t* zh,
                         const wchar_t* tw,
                         const wchar_t* en) const;
  std::array<std::string, 5> initial_switches_{};
  std::array<std::string, 5> switches_{};
  std::array<std::string, 2> initial_paging_{};
  std::array<std::string, 2> paging_{};
  std::vector<std::string> selected_schemas_;
  WORD capture_id_ = 0;
  bool updating_ = false;
};
