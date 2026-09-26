#include <WeaselPaletteCatalog.h>
#include <iostream>
#include <string>

int main() {
  auto* rime = rime_get_api();
  struct Case {
    const char* name;
    std::string yaml;
    size_t schemes;
    size_t groups;
  };
  const Case cases[] = {
      {"full yaml",
       "preset_color_schemes:\n"
       "  paper_light: {name: Paper, variant: light, back_color: 0xFFFFFFFF}\n"
       "  paper_dark: {name: Paper, variant: dark, back_color: 0x222222FF}\n"
       "color_scheme_groups:\n"
       "  paper: {name: Paper, light: paper_light, dark: paper_dark}\n",
       2, 1},
      {"custom patch",
       "patch:\n"
       "  preset_color_schemes/night: {name: Night, variant: dark, back_color: "
       "0x111111FF}\n",
       1, 0},
      {"single wrapped",
       "preset_color_schemes:\n  imported:\n"
       "    name: Solo\n    back_color: 0xAABBCCFF\n",
       1, 0},
      {"map wrapped",
       "preset_color_schemes:\n"
       "  one: {name: One, back_color: 0xFFFFFFFF}\n"
       "  two: {name: Two, back_color: 0x000000FF}\n",
       2, 0},
  };
  for (const auto& entry : cases) {
    weasel::PaletteCatalog catalog;
    if (!catalog.Load(rime, "", "", entry.yaml) ||
        catalog.schemes().size() != entry.schemes ||
        catalog.groups().size() != entry.groups) {
      std::cerr << "FAIL " << entry.name << '\n';
      return 1;
    }
    if (std::string(entry.name) == "full yaml" &&
        weasel::PaletteValue(rime, catalog.Config("custom:paper_light"),
                             "preset_color_schemes/paper_light/back_color") !=
            "0xFFFFFFFF") {
      std::cerr << "FAIL color value" << '\n';
      return 1;
    }
  }
  std::cout << "PASS\n";
  return 0;
}
