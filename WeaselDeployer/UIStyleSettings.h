#pragma once

#include <string>
#include <vector>
#include <array>
#include <set>
#include <utility>
#include <rime_levers_api.h>
#include <WeaselColorScheme.h>
#include <WeaselPaletteCatalog.h>
#include "CandidatePalette.h"

using ColorSchemeInfo = weasel::PaletteScheme;
using ColorSchemeGroup = weasel::PaletteGroup;

class UIStyleSettings {
 public:
  explicit UIStyleSettings(
      weasel::ColorSchemeTarget target = weasel::ColorSchemeTarget::Default);
  ~UIStyleSettings();
  UIStyleSettings(const UIStyleSettings&) = delete;
  UIStyleSettings& operator=(const UIStyleSettings&) = delete;
  weasel::ColorSchemeTarget target() const { return target_; }

  bool GetPresetColorSchemes(std::vector<ColorSchemeInfo>* result);
  std::string GetColorSchemePreview(const std::string& color_scheme_id);
  std::string GetActiveColorScheme();
  bool SelectColorScheme(const std::string& color_scheme_id);
  bool LoadAppearance();
  bool AppearanceSourcesChanged() const;
  std::array<std::string, 4> ActiveAppearance();
  bool SaveAppearance(const std::array<std::string, 4>& colors,
                      const candidate_palette::Draft* custom = nullptr,
                      const std::vector<std::string>& deleted_schemes = {},
                      const std::vector<std::string>& deleted_groups = {},
                      const candidate_palette::ImportPlan* imported = nullptr);
  bool PreparePaletteImport(const std::filesystem::path& path,
                            bool acrylic,
                            candidate_palette::ImportPlan* result,
                            std::wstring* error) const;
  bool configuration_changed() const { return configuration_changed_; }
  const std::vector<ColorSchemeInfo>& schemes() const {
    return palettes_.schemes();
  }
  const std::vector<ColorSchemeGroup>& groups() const {
    return palettes_.groups();
  }
  COLORREF PreviewColor(const std::string& id,
                        const char* key,
                        COLORREF fallback);
  uint32_t PaletteRgba(const std::string& id, size_t role, bool dark);
  int PreviewLayoutInt(const char* key, int fallback);
  int PreviewLayoutSignedInt(const char* key, int fallback);
  std::string PreviewLayoutString(const char* key, const char* fallback);
  bool SaveLayout(
      const std::vector<std::pair<std::string, int>>& values,
      const std::vector<std::pair<std::string, int>>& expected,
      bool horizontal,
      bool expected_horizontal,
      const std::string& layout_type,
      const std::string& expected_layout_type,
      const std::string& align_type,
      const std::string& expected_align_type,
      const std::array<bool, 3>& window_options,
      const std::array<bool, 3>& expected_window_options,
      const std::vector<std::pair<std::string, int>>& style_ints = {},
      const std::vector<std::pair<std::string, int>>& expected_style_ints = {},
      const std::vector<std::pair<std::string, bool>>& style_bools = {},
      const std::vector<std::pair<std::string, bool>>& expected_style_bools =
          {},
      const std::vector<std::pair<std::string, std::string>>& style_strings =
          {},
      const std::vector<std::pair<std::string, std::string>>&
          expected_style_strings = {});
  int PreviewStyleInt(const char* key, int fallback);
  bool PreviewStyleBool(const char* key, bool fallback);
  std::wstring PreviewStyleString(const char* key, const wchar_t* fallback);

  RimeCustomSettings* settings() { return settings_; }

 private:
  RimeLeversApi* api_;
  RimeCustomSettings* settings_;
  weasel::ColorSchemeTarget target_;
  weasel::PaletteCatalog palettes_;
  RimeConfig original_{nullptr};
  RimeConfig custom_{nullptr};
  std::string original_bytes_;
  std::string shared_bytes_;
  std::string base_bytes_;
  bool configuration_changed_ = false;
};
