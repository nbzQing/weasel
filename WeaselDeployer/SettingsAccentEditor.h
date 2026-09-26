#pragma once

#include "SettingsColorPicker.h"
#include "resource.h"
#include <windowsx.h>
#include <cstring>
#include <memory>

// The single-colour variant of the candidate palette editor.  The settings
// accent is opaque, so the palette's opacity row and scheme controls are not
// applicable here.  The colour controls and the large plane use the same
// conversions and picker as the palette page.
namespace settings_color {
class AccentEditor {
 public:
  using Changed = std::function<void(Color)>;
  HWND window() const { return window_; }
  bool Contains(HWND child) const {
    return child == popup_host_ || child == picker_.window() ||
           (popup_host_ && ::IsChild(popup_host_, child)) ||
           child == screen_overlay_;
  }
  bool HandleEscape() {
    if (screen_overlay_) {
      CancelScreenPick();
      return true;
    }
    if (picker_visible()) {
      HidePicker();
      return true;
    }
    return false;
  }
  void Create(HWND parent,
              HFONT font,
              int x,
              int y,
              int width,
              UINT dpi,
              Changed changed) {
    font_ = font;
    dpi_ = dpi;
    changed_ = std::move(changed);
    WNDCLASSW type{};
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.lpfnWndProc = Proc;
    type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = L"Weasel.SettingsAccentEditor";
    ::RegisterClassW(&type);
    window_ =
        ::CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"",
                          WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, x, y, width,
                          Scale(328), parent, nullptr, type.hInstance, this);
  }
  void Set(Color color, bool editable) {
    const bool changed = !(color == color_);
    color_ = color;
    editable_ = editable;
    if (window_) {
      ::EnableWindow(window_, editable);
      Refresh();
      if (picker_visible() && changed)
        picker_.Set(color_, true);
      if (!editable) {
        HidePicker();
        CancelScreenPick();
      }
    }
  }

 private:
  static constexpr int kSwatch = 40300, kPick = 40301, kModel = 40302,
                       kCode = 40303, kChannelLabel = 40310,
                       kChannelLetter = 40320, kChannelSlider = 40330,
                       kChannelNumber = 40340;
  HWND window_ = nullptr, popup_host_ = nullptr, screen_overlay_ = nullptr;
  HFONT font_ = nullptr;
  UINT dpi_ = 96;
  Color color_{};
  Color preview_{};
  std::optional<Color> pending_pick_;
  bool editable_ = false, syncing_ = false, picking_ = false;
  Model model_ = Model::Rgb;
  Changed changed_;
  Picker picker_;
  int Scale(int value) const { return ::MulDiv(value, dpi_, 96); }
  int Count() const {
    return model_ == Model::Hex ? 0 : model_ == Model::Cmyk ? 4 : 3;
  }
  int Limit(int i) const {
    return model_ == Model::Rgb              ? 255
           : i == 0 && model_ != Model::Cmyk ? 360
                                             : 100;
  }
  bool picker_visible() const {
    return popup_host_ && ::IsWindowVisible(popup_host_);
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
  static std::wstring Text(HWND control) {
    wchar_t value[256]{};
    ::GetWindowTextW(control, value, _countof(value));
    return value;
  }
  void SetColor(Color value) {
    if (!editable_ || color_ == value)
      return;
    color_ = value;
    Refresh();
    if (changed_)
      changed_(value);
  }
  void Refresh() {
    if (!window_)
      return;
    syncing_ = true;
    ::SetDlgItemTextW(window_, kCode, Hex(color_).c_str());
    const auto values = Values(color_, model_);
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
        RECT left{}, right{};
        ::GetWindowRect(::GetDlgItem(window_, kChannelLabel + i), &left);
        ::GetWindowRect(::GetDlgItem(window_, kChannelNumber + i), &right);
        ::MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&left), 2);
        ::MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&right),
                          2);
        RECT row{left.left, left.top, right.right, right.bottom};
        ::InflateRect(&row, Scale(2), Scale(2));
        if (repaint_exposed_rows)
          ::UnionRect(&exposed_rows, &exposed_rows, &row);
        else
          exposed_rows = row;
        repaint_exposed_rows = true;
      }
      if (!visible)
        continue;
      ::SetDlgItemTextW(window_, kChannelLabel + i,
                        labels[static_cast<int>(model_)][i]);
      ::SetDlgItemTextW(window_, kChannelLetter + i,
                        letters[static_cast<int>(model_)][i]);
      ::SetDlgItemTextW(window_, kChannelNumber + i,
                        std::to_wstring(int(std::lround(values[i]))).c_str());
      ::InvalidateRect(::GetDlgItem(window_, kChannelSlider + i), nullptr,
                       FALSE);
    }
    if (repaint_exposed_rows)
      ::RedrawWindow(window_, &exposed_rows, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    ::InvalidateRect(::GetDlgItem(window_, kSwatch), nullptr, FALSE);
    syncing_ = false;
  }
  void UpdateComponents() {
    if (syncing_ || !editable_)
      return;
    std::array<double, 4> values{};
    for (int i = 0; i < Count(); ++i) {
      const auto number =
          Number(Text(::GetDlgItem(window_, kChannelNumber + i)));
      if (!number || *number < 0 || *number > Limit(i))
        return;
      values[i] = *number;
    }
    const auto color = FromValues(model_, values);
    if (color)
      SetColor(*color);
  }
  void UpdateSlider(HWND slider, int x) {
    if (!editable_)
      return;
    const int index = ::GetDlgCtrlID(slider) - kChannelSlider;
    if (index < 0 || index >= Count())
      return;
    RECT bounds{};
    ::GetClientRect(slider, &bounds);
    const int left = Scale(5),
              right = (std::max)(left, int(bounds.right) - left - 1);
    auto values = Values(color_, model_);
    values[index] = std::round(
        std::clamp(double(x - left) / (std::max)(1, right - left), 0.0, 1.0) *
        Limit(index));
    if (const auto converted = FromValues(model_, values))
      SetColor(*converted);
  }
  void DrawSlider(HWND slider, HDC dc) {
    RECT area{};
    ::GetClientRect(slider, &area);
    ::FillRect(dc, &area, settings_theme::GetBrush(COLOR_WINDOW));
    const int index = ::GetDlgCtrlID(slider) - kChannelSlider;
    auto values = Values(color_, model_);
    const int left = Scale(5),
              right = (std::max)(left, int(area.right) - left - 1);
    for (int x = left; x <= right; ++x) {
      auto sample = values;
      sample[index] =
          (x - left) * double(Limit(index)) / (std::max)(1, right - left);
      const Color color = FromValues(model_, sample).value_or(color_);
      HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(color.r, color.g, color.b));
      HGDIOBJ old = ::SelectObject(dc, pen);
      ::MoveToEx(dc, x, Scale(7), nullptr);
      ::LineTo(dc, x, Scale(12));
      ::SelectObject(dc, old);
      ::DeleteObject(pen);
    }
    const COLORREF edge =
        settings_navigation::Mix(settings_theme::GetColor(COLOR_3DSHADOW),
                                 settings_theme::GetColor(COLOR_WINDOW), 70);
    HBRUSH border = ::CreateSolidBrush(edge);
    RECT track{left - 1, Scale(6), right + 2, Scale(13)};
    ::FrameRect(dc, &track, border);
    ::DeleteObject(border);
    const int point =
        left + int(std::lround(values[index] / Limit(index) * (right - left)));
    POINT triangle[] = {{point, Scale(10)},
                        {point - Scale(4), Scale(19)},
                        {point + Scale(4), Scale(19)}};
    HBRUSH fill = ::CreateSolidBrush(RGB(255, 255, 255));
    HPEN outline = ::CreatePen(
        PS_SOLID, 1,
        settings_navigation::Mix(settings_theme::GetColor(COLOR_WINDOWTEXT),
                                 settings_theme::GetColor(COLOR_WINDOW), 115));
    HGDIOBJ old_fill = ::SelectObject(dc, fill);
    HGDIOBJ old_outline = ::SelectObject(dc, outline);
    ::Polygon(dc, triangle, 3);
    ::SelectObject(dc, old_outline);
    ::SelectObject(dc, old_fill);
    ::DeleteObject(outline);
    ::DeleteObject(fill);
  }
  static LRESULT CALLBACK SliderProc(HWND slider,
                                     UINT message,
                                     WPARAM w,
                                     LPARAM l,
                                     UINT_PTR,
                                     DWORD_PTR data) {
    auto* self = reinterpret_cast<AccentEditor*>(data);
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{};
      HDC dc = ::BeginPaint(slider, &paint);
      self->DrawSlider(slider, dc);
      ::EndPaint(slider, &paint);
      return 0;
    }
    if (message == WM_LBUTTONDOWN)
      ::SetCapture(slider);
    if (message == WM_LBUTTONDOWN ||
        (message == WM_MOUSEMOVE && ::GetCapture() == slider) ||
        message == WM_LBUTTONUP) {
      if (message == WM_LBUTTONUP && ::GetCapture() == slider)
        ::ReleaseCapture();
      self->UpdateSlider(slider, GET_X_LPARAM(l));
      return 0;
    }
    if (message == WM_NCDESTROY)
      ::RemoveWindowSubclass(slider, SliderProc, 91);
    return ::DefSubclassProc(slider, message, w, l);
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
  void DrawButton(const DRAWITEMSTRUCT& item) {
    const COLORREF surface = settings_theme::GetColor(COLOR_WINDOW);
    const COLORREF accent = settings_theme::GetColor(COLOR_HIGHLIGHT);
    ::FillRect(item.hDC, &item.rcItem, settings_theme::GetBrush(COLOR_WINDOW));
    if (item.CtlID == kSwatch) {
      const Color shown = picking_ ? preview_ : color_;
      RECT swatch = item.rcItem;
      ::InflateRect(&swatch, -2, -2);
      HBRUSH fill = ::CreateSolidBrush(RGB(shown.r, shown.g, shown.b));
      ::FillRect(item.hDC, &swatch, fill);
      ::DeleteObject(fill);
      HBRUSH edge = ::CreateSolidBrush(settings_navigation::Mix(
          settings_theme::GetColor(COLOR_3DSHADOW), surface, 72));
      ::FrameRect(item.hDC, &swatch, edge);
      ::DeleteObject(edge);
      return;
    }
    Gdiplus::Graphics graphics(item.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    const auto& b = item.rcItem;
    settings_navigation::AddControlPath(
        path,
        Gdiplus::RectF(float(b.left) + .5f, float(b.top) + .5f,
                       float(b.right - b.left - 1),
                       float(b.bottom - b.top - 1)),
        float(Scale(5)), true, true);
    Gdiplus::SolidBrush fill(
        settings_navigation::GdiPlusColor(settings_navigation::Mix(
            settings_theme::GetColor(COLOR_BTNFACE), surface, 30)));
    Gdiplus::Pen edge(
        settings_navigation::GdiPlusColor(settings_navigation::Mix(
            settings_theme::GetColor(COLOR_3DSHADOW), surface, 72)),
        1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&edge, &path);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    const int size = Scale(24);
    const Gdiplus::Rect icon{(b.left + b.right - size) / 2,
                             (b.top + b.bottom - size) / 2, size, size};
    DrawPipetteLayer(graphics, IDR_PIPETTE_LIGHT_MASK,
                     settings_navigation::Mix(accent, surface, 25), icon);
    DrawPipetteLayer(graphics, IDR_PIPETTE_MAIN_MASK, accent, icon);
  }
  void PositionPicker() {
    if (!window_ || !popup_host_)
      return;
    POINT point{0, Scale(30)};
    ::ClientToScreen(window_, &point);
    RECT previous{};
    if (::GetWindowRect(popup_host_, &previous) &&
        (point.x != previous.left || point.y != previous.top))
      ::SetWindowPos(popup_host_, nullptr, point.x, point.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  void ShowPicker() {
    if (!editable_ || !popup_host_)
      return;
    picker_.Set(color_, true);
    ::ShowWindow(::GetDlgItem(picker_.window(), 40100), SW_HIDE);
    PositionPicker();
    ::SetWindowPos(popup_host_, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetTimer(popup_host_, 1, 100, nullptr);
    ::RedrawWindow(
        popup_host_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }
  void HidePicker() {
    if (popup_host_) {
      ::KillTimer(popup_host_, 1);
      ::ShowWindow(popup_host_, SW_HIDE);
    }
  }
  static LRESULT CALLBACK PopupHostProc(HWND host,
                                        UINT message,
                                        WPARAM w,
                                        LPARAM l) {
    auto* self = reinterpret_cast<AccentEditor*>(
        ::GetWindowLongPtrW(host, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<AccentEditor*>(
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
    if (message == WM_NCDESTROY && self->popup_host_ == host)
      self->popup_host_ = nullptr;
    return ::DefWindowProcW(host, message, w, l);
  }
  std::optional<Color> ScreenColor(POINT point) const {
    HDC desktop = ::GetDC(nullptr);
    if (!desktop)
      return std::nullopt;
    const COLORREF pixel = ::GetPixel(desktop, point.x, point.y);
    ::ReleaseDC(nullptr, desktop);
    if (pixel == CLR_INVALID)
      return std::nullopt;
    return Color{int(GetRValue(pixel)), int(GetGValue(pixel)),
                 int(GetBValue(pixel))};
  }
  void PreviewScreenColor(POINT point) {
    if (const auto sample = ScreenColor(point)) {
      preview_ = *sample;
      ::RedrawWindow(::GetDlgItem(window_, kSwatch), nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW);
    }
  }
  void CancelScreenPick() {
    if (screen_overlay_) {
      ::KillTimer(screen_overlay_, 1);
      HWND overlay = screen_overlay_;
      screen_overlay_ = nullptr;
      ::DestroyWindow(overlay);
    }
    picking_ = false;
    pending_pick_.reset();
    if (window_)
      ::InvalidateRect(::GetDlgItem(window_, kSwatch), nullptr, FALSE);
  }
  static LRESULT CALLBACK ScreenOverlayProc(HWND overlay,
                                            UINT message,
                                            WPARAM w,
                                            LPARAM l) {
    auto* self = reinterpret_cast<AccentEditor*>(
        ::GetWindowLongPtrW(overlay, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<AccentEditor*>(
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
          self->SetColor(*picked);
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
    }
    return ::DefWindowProcW(overlay, message, w, l);
  }
  void StartScreenPick() {
    if (!editable_)
      return;
    HidePicker();
    CancelScreenPick();
    WNDCLASSW type{};
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.lpfnWndProc = ScreenOverlayProc;
    type.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
    type.lpszClassName = L"Weasel.AccentScreenColorPicker";
    ::RegisterClassW(&type);
    screen_overlay_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
            WS_EX_NOREDIRECTIONBITMAP,
        type.lpszClassName, L"", WS_POPUP,
        ::GetSystemMetrics(SM_XVIRTUALSCREEN),
        ::GetSystemMetrics(SM_YVIRTUALSCREEN),
        ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
        ::GetSystemMetrics(SM_CYVIRTUALSCREEN), ::GetAncestor(window_, GA_ROOT),
        nullptr, type.hInstance, this);
    if (!screen_overlay_)
      return;
    picking_ = true;
    ::SetWindowPos(screen_overlay_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetTimer(screen_overlay_, 1, 100, nullptr);
    POINT point{};
    if (::GetCursorPos(&point))
      PreviewScreenColor(point);
    ::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));
  }
  void Init() {
    Add(L"STATIC", L"自定义强调色", SS_CENTERIMAGE, 0, 0, 0, 100, 24);
    Add(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, kSwatch, 352, 0, 30, 24);
    Add(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, kPick, 386, 0, 34, 24);
    Add(L"STATIC", L"颜色模式", SS_CENTERIMAGE, 0, 0, 36, 58, 24);
    HWND model = Add(L"COMBOBOX", L"",
                     CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE | CBS_HASSTRINGS |
                         WS_VSCROLL | WS_TABSTOP,
                     kModel, 62, 36, 358, 120);
    for (const auto* label : {L"HEX", L"RGB", L"HSV", L"HSL", L"CMYK"})
      ::SendMessageW(model, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    ::SendMessageW(model, CB_SETCURSEL, 1, 0);
    settings_navigation::StyleCombo(window_, kModel);
    for (int i = 0; i < 4; ++i) {
      const int y = 72 + i * 36;
      Add(L"STATIC", L"", SS_CENTERIMAGE, kChannelLabel + i, 0, y, 54, 20);
      Add(L"STATIC", L"", SS_CENTERIMAGE | SS_RIGHT, kChannelLetter + i, 54, y,
          12, 20);
      HWND slider = Add(L"STATIC", L"", SS_NOTIFY | WS_TABSTOP,
                        kChannelSlider + i, 70, y, 294, 20);
      ::SetWindowSubclass(slider, SliderProc, 91,
                          reinterpret_cast<DWORD_PTR>(this));
      Add(L"EDIT", L"", ES_NUMBER | ES_CENTER | WS_TABSTOP, kChannelNumber + i,
          376, y, 44, 20);
    }
    Add(L"STATIC", L"颜色代码", SS_CENTERIMAGE, 0, 0, 232, 58, 24);
    Add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, kCode, 62, 232, 358, 24);
    WNDCLASSW host_type{};
    host_type.hInstance = ::GetModuleHandleW(nullptr);
    host_type.lpfnWndProc = PopupHostProc;
    host_type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    host_type.lpszClassName = L"Weasel.SettingsAccentPickerHost";
    ::RegisterClassW(&host_type);
    const int width = Scale(420), height = Scale(298);
    POINT position{0, Scale(30)};
    ::ClientToScreen(window_, &position);
    popup_host_ = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_COMPOSITED,
        host_type.lpszClassName, L"", WS_POPUP | WS_CLIPCHILDREN, position.x,
        position.y, width, height, ::GetAncestor(window_, GA_ROOT), nullptr,
        host_type.hInstance, this);
    picker_.Create(popup_host_, font_, 0, 0, width, dpi_,
                   [this](Color color) { SetColor(color); });
    picker_.SetHueHorizontal(true);
    HWND picker_window = picker_.window();
    for (int id : {40100, 40103, 40104, 40105, 40106, 40110, 40111, 40112,
                   40113, 40120, 40121, 40122, 40123})
      ::ShowWindow(::GetDlgItem(picker_window, id), SW_HIDE);
    const int margin = Scale(6), gap = Scale(8), hue_height = Scale(20);
    const int surface_width = width - 2 * margin;
    const int plane_height = height - 2 * margin - gap - hue_height;
    ::SetWindowPos(::GetDlgItem(picker_window, 40101), nullptr, margin, margin,
                   surface_width, plane_height, SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(::GetDlgItem(picker_window, 40102), nullptr, margin,
                   margin + plane_height + gap, surface_width, hue_height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowLongPtrW(
        picker_window, GWL_STYLE,
        ::GetWindowLongPtrW(picker_window, GWL_STYLE) | WS_CLIPSIBLINGS);
    ::SetWindowPos(picker_window, HWND_TOP, 0, 0, width, height,
                   SWP_FRAMECHANGED | SWP_NOACTIVATE);
    ::ShowWindow(popup_host_, SW_HIDE);
    Refresh();
  }
  static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<AccentEditor*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<AccentEditor*>(
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
      case WM_COMMAND: {
        const int id = LOWORD(w), notice = HIWORD(w);
        if (id == kSwatch && notice == BN_CLICKED) {
          if (self->picker_visible())
            self->HidePicker();
          else
            self->ShowPicker();
          return 0;
        }
        if (id == kPick && notice == BN_CLICKED) {
          self->StartScreenPick();
          return 0;
        }
        if (id == kModel && notice == CBN_SELCHANGE) {
          self->model_ = static_cast<Model>(
              ::SendDlgItemMessageW(window, kModel, CB_GETCURSEL, 0, 0));
          self->Refresh();
          return 0;
        }
        if (self->syncing_)
          return 0;
        if (id == kCode && notice == EN_CHANGE) {
          if (auto parsed = Parse(Text(::GetDlgItem(window, kCode))))
            self->SetColor(*parsed);
          return 0;
        }
        if (id >= kChannelNumber && id < kChannelNumber + 4 &&
            notice == EN_CHANGE) {
          self->UpdateComponents();
          return 0;
        }
        break;
      }
      case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = self->Scale(28);
        return TRUE;
      case WM_DRAWITEM: {
        const auto& draw = *reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if (draw.CtlID == kModel)
          settings_navigation::DrawComboItem(draw);
        else if (draw.CtlID == kSwatch || draw.CtlID == kPick)
          self->DrawButton(draw);
        return TRUE;
      }
      case WM_ERASEBKGND: {
        RECT area{};
        ::GetClientRect(window, &area);
        ::FillRect(reinterpret_cast<HDC>(w), &area,
                   settings_theme::GetBrush(COLOR_WINDOW));
        return TRUE;
      }
      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORLISTBOX:
        ::SetTextColor(reinterpret_cast<HDC>(w),
                       settings_theme::GetColor(COLOR_WINDOWTEXT));
        ::SetBkColor(reinterpret_cast<HDC>(w),
                     settings_theme::GetColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(
            settings_theme::GetBrush(COLOR_WINDOW));
      case WM_DESTROY:
        self->CancelScreenPick();
        if (self->popup_host_) {
          ::DestroyWindow(self->popup_host_);
          self->popup_host_ = nullptr;
        }
        return 0;
      case WM_NCDESTROY:
        self->window_ = nullptr;
        break;
    }
    return ::DefWindowProcW(window, message, w, l);
  }
};
}  // namespace settings_color
