#include "stdafx.h"
#include <WeaselUtility.h>
#include "UIStyleSettings.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <cstdint>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <limits>

UIStyleSettings::UIStyleSettings(weasel::ColorSchemeTarget target)
    : target_(target) {
  api_ = (RimeLeversApi*)rime_get_api()->find_module("levers")->get_api();
  settings_ = api_->custom_settings_init("weasel", "Weasel::UIStyleSettings");
}

UIStyleSettings::~UIStyleSettings() {
  for (auto config : {&original_, &custom_}) {
    if (config->ptr)
      rime_get_api()->config_close(config);
  }
  api_->custom_settings_destroy(settings_);
}

namespace {
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string Value(RimeConfig* config, const std::string& key) {
  auto value = rime_get_api()->config_get_cstring(config, key.c_str());
  return value ? value : "";
}

bool CopyItem(RimeConfig* from,
              const std::string& source,
              RimeConfig* to,
              const std::string& destination) {
  auto api = rime_get_api();
  RimeConfig item{};
  bool ok = api->config_get_item(from, source.c_str(), &item) &&
            api->config_set_item(to, destination.c_str(), &item);
  if (item.ptr)
    api->config_close(&item);
  return ok;
}

std::string HexRgba(uint32_t color) {
  char text[11]{};
  std::snprintf(text, sizeof(text), "0x%08X", color);
  return text;
}

uint32_t BlendRgba(uint32_t foreground, uint32_t background) {
  const float front_alpha = (foreground & 255u) / 255.0f;
  const float back_alpha = (background & 255u) / 255.0f;
  const float alpha = front_alpha + (1.0f - front_alpha) * back_alpha;
  if (alpha <= 1e-6f)
    return background;
  uint32_t result = 0;
  for (int shift : {24, 16, 8}) {
    const float front = float((foreground >> shift) & 255u);
    const float back = float((background >> shift) & 255u);
    const uint32_t channel = static_cast<uint32_t>(
        (front * front_alpha + back * back_alpha * (1 - front_alpha)) / alpha);
    result |= channel << shift;
  }
  return result | static_cast<uint32_t>(alpha * 255.0f);
}

bool WriteCustomScheme(
    RimeApi* rime,
    RimeLeversApi* levers,
    RimeCustomSettings* settings,
    RimeConfig* source,
    const std::string& source_id,
    const std::string& id,
    const std::string& name,
    const candidate_palette::Colors& colors,
    const std::array<bool, candidate_palette::kRoles.size()>& edited,
    bool dark,
    bool unspecified) {
  RimeConfig config{};
  if (!rime->config_init(&config))
    return false;
  const bool copied =
      source &&
      CopyItem(source, "preset_color_schemes/" + source_id, &config, "");
  const auto format = copied ? Value(&config, "color_format") : "rgba";
  bool ok = rime->config_set_string(&config, "name", name.c_str()) &&
            rime->config_set_string(&config, "author", "User") &&
            rime->config_set_string(&config, "color_format",
                                    format.empty() ? "abgr" : format.c_str());
  if (unspecified)
    rime->config_clear(&config, "variant");
  else if (ok)
    ok = rime->config_set_string(&config, "variant", dark ? "dark" : "light");
  for (size_t role = 0; ok && role < colors.size(); ++role) {
    if (copied && !edited[role])
      continue;
    const uint32_t rgba = colors[role];
    const uint32_t argb = ((rgba & 255u) << 24) | (rgba >> 8);
    const uint32_t abgr = (argb & 0xff00ff00u) | ((argb & 0x00ff0000u) >> 16) |
                          ((argb & 0x000000ffu) << 16);
    const auto value = HexRgba(format == "argb"   ? argb
                               : format == "rgba" ? rgba
                                                  : abgr);
    ok = rime->config_set_string(&config, candidate_palette::kRoles[role].key,
                                 value.c_str());
  }
  if (ok)
    ok = levers->customize_item(
        settings, ("preset_color_schemes/" + id).c_str(), &config);
  rime->config_close(&config);
  return ok;
}

bool WriteCustomGroup(RimeApi* rime,
                      RimeLeversApi* levers,
                      RimeCustomSettings* settings,
                      const std::string& id,
                      const std::string& name,
                      const std::string& light,
                      const std::string& dark) {
  RimeConfig config{};
  if (!rime->config_init(&config))
    return false;
  const bool ok = rime->config_set_string(&config, "name", name.c_str()) &&
                  rime->config_set_string(&config, "light", light.c_str()) &&
                  rime->config_set_string(&config, "dark", dark.c_str()) &&
                  levers->customize_item(
                      settings, ("color_scheme_groups/" + id).c_str(), &config);
  rime->config_close(&config);
  return ok;
}

bool IsImportColor(const std::string& value) {
  if (value.empty())
    return false;
  const bool hash = value.front() == '#';
  const size_t start = hash ? 1
                       : value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0
                           ? 2
                           : 0;
  if (hash && value.size() - start != 6 && value.size() - start != 8)
    return false;
  if (!hash && start == 2 && value.size() - start != 6 &&
      value.size() - start != 8)
    return false;
  return std::all_of(value.begin() + start, value.end(),
                     [hash, start](unsigned char character) {
                       return (hash || start == 2
                                   ? std::isxdigit(character)
                                   : std::isdigit(character)) != 0;
                     });
}

std::wstring ImportNameKey(std::wstring name) {
  const auto first = name.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos)
    return {};
  const auto last = name.find_last_not_of(L" \t\r\n");
  name = name.substr(first, last - first + 1);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](wchar_t letter) { return std::towlower(letter); });
  return name;
}

std::string IndentYaml(const std::string& text, int spaces) {
  std::string result;
  const std::string indent(static_cast<size_t>(spaces), ' ');
  result.reserve(text.size() + text.size() / 12);
  size_t at = 0;
  while (at < text.size()) {
    const size_t end = text.find('\n', at);
    result += indent;
    result.append(text, at,
                  end == std::string::npos ? std::string::npos : end - at);
    result += '\n';
    if (end == std::string::npos)
      break;
    at = end + 1;
  }
  return result;
}

bool DecodeImportFile(const std::filesystem::path& path, std::string* text) {
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes == 0 || bytes > 1024 * 1024)
    return false;
  const std::string raw = ReadFile(path);
  if (raw.size() != bytes)
    return false;
  if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xff &&
      static_cast<unsigned char>(raw[1]) == 0xfe) {
    if ((raw.size() - 2) % 2)
      return false;
    std::wstring wide;
    wide.reserve((raw.size() - 2) / 2);
    for (size_t i = 2; i < raw.size(); i += 2)
      wide.push_back(
          static_cast<wchar_t>(static_cast<unsigned char>(raw[i]) |
                               (static_cast<unsigned char>(raw[i + 1]) << 8)));
    const int length = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
        static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
      return false;
    text->resize(length);
    return ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                 static_cast<int>(wide.size()), text->data(),
                                 length, nullptr, nullptr) == length;
  }
  const size_t prefix = raw.rfind("\xef\xbb\xbf", 0) == 0 ? 3 : 0;
  const char* begin = raw.data() + prefix;
  const int length = static_cast<int>(raw.size() - prefix);
  if (std::memchr(begin, '\0', length))
    return false;
  if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, begin, length,
                            nullptr, 0) > 0) {
    text->assign(begin, length);
    return true;
  }
  const int wide_length =
      ::MultiByteToWideChar(CP_ACP, 0, begin, length, nullptr, 0);
  if (wide_length <= 0)
    return false;
  std::wstring wide(wide_length, L'\0');
  if (::MultiByteToWideChar(CP_ACP, 0, begin, length, wide.data(),
                            wide_length) != wide_length)
    return false;
  const int utf8_length = ::WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), wide_length, nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0)
    return false;
  text->resize(utf8_length);
  return ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_length,
                               text->data(), utf8_length, nullptr,
                               nullptr) == utf8_length;
}

bool WriteImportedScheme(RimeApi* rime,
                         RimeLeversApi* levers,
                         RimeCustomSettings* settings,
                         weasel::PaletteCatalog* catalog,
                         const candidate_palette::ImportedScheme& scheme) {
  auto* source = catalog->Config("custom:" + scheme.source_id);
  if (!source)
    return false;
  const std::string prefix = "preset_color_schemes/" + scheme.source_id + "/";
  const std::string source_format = Value(source, prefix + "color_format");
  const std::string format = source_format.empty() ? "abgr" : source_format;
  RimeConfig config{};
  if (!rime->config_init(&config))
    return false;
  const std::string author = Value(source, prefix + "author");
  bool ok = rime->config_set_string(&config, "name", scheme.name.c_str()) &&
            rime->config_set_string(&config, "author",
                                    author.empty() ? "User" : author.c_str()) &&
            rime->config_set_string(&config, "color_format", format.c_str());
  if (ok && !scheme.unspecified)
    ok = rime->config_set_string(&config, "variant",
                                 scheme.dark ? "dark" : "light");
  for (const auto& role : candidate_palette::kRoles) {
    if (!ok)
      break;
    std::string color = Value(source, prefix + role.key);
    if (color.empty())
      continue;
    if (color.front() == '#') {
      uint32_t rgba =
          static_cast<uint32_t>(std::stoul(color.substr(1), nullptr, 16));
      if (color.size() == 7)
        rgba = (rgba << 8) | 0xff;
      const uint32_t argb = ((rgba & 255u) << 24) | (rgba >> 8);
      const uint32_t abgr = (argb & 0xff00ff00u) |
                            ((argb & 0x00ff0000u) >> 16) |
                            ((argb & 0x000000ffu) << 16);
      color = HexRgba(format == "rgba" ? rgba : format == "argb" ? argb : abgr);
    }
    ok = rime->config_set_string(&config, role.key, color.c_str());
  }
  if (ok)
    ok = levers->customize_item(
        settings, ("preset_color_schemes/" + scheme.target_id).c_str(),
        &config);
  rime->config_close(&config);
  return ok;
}

candidate_palette::Colors ImportedColors(weasel::PaletteCatalog* catalog,
                                         const std::string& source_id,
                                         bool dark) {
  candidate_palette::Colors colors{};
  auto* source = catalog->Config("custom:" + source_id);
  const std::string prefix = "preset_color_schemes/" + source_id + "/";
  const std::string format = Value(source, prefix + "color_format");
  for (size_t role = 0; role < colors.size(); ++role) {
    const auto& definition = candidate_palette::kRoles[role];
    const uint32_t fallback = dark ? definition.dark : definition.light;
    std::string value = Value(source, prefix + definition.key);
    if (value.empty()) {
      colors[role] = fallback;
      continue;
    }
    try {
      if (value.front() == '#') {
        uint32_t rgba =
            static_cast<uint32_t>(std::stoul(value.substr(1), nullptr, 16));
        colors[role] = value.size() == 7 ? (rgba << 8) | 0xff : rgba;
        continue;
      }
      const bool alpha = value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0
                             ? value.size() == 10
                             : std::stoull(value) > 0xffffff;
      const uint32_t raw =
          static_cast<uint32_t>(std::stoull(value, nullptr, 0));
      if (format == "rgba")
        colors[role] = alpha ? raw : (raw << 8) | 0xff;
      else if (format == "argb") {
        const uint32_t argb = alpha ? raw : (0xff000000u | raw);
        colors[role] = ((argb & 0xffffffu) << 8) | (argb >> 24);
      } else {
        const uint32_t abgr = alpha ? raw : (0xff000000u | raw);
        colors[role] = ((abgr & 0xffu) << 24) | (((abgr >> 8) & 0xffu) << 16) |
                       (((abgr >> 16) & 0xffu) << 8) | (abgr >> 24);
      }
    } catch (...) {
      colors[role] = fallback;
    }
  }
  return colors;
}
}  // namespace

bool UIStyleSettings::AppearanceSourcesChanged() const {
  const auto user = WeaselUserDataPath();
  return ReadFile(user / L"weasel.custom.yaml") != original_bytes_ ||
         ReadFile(user / L"weasel.yaml") != base_bytes_ ||
         ReadFile(WeaselSharedDataPath() / L"weasel.yaml") != shared_bytes_;
}

bool UIStyleSettings::LoadAppearance() {
  auto rime = rime_get_api();
  for (auto* config : {&original_, &custom_}) {
    if (config->ptr)
      rime->config_close(config);
    config->ptr = nullptr;
  }
  const auto user = WeaselUserDataPath();
  original_bytes_ = ReadFile(user / L"weasel.custom.yaml");
  shared_bytes_ = ReadFile(WeaselSharedDataPath() / L"weasel.yaml");
  base_bytes_ = ReadFile(user / L"weasel.yaml");
  // Refresh both the raw catalog and levers' save snapshot every time. An
  // undeployed manual edit is readable; save guards all three source files.
  const bool loaded = api_->load_settings(settings_) != False;
  RimeConfig borrowed{};
  if ((!loaded && !original_bytes_.empty()) ||
      !rime->config_load_string(
          &custom_, original_bytes_.empty() ? "{}" : original_bytes_.c_str()) ||
      !api_->settings_get_config(settings_, &borrowed) ||
      !rime->config_init(&original_) ||
      !palettes_.Load(rime, shared_bytes_, base_bytes_, original_bytes_))
    return false;
  if (!CopyItem(&borrowed, "", &original_, ""))
    return false;
  palettes_.ReadStyle(&original_);
  return ReadFile(user / L"weasel.custom.yaml") == original_bytes_ &&
         ReadFile(user / L"weasel.yaml") == base_bytes_ &&
         ReadFile(WeaselSharedDataPath() / L"weasel.yaml") == shared_bytes_;
}

std::array<std::string, 4> UIStyleSettings::ActiveAppearance() {
  std::array<std::string, 4> result;
  for (size_t i = 0; i < result.size(); ++i) {
    const auto key = std::string(weasel::AppearanceColorKey(i));
    auto id = Value(&original_, key);
    auto source = Value(&original_, key + "_source");
    if (i >= 2) {
      const auto legacy =
          Value(&original_, i == 3 ? "style/color_scheme_normal_dark"
                                   : "style/color_scheme_normal");
      if (!legacy.empty()) {
        id = legacy;
        source.clear();
      }
    }
    result[i] = palettes_.Resolve(id, source);
  }
  return result;
}

bool UIStyleSettings::PreparePaletteImport(
    const std::filesystem::path& path,
    bool acrylic,
    candidate_palette::ImportPlan* result,
    std::wstring* error) const {
  if (!result)
    return false;
  *result = {};
  result->acrylic = acrylic;
  std::string text;
  if (!DecodeImportFile(path, &text)) {
    if (error)
      *error = L"无法读取配色文件，或文件超过 1 MB。";
    return false;
  }
  auto rime = rime_get_api();
  weasel::PaletteCatalog imported;
  std::string normalized = text;
  if (!imported.Load(rime, "", "", normalized)) {
    RimeConfig probe{};
    if (!rime->config_load_string(&probe, text.c_str())) {
      if (error)
        *error = L"文件不是有效的 Rime/YAML 配色内容。";
      return false;
    }
    bool single = false;
    for (const auto& role : candidate_palette::kRoles)
      if (rime->config_get_cstring(&probe, role.key)) {
        single = true;
        break;
      }
    rime->config_close(&probe);
    normalized =
        single ? "preset_color_schemes:\n  imported:\n" + IndentYaml(text, 4)
               : "preset_color_schemes:\n" + IndentYaml(text, 2);
    if (!imported.Load(rime, "", "", normalized)) {
      if (error)
        *error = L"未识别到配色方案。请使用 Rime 配色键值或 YAML 配色片段。";
      return false;
    }
  }
  if (imported.schemes().size() > 32) {
    if (error)
      *error = L"一次最多导入 32 个配色方案。";
    return false;
  }
  std::set<std::string> recognized;
  for (const auto& scheme : imported.schemes()) {
    const std::string id = weasel::PaletteId(scheme.color_scheme_id);
    const std::string prefix = "preset_color_schemes/" + id + "/";
    auto* config = imported.Config(scheme.color_scheme_id);
    bool has_color = false;
    const auto format = Value(config, prefix + "color_format");
    if (!format.empty() && format != "rgba" && format != "argb" &&
        format != "abgr") {
      if (error)
        *error = L"配色方案包含不支持的 color_format。";
      return false;
    }
    for (const auto& role : candidate_palette::kRoles) {
      const auto color = Value(config, prefix + role.key);
      if (color.empty())
        continue;
      if (!IsImportColor(color)) {
        if (error)
          *error =
              L"配色方案“" + u8tow(scheme.name) + L"”存在无法识别的颜色值。";
        return false;
      }
      has_color = true;
    }
    if (has_color)
      recognized.insert(id);
  }
  if (recognized.empty()) {
    if (error)
      *error = L"未找到可导入的颜色设置项。";
    return false;
  }
  std::set<std::wstring> names;
  for (const auto& scheme : schemes())
    names.insert(ImportNameKey(u8tow(scheme.name)));
  for (const auto& group : groups())
    names.insert(ImportNameKey(u8tow(group.name)));
  const auto unique_name = [&](std::wstring requested, bool pair) {
    if (ImportNameKey(requested).empty())
      requested = L"导入配色";
    for (int suffix = 0; suffix < 10000; ++suffix) {
      std::wstring candidate = requested;
      if (suffix)
        candidate +=
            L" 导入" + (suffix > 1 ? L" " + std::to_wstring(suffix) : L"");
      const auto key = ImportNameKey(candidate);
      const auto light = ImportNameKey(candidate + L" · 浅色");
      const auto dark = ImportNameKey(candidate + L" · 深色");
      if (names.count(key) ||
          (pair && (names.count(light) || names.count(dark))))
        continue;
      names.insert(key);
      if (pair) {
        names.insert(light);
        names.insert(dark);
      }
      return candidate;
    }
    return requested + L" 导入";
  };
  const auto stamp = "weasel_user_" + std::to_string(::GetTickCount64()) + "_" +
                     std::to_string(::GetCurrentProcessId());
  const std::string material = acrylic ? "_acrylic" : "_normal";
  std::set<std::string> paired;
  size_t index = 0;
  for (const auto& group : imported.groups()) {
    const auto light = weasel::PaletteId(group.light);
    const auto dark = weasel::PaletteId(group.dark);
    if (!recognized.count(light) || !recognized.count(dark) ||
        paired.count(light) || paired.count(dark))
      continue;
    const auto name = unique_name(u8tow(group.name), true);
    const auto stem = stamp + "_" + std::to_string(index++) + material;
    result->schemes.push_back({light, stem + "_light", wtou8(name + L" · 浅色"),
                               false, ImportedColors(&imported, light, false)});
    result->schemes.push_back({dark, stem + "_dark", wtou8(name + L" · 深色"),
                               true, ImportedColors(&imported, dark, true)});
    result->groups.push_back(
        {stem, wtou8(name), stem + "_light", stem + "_dark"});
    paired.insert(light);
    paired.insert(dark);
  }
  for (const auto& scheme : imported.schemes()) {
    const auto id = weasel::PaletteId(scheme.color_scheme_id);
    if (!recognized.count(id) || paired.count(id))
      continue;
    const bool dark = scheme.theme == weasel::PaletteTheme::Dark;
    const auto name = unique_name(u8tow(scheme.name), false);
    const auto stem = stamp + "_" + std::to_string(index++) + material;
    const bool unspecified = scheme.theme == weasel::PaletteTheme::Unspecified;
    const char* suffix = unspecified ? "_single" : dark ? "_dark" : "_light";
    result->schemes.push_back({id, stem + suffix, wtou8(name), dark,
                               ImportedColors(&imported, id, dark),
                               unspecified});
  }
  result->normalized_yaml = std::move(normalized);
  return !result->schemes.empty();
}

bool UIStyleSettings::SaveAppearance(
    const std::array<std::string, 4>& colors,
    const candidate_palette::Draft* custom,
    const std::vector<std::string>& deleted_schemes,
    const std::vector<std::string>& deleted_groups,
    const candidate_palette::ImportPlan* imported) {
  auto rime = rime_get_api();
  configuration_changed_ = false;
  const auto user = WeaselUserDataPath();
  const auto path = user / L"weasel.custom.yaml";
  const auto unchanged = [&]() {
    return ReadFile(path) == original_bytes_ &&
           ReadFile(user / L"weasel.yaml") == base_bytes_ &&
           ReadFile(WeaselSharedDataPath() / L"weasel.yaml") == shared_bytes_;
  };
  if (!unchanged())
    return false;
  const auto before = ActiveAppearance();
  const bool custom_changed =
      custom && std::any_of(custom->changed.begin(), custom->changed.end(),
                            [](bool changed) { return changed; });
  const bool import_changed = imported && !imported->schemes.empty();
  if (colors == before && !custom_changed && deleted_schemes.empty() &&
      deleted_groups.empty() && !import_changed)
    return true;
  if (import_changed) {
    for (const auto& scheme : imported->schemes)
      if (scheme.target_id.rfind("weasel_user_", 0) != 0 ||
          scheme.target_id.find('/') != std::string::npos ||
          palettes_.Find("custom:" + scheme.target_id) ||
          palettes_.Find("base:" + scheme.target_id))
        return false;
  }
  const auto valid_delete_id = [](const std::string& id) {
    return id.rfind("weasel_user_", 0) == 0 &&
           id.find('/') == std::string::npos;
  };
  for (const auto& id : deleted_schemes) {
    if (!valid_delete_id(id) || !palettes_.Find("custom:" + id) ||
        std::find(colors.begin(), colors.end(), "custom:" + id) != colors.end())
      return false;
  }
  for (const auto& id : deleted_groups)
    if (!valid_delete_id(id))
      return false;
  // An existing unavailable or cross-theme value is preserved unless edited.
  for (size_t i = 0; i < colors.size(); ++i) {
    const bool new_custom =
        custom && custom->changed[i] && colors[i] == "custom:" + custom->ids[i];
    if (colors[i] != before[i] && !palettes_.Find(colors[i]) && !new_custom)
      return false;
  }
  // Reload levers before each save attempt so a previous failed attempt cannot
  // leave pending mutations behind. Keep all scheme definitions untouched.
  if (!api_->load_settings(settings_) && !original_bytes_.empty())
    return false;
  for (const auto& id : deleted_schemes)
    if (!api_->customize_item(settings_, ("preset_color_schemes/" + id).c_str(),
                              nullptr))
      return false;
  for (const auto& id : deleted_groups)
    if (!api_->customize_item(settings_, ("color_scheme_groups/" + id).c_str(),
                              nullptr))
      return false;
  if (import_changed) {
    weasel::PaletteCatalog source;
    if (!source.Load(rime, "", "", imported->normalized_yaml))
      return false;
    for (const auto& scheme : imported->schemes)
      if (!WriteImportedScheme(rime, api_, settings_, &source, scheme))
        return false;
    for (const auto& group : imported->groups)
      if (!WriteCustomGroup(rime, api_, settings_, group.target_stem,
                            group.name, group.light_id, group.dark_id))
        return false;
  }
  if (custom_changed) {
    if (custom->name.empty())
      return false;
    const auto name = wtou8(custom->name);
    for (size_t i = 0; i < custom->changed.size(); ++i) {
      if (!custom->changed[i])
        continue;
      const auto& id = custom->ids[i];
      const bool unspecified =
          id.size() > 7 && id.compare(id.size() - 7, 7, "_single") == 0;
      const auto scheme_name =
          name + (unspecified ? "" : (i % 2 ? " · 深色" : " · 浅色"));
      if (id.empty() || id.find('/') != std::string::npos ||
          palettes_.Find("base:" + id) ||
          (palettes_.Find("custom:" + id) &&
           custom->sources[i] != "custom:" + id) ||
          !WriteCustomScheme(
              rime, api_, settings_, palettes_.Config(custom->sources[i]),
              weasel::PaletteId(custom->sources[i]), id, scheme_name,
              custom->colors[i], custom->edited[i], i % 2 != 0, unspecified))
        return false;
    }
    for (size_t material = 0; material < 2; ++material) {
      const size_t light = material * 2;
      const size_t dark = light + 1;
      const bool has_light = custom->changed[light] ||
                             palettes_.Find("custom:" + custom->ids[light]);
      const bool has_dark = custom->changed[dark] ||
                            palettes_.Find("custom:" + custom->ids[dark]);
      if (!has_light || !has_dark)
        continue;
      const auto& light_id = custom->ids[light];
      const auto& dark_id = custom->ids[dark];
      const auto suffix = std::string("_light");
      if (light_id.size() <= suffix.size() ||
          light_id.compare(light_id.size() - suffix.size(), suffix.size(),
                           suffix) != 0)
        continue;
      const auto stem = light_id.substr(0, light_id.size() - suffix.size());
      if (dark_id != stem + "_dark")
        continue;
      if (!WriteCustomGroup(rime, api_, settings_, stem,
                            name + (material ? " · 普通" : ""), light_id,
                            dark_id))
        return false;
    }
  }
  for (size_t i = 0; i < colors.size(); ++i) {
    if (colors[i] == before[i])
      continue;
    const std::string key = weasel::AppearanceColorKey(i);
    if (!api_->customize_string(settings_, key.c_str(),
                                weasel::PaletteId(colors[i]).c_str()) ||
        !api_->customize_string(settings_, (key + "_source").c_str(),
                                weasel::PaletteSource(colors[i]).c_str()))
      return false;
    if (i >= 2) {
      const char* legacy =
          i == 3 ? "color_scheme_normal_dark" : "color_scheme_normal";
      if (!api_->customize_item(
              settings_, (std::string("style/") + legacy).c_str(), nullptr))
        return false;
    }
  }
  RimeConfig style{};
  rime->config_get_item(&custom_, "patch/style", &style);
  if (style.ptr) {
    bool modified = false;
    for (size_t i = 2; i < colors.size(); ++i) {
      if (colors[i] != before[i]) {
        rime->config_clear(&style, i == 3 ? "color_scheme_normal_dark"
                                          : "color_scheme_normal");
        modified = true;
      }
    }
    const bool ok =
        !modified || api_->customize_item(settings_, "style", &style);
    rime->config_close(&style);
    if (!ok)
      return false;
  }
  FILETIME now{};
  ::GetSystemTimeAsFileTime(&now);
  const auto stamp =
      (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  const auto backup = path.wstring() + L".before-appearance-" +
                      std::to_wstring(stamp) + L".bak";
  int recorded = 0;
  if (rime->config_get_int(&original_, "__build_info/timestamps/weasel.custom",
                           &recorded) &&
      recorded == std::time(nullptr))
    ::Sleep(1100);
  if (!unchanged())
    return false;
  const bool existed = std::filesystem::exists(path);
  if (existed && !::CopyFileW(path.c_str(), backup.c_str(), TRUE))
    return false;
  const bool saved = api_->save_settings(settings_) != False;
  RimeConfig verify{};
  const auto updated = ReadFile(path);
  const bool valid = saved && !updated.empty() && updated != original_bytes_ &&
                     rime->config_load_string(&verify, updated.c_str());
  if (verify.ptr)
    rime->config_close(&verify);
  if (!valid) {
    if (existed)
      ::CopyFileW(backup.c_str(), path.c_str(), FALSE);
    else if (!updated.empty())
      ::DeleteFileW(path.c_str());
    return false;
  }
  original_bytes_ = updated;
  configuration_changed_ = true;
  return true;
}

int UIStyleSettings::PreviewLayoutInt(const char* key, int fallback) {
  int value = fallback;
  const auto path = std::string("style/layout/") + key;
  rime_get_api()->config_get_int(&original_, path.c_str(), &value);
  return (std::max)(0, value);
}

int UIStyleSettings::PreviewLayoutSignedInt(const char* key, int fallback) {
  int value = fallback;
  if (std::strcmp(key, "border_width") == 0 &&
      rime_get_api()->config_get_int(&original_, "style/layout/border", &value))
    return value;
  if (std::strcmp(key, "round_corner") == 0 &&
      rime_get_api()->config_get_int(
          &original_, "style/layout/hilited_corner_radius", &value))
    return value;
  const auto path = std::string("style/layout/") + key;
  rime_get_api()->config_get_int(&original_, path.c_str(), &value);
  return value;
}

std::string UIStyleSettings::PreviewLayoutString(const char* key,
                                                 const char* fallback) {
  const auto value = Value(&original_, std::string("style/layout/") + key);
  return value.empty() ? fallback : value;
}

bool UIStyleSettings::SaveLayout(
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
    const std::vector<std::pair<std::string, int>>& style_ints,
    const std::vector<std::pair<std::string, int>>& expected_style_ints,
    const std::vector<std::pair<std::string, bool>>& style_bools,
    const std::vector<std::pair<std::string, bool>>& expected_style_bools,
    const std::vector<std::pair<std::string, std::string>>& style_strings,
    const std::vector<std::pair<std::string, std::string>>&
        expected_style_strings) {
  constexpr const char* kWindowKeys[] = {"vertical_text_left_to_right",
                                         "vertical_text_with_wrap",
                                         "vertical_auto_reverse"};
  configuration_changed_ = false;
  if (AppearanceSourcesChanged() && !LoadAppearance())
    return false;
  for (const auto& [key, old_value] : expected)
    if (PreviewLayoutSignedInt(key.c_str(), old_value) != old_value)
      return false;
  if (PreviewStyleBool("horizontal", expected_horizontal) !=
      expected_horizontal)
    return false;
  if (PreviewLayoutString("type", "") != expected_layout_type ||
      PreviewLayoutString("align_type", "center") != expected_align_type)
    return false;
  for (size_t i = 0; i < window_options.size(); ++i)
    if (PreviewStyleBool(kWindowKeys[i], false) != expected_window_options[i])
      return false;
  for (const auto& [key, old_value] : expected_style_ints)
    if (PreviewStyleInt(key.c_str(), old_value) != old_value)
      return false;
  for (const auto& [key, old_value] : expected_style_bools)
    if (PreviewStyleBool(key.c_str(), old_value) != old_value)
      return false;
  for (const auto& [key, old_value] : expected_style_strings)
    if (PreviewStyleString(key.c_str(), u8tow(old_value).c_str()) !=
        u8tow(old_value))
      return false;
  const auto path = WeaselUserDataPath() / L"weasel.custom.yaml";
  if (!api_->load_settings(settings_) && !original_bytes_.empty())
    return false;
  bool changed = false;
  for (const auto& [key, value] : values) {
    if (PreviewLayoutSignedInt(key.c_str(), value) == value)
      continue;
    if (!api_->customize_int(settings_, ("style/layout/" + key).c_str(), value))
      return false;
    if (key == "border_width" &&
        !api_->customize_int(settings_, "style/layout/border", value))
      return false;
    if (key == "round_corner" &&
        !api_->customize_int(settings_, "style/layout/hilited_corner_radius",
                             value))
      return false;
    changed = true;
  }
  if (horizontal != expected_horizontal) {
    if (!api_->customize_bool(settings_, "style/horizontal",
                              horizontal ? True : False))
      return false;
    changed = true;
  }
  if (layout_type != expected_layout_type) {
    if (!api_->customize_string(settings_, "style/layout/type",
                                layout_type.c_str()))
      return false;
    changed = true;
  }
  if (align_type != expected_align_type) {
    if (!api_->customize_string(settings_, "style/layout/align_type",
                                align_type.c_str()))
      return false;
    changed = true;
  }
  for (size_t i = 0; i < window_options.size(); ++i) {
    if (window_options[i] == expected_window_options[i])
      continue;
    if (!api_->customize_bool(settings_,
                              (std::string("style/") + kWindowKeys[i]).c_str(),
                              window_options[i] ? True : False))
      return false;
    changed = true;
  }
  for (const auto& [key, value] : style_ints) {
    const auto old =
        std::find_if(expected_style_ints.begin(), expected_style_ints.end(),
                     [&](const auto& item) { return item.first == key; });
    if (old != expected_style_ints.end() && old->second == value)
      continue;
    if (!api_->customize_int(settings_, ("style/" + key).c_str(), value))
      return false;
    changed = true;
  }
  for (const auto& [key, value] : style_bools) {
    const auto old =
        std::find_if(expected_style_bools.begin(), expected_style_bools.end(),
                     [&](const auto& item) { return item.first == key; });
    if (old != expected_style_bools.end() && old->second == value)
      continue;
    if (!api_->customize_bool(settings_, ("style/" + key).c_str(),
                              value ? True : False))
      return false;
    changed = true;
  }
  for (const auto& [key, value] : style_strings) {
    const auto old = std::find_if(
        expected_style_strings.begin(), expected_style_strings.end(),
        [&](const auto& item) { return item.first == key; });
    if (old != expected_style_strings.end() && old->second == value)
      continue;
    if (!api_->customize_string(settings_, ("style/" + key).c_str(),
                                value.c_str()))
      return false;
    changed = true;
  }
  if (!changed)
    return true;
  FILETIME now{};
  ::GetSystemTimeAsFileTime(&now);
  const auto stamp =
      (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  const auto backup =
      path.wstring() + L".before-layout-" + std::to_wstring(stamp) + L".bak";
  const bool existed = std::filesystem::exists(path);
  if (AppearanceSourcesChanged() ||
      (existed && !::CopyFileW(path.c_str(), backup.c_str(), TRUE)))
    return false;
  const bool saved = api_->save_settings(settings_) != False;
  RimeConfig verify{};
  const auto updated = ReadFile(path);
  const bool valid =
      saved && !updated.empty() && updated != original_bytes_ &&
      rime_get_api()->config_load_string(&verify, updated.c_str());
  if (verify.ptr)
    rime_get_api()->config_close(&verify);
  if (!valid) {
    if (existed)
      ::CopyFileW(backup.c_str(), path.c_str(), FALSE);
    else if (!updated.empty())
      ::DeleteFileW(path.c_str());
    return false;
  }
  original_bytes_ = updated;
  configuration_changed_ = true;
  return true;
}

int UIStyleSettings::PreviewStyleInt(const char* key, int fallback) {
  int value = fallback;
  const auto path = std::string("style/") + key;
  rime_get_api()->config_get_int(&original_, path.c_str(), &value);
  return (std::max)(0, value);
}

bool UIStyleSettings::PreviewStyleBool(const char* key, bool fallback) {
  Bool value = fallback ? True : False;
  const auto path = std::string("style/") + key;
  rime_get_api()->config_get_bool(&original_, path.c_str(), &value);
  return value != False;
}

std::wstring UIStyleSettings::PreviewStyleString(const char* key,
                                                 const wchar_t* fallback) {
  const auto path = std::string("style/") + key;
  const auto value = Value(&original_, path);
  return value.empty() ? fallback : u8tow(value);
}

COLORREF UIStyleSettings::PreviewColor(const std::string& id,
                                       const char* key,
                                       COLORREF fallback) {
  auto* config = palettes_.Config(id);
  if (!config)
    return fallback;
  const std::string prefix =
      "preset_color_schemes/" + weasel::PaletteId(id) + "/";
  const auto text = Value(config, prefix + key);
  if (text.empty())
    return fallback;
  try {
    const auto color = std::stoull(text, nullptr, 0);
    const auto format = Value(config, prefix + "color_format");
    if (format == "rgba")
      return RGB((color >> 24) & 255, (color >> 16) & 255, (color >> 8) & 255);
    if (format == "argb")
      return RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
    return static_cast<COLORREF>(color & 0xffffff);
  } catch (...) {
    return fallback;
  }
}

uint32_t UIStyleSettings::PaletteRgba(const std::string& id,
                                      size_t role,
                                      bool dark) {
  if (role >= candidate_palette::kRoles.size())
    return 0;
  const auto& definition = candidate_palette::kRoles[role];
  const uint32_t fallback = dark ? definition.dark : definition.light;
  auto* config = palettes_.Config(id);
  if (!config)
    return fallback;
  const std::string prefix =
      "preset_color_schemes/" + weasel::PaletteId(id) + "/";
  auto value = Value(config, prefix + definition.key);
  if (value.empty()) {
    switch (role) {
      case 0:
        return 0xffffffffu;
      case 1:
        return PaletteRgba(id, 3, dark);
      case 2:
        return 0;
      case 3:
        return 0x000000ffu;
      case 4:
        return PaletteRgba(id, 3, dark);
      case 5:
        return PaletteRgba(id, 0, dark);
      case 6:
        return PaletteRgba(id, 3, dark);
      case 7:
        return BlendRgba(PaletteRgba(id, 6, dark), PaletteRgba(id, 9, dark));
      case 8:
        return PaletteRgba(id, 7, dark);
      case 9:
        return 0;
      case 10:
        return PaletteRgba(id, 4, dark);
      case 11:
        return BlendRgba(PaletteRgba(id, 10, dark), PaletteRgba(id, 13, dark));
      case 12:
        return PaletteRgba(id, 11, dark);
      case 13:
        return PaletteRgba(id, 5, dark);
      default:
        return 0;
    }
  }
  try {
    if (value.front() == '#')
      value = "0x" + value.substr(1);
    size_t parsed = 0;
    const uint64_t number = std::stoull(value, &parsed, 0);
    if (parsed != value.size() || number > 0xffffffffull)
      return fallback;
    const bool has_alpha = value.size() > 8;
    const auto format = Value(config, prefix + "color_format");
    const uint32_t raw = static_cast<uint32_t>(number);
    if (format == "rgba")
      return has_alpha ? raw : ((raw << 8) | 0xff);
    if (format == "argb") {
      const uint32_t argb = has_alpha ? raw : (0xff000000u | raw);
      return ((argb & 0xffffffu) << 8) | (argb >> 24);
    }
    const uint32_t abgr = has_alpha ? raw : (0xff000000u | raw);
    return ((abgr & 0xffu) << 24) | (((abgr >> 8) & 0xffu) << 16) |
           (((abgr >> 16) & 0xffu) << 8) | (abgr >> 24);
  } catch (...) {
    return fallback;
  }
}

bool UIStyleSettings::GetPresetColorSchemes(
    std::vector<ColorSchemeInfo>* result) {
  if (!result)
    return false;
  result->clear();
  RimeConfig config = {0};
  api_->settings_get_config(settings_, &config);
  RimeApi* rime = rime_get_api();
  RimeConfigIterator preset = {0};
  if (!rime->config_begin_map(&preset, &config, "preset_color_schemes")) {
    return false;
  }
  while (rime->config_next(&preset)) {
    RimeConfigIterator scheme = {0};
    if (!rime->config_begin_map(&scheme, &config, preset.path))
      continue;
    rime->config_end(&scheme);
    std::string name_key(preset.path);
    name_key += "/name";
    const char* name = rime->config_get_cstring(&config, name_key.c_str());
    std::string author_key(preset.path);
    author_key += "/author";
    const char* author = rime->config_get_cstring(&config, author_key.c_str());
    ColorSchemeInfo info;
    info.color_scheme_id = preset.key;
    info.name = name ? name : preset.key;
    if (author)
      info.author = author;
    result->push_back(info);
  }
  rime->config_end(&preset);
  return true;
}

// check if a file exists
static inline bool IfFileExist(std::string filename) {
  DWORD dwAttrib = GetFileAttributes(acptow(filename).c_str());
  return (INVALID_FILE_ATTRIBUTES != dwAttrib &&
          0 == (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

// get preview image from user dir first, then shared_dir
std::string UIStyleSettings::GetColorSchemePreview(
    const std::string& color_scheme_id) {
  if (color_scheme_id.empty())
    return {};
  std::string shared_dir = rime_get_api()->get_shared_data_dir();
  std::string user_dir = rime_get_api()->get_user_data_dir();
  std::string filename =
      user_dir + "\\preview\\color_scheme_" + color_scheme_id + ".png";
  if (IfFileExist(filename))
    return filename;
  else
    return (shared_dir + "\\preview\\color_scheme_" + color_scheme_id + ".png");
}

std::string UIStyleSettings::GetActiveColorScheme() {
  RimeConfig config = {0};
  api_->settings_get_config(settings_, &config);
  const char* value = rime_get_api()->config_get_cstring(
      &config, weasel::ColorSchemeConfigKey(target_));
  if (!value)
    return std::string();
  return std::string(value);
}

bool UIStyleSettings::SelectColorScheme(const std::string& color_scheme_id) {
  return !!api_->customize_string(settings_,
                                  weasel::ColorSchemeConfigKey(target_),
                                  color_scheme_id.c_str());
}
