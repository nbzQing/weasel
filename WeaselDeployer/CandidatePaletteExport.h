#pragma once

#include "CandidatePalette.h"
#include <WeaselPaletteCatalog.h>
#include <cstdint>
#include <cstdio>
#include <string>

namespace candidate_palette {

struct ExportPlan {
  std::string name;
  std::string identity;
  Colors light{};
  Colors dark{};
  bool paired = false;
  weasel::PaletteTheme theme = weasel::PaletteTheme::Unspecified;
};

inline std::string QuoteYaml(const std::string& value) {
  std::string quoted = "'";
  for (unsigned char character : value) {
    if (character == '\'')
      quoted += "''";
    else if (character < 32 || character == 127)
      quoted += ' ';
    else
      quoted += static_cast<char>(character);
  }
  quoted += '\'';
  return quoted;
}

inline std::string ExportId(const ExportPlan& plan) {
  uint64_t hash = 14695981039346656037ull;
  const auto feed = [&](const std::string& value) {
    for (unsigned char character : value) {
      hash ^= character;
      hash *= 1099511628211ull;
    }
    hash ^= 0xff;
    hash *= 1099511628211ull;
  };
  feed(plan.name);
  feed(plan.identity);
  feed(plan.paired ? "pair" : "single");
  char digits[17]{};
  std::snprintf(digits, sizeof(digits), "%016llx",
                static_cast<unsigned long long>(hash));
  return std::string("weasel_export_") + digits;
}

inline std::string BuildExportYaml(const ExportPlan& plan) {
  if (plan.name.empty())
    return {};
  std::string yaml =
      "# 可在小狼毫设置的「自定义颜色」中通过「导入配色文件」读取。\n"
      "# 手工使用时，将 patch 下的条目合并进用户文件夹的 weasel.custom.yaml，\n"
      "# 然后重新部署；请勿覆盖该文件中已有的设置。\n"
      "patch:\n";
  const std::string stem = ExportId(plan);
  const auto append_scheme = [&](const std::string& id, const std::string& name,
                                 weasel::PaletteTheme theme,
                                 const Colors& colors) {
    yaml += "  \"preset_color_schemes/" + id + "\":\n";
    yaml += "    name: " + QuoteYaml(name) + "\n";
    yaml += "    author: User\n";
    yaml += "    color_format: rgba\n";
    if (theme == weasel::PaletteTheme::Light)
      yaml += "    variant: light\n";
    else if (theme == weasel::PaletteTheme::Dark)
      yaml += "    variant: dark\n";
    for (size_t role = 0; role < kRoles.size(); ++role) {
      char color[11]{};
      std::snprintf(color, sizeof(color), "0x%08X", colors[role]);
      yaml += "    ";
      yaml += kRoles[role].key;
      yaml += ": ";
      yaml += color;
      yaml += '\n';
    }
  };
  if (plan.paired) {
    const auto light_id = stem + "_light";
    const auto dark_id = stem + "_dark";
    append_scheme(light_id, plan.name + " · 浅色", weasel::PaletteTheme::Light,
                  plan.light);
    append_scheme(dark_id, plan.name + " · 深色", weasel::PaletteTheme::Dark,
                  plan.dark);
    yaml += "  \"color_scheme_groups/" + stem + "\":\n";
    yaml += "    name: " + QuoteYaml(plan.name) + "\n";
    yaml += "    light: " + light_id + "\n";
    yaml += "    dark: " + dark_id + "\n";
  } else {
    append_scheme(
        stem, plan.name, plan.theme,
        plan.theme == weasel::PaletteTheme::Dark ? plan.dark : plan.light);
  }
  return yaml;
}

}  // namespace candidate_palette
