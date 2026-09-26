#include "../CandidatePaletteExport.h"
#include <WeaselPaletteCatalog.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool CheckColors(RimeApi* rime,
                 weasel::PaletteCatalog& catalog,
                 const std::string& id,
                 const candidate_palette::Colors& colors) {
  auto* config = catalog.Config("custom:" + id);
  if (!config)
    return false;
  for (size_t role = 0; role < colors.size(); ++role) {
    char expected[11]{};
    std::snprintf(expected, sizeof(expected), "0x%08X", colors[role]);
    const auto path = "preset_color_schemes/" + id + "/" +
                      candidate_palette::kRoles[role].key;
    if (weasel::PaletteValue(rime, config, path) != expected)
      return false;
  }
  return true;
}

bool CheckSingle(RimeApi* rime, weasel::PaletteTheme theme) {
  candidate_palette::ExportPlan plan;
  plan.name = "Green O'Brien 配色";
  plan.identity = "base:green|acrylic";
  plan.theme = theme;
  for (size_t role = 0; role < plan.light.size(); ++role) {
    plan.light[role] = 0x10203040u + static_cast<uint32_t>(role);
    plan.dark[role] = 0x50607080u + static_cast<uint32_t>(role);
  }
  const auto yaml = candidate_palette::BuildExportYaml(plan);
  weasel::PaletteCatalog catalog;
  const auto id = candidate_palette::ExportId(plan);
  const auto* scheme =
      catalog.Load(rime, "", "", yaml) ? catalog.Find("custom:" + id) : nullptr;
  return scheme && scheme->theme == theme && scheme->name == plan.name &&
         catalog.groups().empty() && catalog.schemes().size() == 1 &&
         CheckColors(
             rime, catalog, id,
             theme == weasel::PaletteTheme::Dark ? plan.dark : plan.light);
}

bool CheckPair(RimeApi* rime) {
  candidate_palette::ExportPlan plan;
  plan.name = "青色 / Cyan";
  plan.identity = "custom:cyan|normal";
  plan.paired = true;
  for (size_t role = 0; role < plan.light.size(); ++role) {
    plan.light[role] = 0xABCDEF10u + static_cast<uint32_t>(role);
    plan.dark[role] = 0x12345670u + static_cast<uint32_t>(role);
  }
  const auto yaml = candidate_palette::BuildExportYaml(plan);
  weasel::PaletteCatalog catalog;
  if (!catalog.Load(rime, "", "", yaml) || catalog.schemes().size() != 2 ||
      catalog.groups().size() != 1)
    return false;
  const auto stem = candidate_palette::ExportId(plan);
  const auto& group = catalog.groups().front();
  return group.name == plan.name &&
         group.light == "custom:" + stem + "_light" &&
         group.dark == "custom:" + stem + "_dark" &&
         CheckColors(rime, catalog, stem + "_light", plan.light) &&
         CheckColors(rime, catalog, stem + "_dark", plan.dark);
}

bool CheckUserConfig(RimeApi* rime) {
  candidate_palette::ExportPlan plan;
  plan.name = "User File Palette";
  plan.identity = "user-file";
  plan.light[0] = 0x11223380u;
  const auto folder =
      std::filesystem::temp_directory_path() / "weasel-palette-export-smoke";
  std::filesystem::create_directories(folder);
  const auto base = folder / "weasel.yaml";
  const auto custom = folder / "weasel.custom.yaml";
  std::ofstream(base) << "preset_color_schemes: {}\n";
  std::ofstream(custom) << candidate_palette::BuildExportYaml(plan);
  weasel::PaletteCatalog catalog;
  const auto id = candidate_palette::ExportId(plan);
  return catalog.LoadFiles(rime, folder, folder) &&
         catalog.Find("custom:" + id) &&
         CheckColors(rime, catalog, id, plan.light);
}

}  // namespace

int main() {
  auto* rime = rime_get_api();
  if (!CheckPair(rime) || !CheckSingle(rime, weasel::PaletteTheme::Light) ||
      !CheckSingle(rime, weasel::PaletteTheme::Dark) ||
      !CheckSingle(rime, weasel::PaletteTheme::Unspecified) ||
      !CheckUserConfig(rime)) {
    std::cerr << "FAIL palette export round trip\n";
    return 1;
  }
  std::cout << "PASS palette export round trip\n";
  return 0;
}
