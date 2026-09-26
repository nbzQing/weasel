#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <string>
#include <vector>
#include "SettingsPerformance.h"
#include "AppearancePreviewText.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")

namespace weasel {

// Only the settings illustration uses this painter. The live candidate window
// and its composition clipping geometry are unchanged.
struct AppearancePreview {
  COLORREF page_background = ::GetSysColor(COLOR_BTNFACE);
  UINT dpi = 96;
  bool acrylic = true;
  bool dark = false;
  bool horizontal = false;
  bool fullscreen = false;
  bool vertical_text = false;
  bool vertical_text_left_to_right = false;
  bool vertical_text_with_wrap = false;
  bool reverse_candidates = false;
  bool inline_preedit = false;
  float radius = 11;
  float highlight_radius = 8;
  float border_width = 1;
  int min_width = 130;
  int max_width = 0;
  int min_height = 0;
  int max_height = 0;
  int align_type = 1;
  int margin_x = 11;
  int margin_y = 7;
  int spacing = 5;
  int candidate_spacing = 6;
  int hilite_spacing = 5;
  int hilite_padding_x = 8;
  int hilite_padding_y = 4;
  int baseline = 0;
  int linespacing = 0;
  int hover_type = 0;
  BYTE text_quality = ANTIALIASED_QUALITY;
  // Preview-only effects for the layout editor; live candidate rendering is
  // configured through Rime and remains independent of this illustration.
  bool layout_effects_preview = false;
  int shadow_radius = 6;
  int shadow_offset_x = 0;
  int shadow_offset_y = 2;
  uint32_t shadow_rgba = 0x00000028;
  int font_point = 11;
  int label_font_point = 9;
  std::wstring font_face = L"Microsoft YaHei";
  std::wstring label_font_face = L"Microsoft YaHei";
  std::wstring title;
  std::wstring preedit = L"ni hao";
  std::wstring mark_text;
  COLORREF background, border, text, label, highlight, highlighted_text,
      highlighted_label, mark;
  std::vector<std::wstring> candidates = std::vector<std::wstring>(5);
  std::vector<std::wstring> labels{L"1.", L"2.", L"3.", L"4.", L"5."};
  // Used only while the new custom color editor points out one preview part.
  int candidate_color_hint = -1;
  BYTE candidate_hint_alpha = 0;
  bool custom_palette = false;
  bool editing_preview = false;
  std::array<uint32_t, 22> custom_rgba{};
};

inline Gdiplus::Color PreviewColor(COLORREF rgb, BYTE alpha = 255) {
  return Gdiplus::Color(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
}

inline void PreviewRoundRect(Gdiplus::GraphicsPath& path,
                             const Gdiplus::RectF& rect,
                             float radius) {
  const float diameter = (std::min)((std::max)(0.0f, radius * 2),
                                    (std::min)(rect.Width, rect.Height));
  if (diameter <= 0) {
    path.AddRectangle(rect);
    return;
  }
  const float right = rect.GetRight() - diameter;
  const float bottom = rect.GetBottom() - diameter;
  path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
  path.AddArc(right, rect.Y, diameter, diameter, 270, 90);
  path.AddArc(right, bottom, diameter, diameter, 0, 90);
  path.AddArc(rect.X, bottom, diameter, diameter, 90, 90);
  path.CloseFigure();
}

inline void DrawPreviewGlow(Gdiplus::Graphics& canvas,
                            const Gdiplus::RectF& bounds,
                            const Gdiplus::Color& color) {
  Gdiplus::GraphicsPath path;
  path.AddEllipse(bounds);
  Gdiplus::PathGradientBrush glow(&path);
  glow.SetCenterColor(color);
  Gdiplus::Color edge(0, color.GetR(), color.GetG(), color.GetB());
  INT edge_count = 1;
  glow.SetSurroundColors(&edge, &edge_count);
  canvas.FillPath(&glow, &path);
}

inline bool DrawAppearancePreview(HDC dc,
                                  const RECT& bounds,
                                  HFONT font,
                                  const AppearancePreview& style) {
  using namespace Gdiplus;
  const auto started = settings_performance::Now();
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  if (width <= 0 || height <= 0)
    return false;
  Bitmap buffer(width, height, PixelFormat32bppPARGB);
  Graphics canvas(&buffer);
  canvas.SetSmoothingMode(SmoothingModeAntiAlias);
  canvas.SetPixelOffsetMode(PixelOffsetModeHalf);
  canvas.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
  const RectF bitmap_bounds(0, 0, static_cast<REAL>(width),
                            static_cast<REAL>(height));
  SolidBrush page_background(PreviewColor(style.page_background));
  canvas.FillRectangle(&page_background, bitmap_bounds);
  const float dpi_scale = static_cast<float>(style.dpi) / 96.0f;
  const RectF scene(0.5f, 0.5f, static_cast<REAL>(width) - 1.0f,
                    static_cast<REAL>(height) - 1.0f);
  GraphicsPath scene_path;
  PreviewRoundRect(scene_path, scene, 10.0f * dpi_scale);
  const auto scene_state = canvas.Save();
  canvas.SetClip(&scene_path);
  // A fixed smooth scene illustrates the backdrop without capturing desktop
  // contents. Normal mode covers it with a fully opaque background.
  LinearGradientBrush scenery(
      scene, style.dark ? Color(255, 27, 72, 96) : Color(255, 171, 214, 229),
      style.dark ? Color(255, 91, 47, 88) : Color(255, 235, 187, 216), 35.0f);
  canvas.FillPath(&scenery, &scene_path);
  DrawPreviewGlow(
      canvas,
      RectF(scene.X - scene.Width * 0.18f, scene.Y + scene.Height * 0.48f,
            scene.Width * 0.72f, scene.Height * 0.72f),
      style.dark ? Color(92, 41, 170, 196) : Color(118, 97, 193, 221));
  DrawPreviewGlow(
      canvas,
      RectF(scene.X + scene.Width * 0.58f, scene.Y - scene.Height * 0.18f,
            scene.Width * 0.62f, scene.Height * 0.68f),
      style.dark ? Color(84, 194, 71, 151) : Color(112, 239, 137, 188));
  const auto pixels = [dpi_scale](int value) {
    return static_cast<float>(value) * dpi_scale;
  };
  const auto custom_rgb = [&](size_t role) {
    const auto rgba = style.custom_rgba[role];
    return RGB((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255);
  };
  const auto custom_alpha = [&](size_t role) {
    return static_cast<BYTE>(style.custom_rgba[role] & 255);
  };
  LOGFONTW candidate_logical{};
  settings_performance::Record("paint.background", started);
  ::GetObjectW(font, sizeof(candidate_logical), &candidate_logical);
  candidate_logical.lfHeight =
      -::MulDiv((std::max)(1, style.font_point), style.dpi, 72);
  wcsncpy_s(candidate_logical.lfFaceName, style.font_face.c_str(), _TRUNCATE);
  AppearancePreviewText candidate_font(candidate_logical, style.text_quality);
  LOGFONTW label_logical = candidate_logical;
  label_logical.lfHeight =
      -::MulDiv((std::max)(1, style.label_font_point), style.dpi, 72);
  wcsncpy_s(label_logical.lfFaceName, style.label_font_face.c_str(), _TRUNCATE);
  AppearancePreviewText label_font(label_logical, style.text_quality);
  if (!candidate_font.valid() || !label_font.valid())
    return false;
  settings_performance::Record("paint.fonts", started);
  const auto measure = [&](const std::wstring& text,
                           const AppearancePreviewText& measure_font) {
    return measure_font.Measure(text);
  };
  const float candidate_height = candidate_font.height();
  const float label_height = label_font.height();
  const float line_spacing =
      style.linespacing > 0 && style.baseline > 0
          ? candidate_height * static_cast<float>(style.linespacing) / 100.0f
          : 0.0f;
  const float baseline_offset =
      style.linespacing > 0 && style.baseline > 0
          ? candidate_height *
                (static_cast<float>(style.baseline) -
                 static_cast<float>(style.linespacing) / 2.0f) /
                100.0f
          : 0.0f;
  const float row_height = (std::max)(candidate_height, label_height) +
                           line_spacing + pixels(style.hilite_padding_y) * 2;
  const float margin_x =
      pixels((std::max)(style.margin_x < 0 ? -style.margin_x : style.margin_x,
                        style.hilite_padding_x));
  const float margin_y =
      pixels((std::max)(style.margin_y < 0 ? -style.margin_y : style.margin_y,
                        style.hilite_padding_y));
  const float label_gap = pixels(style.hilite_spacing);
  const float candidate_gap = pixels(style.candidate_spacing);
  std::vector<float> entry_widths(style.candidates.size());
  std::vector<float> entry_heights(style.candidates.size());
  float widest_entry = 0;
  for (size_t i = 0; i < style.candidates.size(); ++i) {
    if (style.vertical_text) {
      float glyph_width = 0;
      for (wchar_t glyph : style.candidates[i])
        glyph_width = (std::max)(
            glyph_width, measure(std::wstring(1, glyph), candidate_font));
      const size_t glyph_count =
          (std::max)(size_t{1}, style.candidates[i].size());
      const size_t glyph_rows = style.vertical_text_with_wrap
                                    ? (std::min)(size_t{3}, glyph_count)
                                    : glyph_count;
      const size_t glyph_columns = (glyph_count + glyph_rows - 1) / glyph_rows;
      const float glyph_block_width =
          glyph_width * glyph_columns +
          pixels(2) * static_cast<float>(glyph_columns - 1);
      entry_widths[i] =
          (std::max)(measure(style.labels[i], label_font), glyph_block_width) +
          pixels(style.hilite_padding_x) * 2;
      entry_heights[i] = pixels(style.hilite_padding_y) * 2 + label_height +
                         label_gap + candidate_height * glyph_rows;
      if (i == 0 && !style.mark_text.empty())
        entry_heights[i] += label_height + label_gap;
    } else {
      entry_widths[i] = measure(style.labels[i], label_font) + label_gap +
                        measure(style.candidates[i], candidate_font);
      if (i == 0 && !style.mark_text.empty())
        entry_widths[i] += measure(style.mark_text, label_font) + label_gap;
      entry_heights[i] = row_height;
    }
    if (style.custom_palette && i < 2)
      entry_widths[i] += pixels(6) + measure(L"注释", label_font);
    widest_entry = (std::max)(widest_entry, entry_widths[i]);
  }
  const float preedit_height = style.inline_preedit ? 0.0f : candidate_height;
  const float preedit_width =
      style.inline_preedit ? 0.0f : measure(style.preedit, candidate_font);
  float content_width = widest_entry;
  if (style.horizontal || style.vertical_text) {
    content_width = 0;
    for (size_t i = 0; i < entry_widths.size(); ++i) {
      content_width +=
          entry_widths[i] +
          (style.vertical_text ? 0.0f : pixels(style.hilite_padding_x) * 2);
      if (i + 1 < entry_widths.size())
        content_width += candidate_gap;
    }
  }
  float panel_width =
      (std::max)(pixels(style.min_width),
                 (std::max)(content_width, preedit_width) + margin_x * 2);
  if (style.max_width > 0)
    panel_width = (std::min)(panel_width, pixels(style.max_width));
  float rows_height = style.horizontal
                          ? row_height
                          : row_height * style.candidates.size() +
                                candidate_gap * (style.candidates.size() - 1);
  if (style.vertical_text)
    rows_height = *std::max_element(entry_heights.begin(), entry_heights.end());
  const float pager_height = style.custom_palette ? pixels(16) : 0.0f;
  const float preedit_gap = style.inline_preedit ? 0.0f : pixels(style.spacing);
  float panel_height =
      margin_y * 2 + preedit_height + preedit_gap + rows_height + pager_height;
  if (style.layout_effects_preview) {
    panel_height = (std::max)(panel_height, pixels(style.min_height));
    if (style.max_height > 0)
      panel_height = (std::min)(panel_height, pixels(style.max_height));
  }
  LOGFONTW title_logical{};
  ::GetObjectW(font, sizeof(title_logical), &title_logical);
  title_logical.lfWeight = FW_NORMAL;
  AppearancePreviewText title_font(title_logical);
  if (!title_font.valid())
    return false;
  const size_t title_break = style.title.find(L" • ") != std::wstring::npos
                                 ? style.title.find(L" • ")
                                 : style.title.find(L" · ");
  const std::wstring title_first = title_break == std::wstring::npos
                                       ? style.title
                                       : style.title.substr(0, title_break);
  const std::wstring title_second = title_break == std::wstring::npos
                                        ? std::wstring()
                                        : style.title.substr(title_break + 3);
  const float title_line_gap = pixels(2);
  const float title_block_height =
      title_font.height() +
      (title_second.empty() ? 0 : title_font.height() + title_line_gap);
  // Wide horizontal candidates need a separate title row so neither the
  // translucent nor opaque panel can cover the preview's label.
  const float title_row = (style.horizontal || style.vertical_text)
                              ? title_block_height + pixels(20)
                              : 0;
  const float available_width = static_cast<float>(width) - pixels(16);
  const float available_height =
      static_cast<float>(height) - pixels(12) - title_row;
  if (style.fullscreen && style.layout_effects_preview) {
    panel_width = available_width;
    panel_height = available_height;
  }
  const float zoom =
      (std::min)(1.0f, (std::min)(available_width / panel_width,
                                  available_height / panel_height));
  const float origin_x = (width - panel_width * zoom) / 2.0f;
  const float origin_y =
      scene.Y + title_row +
      (scene.Height - title_row - panel_height * zoom) / 2.0f;
  const float title_x = pixels(12);
  const float title_y =
      style.horizontal || style.vertical_text ? pixels(12) : origin_y;
  const COLORREF title_color =
      style.dark ? RGB(245, 245, 245) : RGB(24, 24, 24);
  bool text_drawn = title_font.Draw(
      canvas, title_first,
      RectF(title_x, title_y, width - pixels(24), title_font.height()),
      title_color, false);
  if (!title_second.empty()) {
    const float second_y = title_y + title_font.height() + title_line_gap;
    if (style.custom_palette && style.editing_preview) {
      const float badge_width =
          (std::min)(title_font.Measure(title_second) + pixels(10),
                     static_cast<float>(width) - pixels(24));
      GraphicsPath badge_path;
      PreviewRoundRect(badge_path,
                       RectF(title_x - pixels(4), second_y - pixels(2),
                             badge_width, title_font.height() + pixels(4)),
                       pixels(4));
      SolidBrush badge(style.dark ? Color(185, 9, 102, 107)
                                  : Color(170, 198, 243, 243));
      canvas.FillPath(&badge, &badge_path);
    }
    text_drawn &= title_font.Draw(
        canvas, title_second,
        RectF(title_x, second_y, width - pixels(24), title_font.height()),
        title_color, false);
  }
  const auto preview_state = canvas.Save();
  canvas.TranslateTransform(origin_x, origin_y);
  canvas.ScaleTransform(zoom, zoom);
  const RectF panel(0, 0, panel_width, panel_height);
  GraphicsPath outline;
  PreviewRoundRect(outline, panel, pixels(static_cast<int>(style.radius)));
  if (style.layout_effects_preview && style.shadow_radius > 0 &&
      (style.shadow_rgba & 255u)) {
    const int layers = (std::min)(12, (std::max)(3, style.shadow_radius));
    const float radius = pixels(style.shadow_radius);
    for (int layer = layers; layer >= 1; --layer) {
      const float spread = radius * layer / layers;
      GraphicsPath shadow_path;
      PreviewRoundRect(shadow_path,
                       RectF(pixels(style.shadow_offset_x) - spread / 2,
                             pixels(style.shadow_offset_y) - spread / 2,
                             panel_width + spread, panel_height + spread),
                       pixels(static_cast<int>(style.radius)) + spread / 2);
      const BYTE alpha = static_cast<BYTE>((style.shadow_rgba & 255u) *
                                           (layers - layer + 1) / (layers * 2));
      Pen pen(Color(alpha, (style.shadow_rgba >> 24) & 255u,
                    (style.shadow_rgba >> 16) & 255u,
                    (style.shadow_rgba >> 8) & 255u),
              pixels(1));
      canvas.DrawPath(&pen, &shadow_path);
    }
  }
  if (style.custom_palette && custom_alpha(2)) {
    GraphicsPath shadow_path;
    PreviewRoundRect(shadow_path,
                     RectF(0, pixels(2), panel_width, panel_height),
                     pixels(static_cast<int>(style.radius)));
    Pen shadow(Color(custom_alpha(2), GetRValue(custom_rgb(2)),
                     GetGValue(custom_rgb(2)), GetBValue(custom_rgb(2))),
               pixels(5));
    canvas.DrawPath(&shadow, &shadow_path);
  }
  SolidBrush background(
      PreviewColor(style.custom_palette ? custom_rgb(0) : style.background,
                   style.custom_palette ? custom_alpha(0)
                   : style.acrylic      ? 176
                                        : 255));
  canvas.FillPath(&background, &outline);
  if (style.acrylic && !style.custom_palette) {
    // A soft directional veil gives the translucent panel a clearer frosted
    // glass appearance while keeping the configured colors recognizable.
    LinearGradientBrush frost(
        panel, style.dark ? Color(24, 255, 255, 255) : Color(62, 255, 255, 255),
        style.dark ? Color(16, 0, 0, 0) : Color(18, 255, 255, 255), 118.0f);
    canvas.FillPath(&frost, &outline);
  }
  const auto contentState = canvas.Save();
  canvas.SetClip(&outline);
  if (!style.inline_preedit && style.custom_palette) {
    const float normal_width = measure(L"ni ", candidate_font);
    const float focused_width = measure(L"hao", candidate_font);
    if (custom_alpha(5)) {
      SolidBrush highlight_back(PreviewColor(custom_rgb(5), custom_alpha(5)));
      canvas.FillRectangle(&highlight_back,
                           RectF(margin_x + normal_width, margin_y,
                                 focused_width, preedit_height));
    }
    if (custom_alpha(3))
      text_drawn &= candidate_font.Draw(
          canvas, L"ni ",
          RectF(margin_x, margin_y, normal_width, preedit_height),
          custom_rgb(3), true, custom_alpha(3));
    if (custom_alpha(4))
      text_drawn &= candidate_font.Draw(canvas, L"hao",
                                        RectF(margin_x + normal_width, margin_y,
                                              focused_width, preedit_height),
                                        custom_rgb(4), true, custom_alpha(4));
  } else if (!style.inline_preedit) {
    text_drawn &= candidate_font.Draw(
        canvas, style.preedit,
        RectF(margin_x, margin_y, panel_width - margin_x * 2, preedit_height),
        style.text);
  }
  const float candidates_top = margin_y + preedit_height + preedit_gap;
  float candidate_left = margin_x;
  std::vector<RectF> candidate_rects(style.candidates.size());
  const auto source_index = [&](size_t display_index) {
    size_t source = style.reverse_candidates
                        ? style.candidates.size() - 1 - display_index
                        : display_index;
    if (style.vertical_text && !style.vertical_text_left_to_right)
      source = style.candidates.size() - 1 - source;
    return source;
  };
  const auto paint_hover = [&](const RectF& item, size_t source) {
    if (source != 1 || style.hover_type == 0 || style.custom_palette)
      return;
    GraphicsPath hover_path;
    PreviewRoundRect(hover_path, item,
                     pixels(static_cast<int>(style.highlight_radius)));
    SolidBrush hover(
        PreviewColor(style.highlight, style.hover_type == 1 ? 205 : 105));
    canvas.FillPath(&hover, &hover_path);
  };
  const auto paint_selected_background = [&](const RectF& item, size_t source) {
    if (source != 0 || style.custom_palette)
      return;
    GraphicsPath highlight_path;
    PreviewRoundRect(highlight_path, item,
                     pixels(static_cast<int>(style.highlight_radius)));
    SolidBrush hilite(PreviewColor(style.highlight));
    canvas.FillPath(&hilite, &highlight_path);
  };
  const auto paint_marker_bar = [&](const RectF& item, size_t source) {
    if (source != 0 || !style.mark_text.empty())
      return;
    const float marker_width = (std::max)(pixels(3), 2.0f);
    GraphicsPath marker_path;
    PreviewRoundRect(
        marker_path,
        RectF(item.X + pixels(2), item.Y + pixels(style.hilite_padding_y),
              marker_width, item.Height - pixels(style.hilite_padding_y) * 2),
        marker_width / 2);
    if (!style.custom_palette || custom_alpha(15)) {
      SolidBrush marker(
          PreviewColor(style.custom_palette ? custom_rgb(15) : style.mark,
                       style.custom_palette ? custom_alpha(15) : 255));
      canvas.FillPath(&marker, &marker_path);
    }
  };

  if (style.vertical_text) {
    for (size_t display = 0; display < style.candidates.size(); ++display) {
      const size_t source = source_index(display);
      const RectF item(candidate_left, candidates_top, entry_widths[source],
                       rows_height);
      candidate_rects[source] = item;
      paint_hover(item, source);
      paint_selected_background(item, source);
      paint_marker_bar(item, source);
      const COLORREF label_color =
          source == 0 ? style.highlighted_label : style.label;
      const COLORREF candidate_color =
          source == 0 ? style.highlighted_text : style.text;
      float text_top = item.Y + pixels(style.hilite_padding_y);
      text_drawn &= label_font.Draw(
          canvas, style.labels[source],
          RectF(item.X + pixels(style.hilite_padding_x), text_top,
                item.Width - pixels(style.hilite_padding_x) * 2, label_height),
          label_color);
      text_top += label_height + label_gap;
      if (source == 0 && !style.mark_text.empty()) {
        text_drawn &= label_font.Draw(
            canvas, style.mark_text,
            RectF(item.X + pixels(style.hilite_padding_x), text_top,
                  item.Width - pixels(style.hilite_padding_x) * 2,
                  label_height),
            style.mark);
        text_top += label_height + label_gap;
      }
      const size_t glyph_count =
          (std::max)(size_t{1}, style.candidates[source].size());
      const size_t glyph_rows = style.vertical_text_with_wrap
                                    ? (std::min)(size_t{3}, glyph_count)
                                    : glyph_count;
      const size_t glyph_columns = (glyph_count + glyph_rows - 1) / glyph_rows;
      const float glyph_width =
          (item.Width - pixels(style.hilite_padding_x) * 2 -
           pixels(2) * static_cast<float>(glyph_columns - 1)) /
          static_cast<float>(glyph_columns);
      for (size_t glyph_index = 0;
           glyph_index < style.candidates[source].size(); ++glyph_index) {
        const size_t glyph_column = glyph_index / glyph_rows;
        const size_t glyph_row = glyph_index % glyph_rows;
        text_drawn &= candidate_font.Draw(
            canvas, std::wstring(1, style.candidates[source][glyph_index]),
            RectF(item.X + pixels(style.hilite_padding_x) +
                      static_cast<float>(glyph_column) *
                          (glyph_width + pixels(2)),
                  text_top + static_cast<float>(glyph_row) * candidate_height +
                      baseline_offset,
                  glyph_width, candidate_height),
            candidate_color);
      }
      candidate_left += item.Width + candidate_gap;
    }
  } else {
    for (size_t display = 0; display < style.candidates.size(); ++display) {
      const size_t source = source_index(display);
      const float top = style.horizontal
                            ? candidates_top
                            : candidates_top + static_cast<float>(display) *
                                                   (row_height + candidate_gap);
      const float item_width =
          style.horizontal
              ? entry_widths[source] + pixels(style.hilite_padding_x) * 2
              : panel_width - margin_x * 2;
      const RectF item(candidate_left, top, item_width, row_height);
      candidate_rects[source] = item;
      if (style.custom_palette) {
        const size_t back_role = source ? 9 : 13;
        const size_t border_role = source ? 18 : 14;
        const size_t shadow_role = source ? 19 : 20;
        GraphicsPath row_path;
        PreviewRoundRect(row_path, item,
                         pixels(static_cast<int>(style.highlight_radius)));
        if (custom_alpha(shadow_role)) {
          Pen row_shadow(Color(custom_alpha(shadow_role),
                               GetRValue(custom_rgb(shadow_role)),
                               GetGValue(custom_rgb(shadow_role)),
                               GetBValue(custom_rgb(shadow_role))),
                         pixels(3));
          canvas.DrawPath(&row_shadow, &row_path);
        }
        if (custom_alpha(back_role)) {
          SolidBrush row_back(
              PreviewColor(custom_rgb(back_role), custom_alpha(back_role)));
          canvas.FillPath(&row_back, &row_path);
        }
        if (custom_alpha(border_role)) {
          Pen row_border(
              PreviewColor(custom_rgb(border_role), custom_alpha(border_role)),
              pixels(1));
          canvas.DrawPath(&row_border, &row_path);
        }
      }
      paint_hover(item, source);
      paint_selected_background(item, source);
      paint_marker_bar(item, source);
      float text_left = item.X + pixels(style.hilite_padding_x);
      if (source == 0 && !style.mark_text.empty()) {
        const float mark_width = measure(style.mark_text, label_font);
        text_drawn &= label_font.Draw(
            canvas, style.mark_text,
            RectF(text_left, top, mark_width, row_height), style.mark);
        text_left += mark_width + label_gap;
      }
      const float label_width = measure(style.labels[source], label_font);
      const float label_top =
          style.layout_effects_preview && style.align_type != 1
              ? top + (style.align_type == 0
                           ? pixels(style.hilite_padding_y)
                           : row_height - label_height -
                                 pixels(style.hilite_padding_y))
              : top;
      const float candidate_top =
          (style.layout_effects_preview && style.align_type != 1
               ? top + (style.align_type == 0
                            ? pixels(style.hilite_padding_y)
                            : row_height - candidate_height -
                                  pixels(style.hilite_padding_y))
               : top) +
          baseline_offset;
      const size_t label_role = source ? 7 : 11;
      const size_t text_role = source ? 6 : 10;
      if (!style.custom_palette || custom_alpha(label_role))
        text_drawn &= label_font.Draw(
            canvas, style.labels[source],
            RectF(text_left, label_top, label_width,
                  style.align_type == 1 ? row_height : label_height),
            style.custom_palette ? custom_rgb(label_role)
            : source             ? style.label
                                 : style.highlighted_label,
            true, style.custom_palette ? custom_alpha(label_role) : 255);
      const float candidate_left_text = text_left + label_width + label_gap;
      const float comment_width = style.custom_palette && source < 2
                                      ? measure(L"注释", label_font) + pixels(6)
                                      : 0.0f;
      const float candidate_width = (std::max)(
          1.0f, item.Width - pixels(style.hilite_padding_x) * 2 - label_width -
                    label_gap - comment_width -
                    (text_left - item.X - pixels(style.hilite_padding_x)));
      if (!style.custom_palette || custom_alpha(text_role))
        text_drawn &= candidate_font.Draw(
            canvas, style.candidates[source],
            RectF(candidate_left_text, candidate_top, candidate_width,
                  style.align_type == 1 ? row_height : candidate_height),
            style.custom_palette ? custom_rgb(text_role)
            : source             ? style.text
                                 : style.highlighted_text,
            true, style.custom_palette ? custom_alpha(text_role) : 255);
      if (style.custom_palette && source < 2) {
        const size_t comment_role = source ? 8 : 12;
        if (custom_alpha(comment_role))
          text_drawn &= label_font.Draw(
              canvas, L"注释",
              RectF(candidate_left_text + candidate_width + pixels(6), top,
                    comment_width - pixels(6), row_height),
              custom_rgb(comment_role), true, custom_alpha(comment_role));
      }
      if (style.horizontal)
        candidate_left += item_width + candidate_gap;
    }
  }
  if (style.custom_palette) {
    const float pager_top = panel_height - margin_y - pager_height;
    const float arrow_width = pixels(10);
    const float arrow_gap = pixels(5);
    for (int arrow = 0; arrow < 2; ++arrow) {
      const size_t role = arrow ? 17 : 16;
      const COLORREF fallback =
          style.dark ? RGB(190, 190, 190) : RGB(117, 117, 117);
      const COLORREF ink = custom_alpha(role) ? custom_rgb(role) : fallback;
      text_drawn &= label_font.Draw(
          canvas, arrow ? L"›" : L"‹",
          RectF(panel_width - margin_x - (2 - arrow) * arrow_width -
                    (arrow ? 0 : arrow_gap),
                pager_top, arrow_width, pager_height),
          ink, true, custom_alpha(role) ? custom_alpha(role) : 255);
    }
  }
  if (style.candidate_color_hint >= 0 && style.candidate_hint_alpha) {
    const auto draw_hint = [&](RectF target) {
      target.X -= pixels(3);
      target.Y -= pixels(2);
      target.Width += pixels(6);
      target.Height += pixels(4);
      GraphicsPath shape;
      PreviewRoundRect(shape, target, pixels(4));
      SolidBrush fill(Color(style.candidate_hint_alpha, 10, 157, 161));
      Pen border(Color((std::min)(255, int(style.candidate_hint_alpha) + 48),
                       10, 157, 161),
                 1.0f);
      canvas.FillPath(&fill, &shape);
      canvas.DrawPath(&border, &shape);
    };
    const int role = style.candidate_color_hint;
    if (style.custom_palette && (role == 16 || role == 17)) {
      const float arrow_width = pixels(10);
      const float arrow_gap = pixels(5);
      const int arrow = role - 16;
      draw_hint(RectF(panel_width - margin_x - (2 - arrow) * arrow_width -
                          (arrow ? 0 : arrow_gap),
                      panel_height - margin_y - pager_height, arrow_width,
                      pager_height));
    } else if (role <= 2) {
      draw_hint(panel);
    } else if (role <= 5 || role == 21) {
      draw_hint(RectF(margin_x, margin_y, panel_width - 2 * margin_x,
                      preedit_height));
    } else if (role >= 10 && role <= 15 || role == 20) {
      draw_hint(candidate_rects[0]);
    } else {
      for (size_t i = 1; i < candidate_rects.size(); ++i)
        draw_hint(candidate_rects[i]);
    }
  }
  canvas.Restore(contentState);
  if (style.acrylic && (!style.layout_effects_preview ||
                        (style.margin_x >= 0 && style.margin_y >= 0))) {
    Pen acrylic_edge(
        style.dark ? Color(42, 255, 255, 255) : Color(96, 255, 255, 255),
        pixels(1));
    canvas.DrawPath(&acrylic_edge, &outline);
  }
  if (style.border_width > 0 && (!style.custom_palette || custom_alpha(1)) &&
      (!style.layout_effects_preview ||
       (style.margin_x >= 0 && style.margin_y >= 0))) {
    Pen border(PreviewColor(style.custom_palette ? custom_rgb(1) : style.border,
                            style.custom_palette ? custom_alpha(1) : 255),
               pixels(static_cast<int>((std::min)(style.border_width, 4.0f))));
    canvas.DrawPath(&border, &outline);
  }
  canvas.Restore(preview_state);
  canvas.Restore(scene_state);
  Pen scene_border(style.custom_palette && style.editing_preview
                       ? Color(255, 10, 157, 161)
                   : style.dark ? Color(74, 255, 255, 255)
                                : Color(54, 54, 54, 54),
                   1.0f);
  canvas.DrawPath(&scene_border, &scene_path);
  canvas.Flush(FlushIntentionSync);
  Graphics output(dc);
  // The buffer already matches the control's physical pixels. Specifying its
  // size avoids GDI+ scaling it a second time on a different-DPI display.
  return text_drawn && canvas.GetLastStatus() == Ok &&
         output.DrawImage(
             &buffer, Rect(static_cast<INT>(bounds.left),
                           static_cast<INT>(bounds.top), width, height)) == Ok;
}

}  // namespace weasel
