#include <rime_api.h>
#include <rime_levers_api.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main() {
  const auto folder = std::filesystem::temp_directory_path() /
                      "weasel-candidate-palette-rime-smoke";
  std::filesystem::create_directories(folder);
  const auto directory = folder.u8string();
  std::ofstream(folder / "weasel.yaml") << "preset_color_schemes: {}\n";
  std::ofstream(folder / "weasel.custom.yaml") << "patch: {}\n";
  RimeApi* rime = rime_get_api();
  RIME_STRUCT(RimeTraits, traits);
  traits.app_name = "rime.weasel.palette.test";
  traits.shared_data_dir = directory.c_str();
  traits.user_data_dir = directory.c_str();
  traits.prebuilt_data_dir = directory.c_str();
  traits.staging_dir = directory.c_str();
  rime->setup(&traits);
  rime->deployer_initialize(&traits);
  auto* module = rime->find_module("levers");
  if (!module)
    return 1;
  auto* levers = reinterpret_cast<RimeLeversApi*>(module->get_api());
  auto* settings = levers->custom_settings_init("weasel", "palette.test");
  RimeConfig source{};
  RimeConfig scheme{};
  RimeConfig item{};
  const bool ok =
      levers->load_settings(settings) &&
      rime->config_load_string(
          &source,
          "preset_color_schemes:\n  base:\n    name: Base\n"
          "    color_format: rgba\n    back_color: 0x112233FF\n") &&
      rime->config_init(&scheme) &&
      rime->config_get_item(&source, "preset_color_schemes/base", &item) &&
      rime->config_set_item(&scheme, "", &item) &&
      rime->config_set_string(&scheme, "name", "Test") &&
      rime->config_set_string(&scheme, "border_color", "0xAABBCC80") &&
      levers->customize_item(settings, "preset_color_schemes/test", &scheme) &&
      levers->save_settings(settings);
  if (item.ptr)
    rime->config_close(&item);
  if (scheme.ptr)
    rime->config_close(&scheme);
  if (source.ptr)
    rime->config_close(&source);
  levers->custom_settings_destroy(settings);
  rime->finalize();
  std::ifstream stream(folder / "weasel.custom.yaml");
  const std::string yaml(std::istreambuf_iterator<char>{stream},
                         std::istreambuf_iterator<char>{});
  const bool verified =
      ok && yaml.find("preset_color_schemes/test") != std::string::npos &&
      yaml.find("0x112233FF") != std::string::npos &&
      yaml.find("0xAABBCC80") != std::string::npos;
  if (!verified) {
    std::cout << "FAIL create" << '\n';
    return 1;
  }
  // A null customization is the same removal used by the settings page.
  rime->setup(&traits);
  rime->deployer_initialize(&traits);
  module = rime->find_module("levers");
  if (!module)
    return 1;
  levers = reinterpret_cast<RimeLeversApi*>(module->get_api());
  settings = levers->custom_settings_init("weasel", "palette.test");
  const bool deleted =
      levers->load_settings(settings) &&
      levers->customize_item(settings, "preset_color_schemes/test", nullptr) &&
      levers->save_settings(settings);
  levers->custom_settings_destroy(settings);
  rime->finalize();
  std::ifstream after_stream(folder / "weasel.custom.yaml");
  const std::string after(std::istreambuf_iterator<char>{after_stream},
                          std::istreambuf_iterator<char>{});
  const bool gone =
      deleted && after.find("preset_color_schemes/test") == std::string::npos &&
      after.find("name: Test") == std::string::npos;
  std::cout << (gone ? "PASS" : "FAIL delete") << '\n';
  return gone ? 0 : 1;
}
