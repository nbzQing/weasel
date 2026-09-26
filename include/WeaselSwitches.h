#pragma once

#include <rime_api.h>
#include <string>
#include <vector>

namespace weasel {

struct SwitchGroup {
  int index = -1;
  std::string name;
  std::vector<std::string> options;
  std::vector<std::string> states;
  int reset = -1;
};

inline std::string SwitchConfigString(RimeApi* api,
                                      RimeConfig* config,
                                      const std::string& path) {
  char value[512] = {};
  return api->config_get_string(config, path.c_str(), value, sizeof(value))
             ? value
             : std::string();
}

inline std::vector<SwitchGroup> LoadSwitchGroups(RimeApi* api,
                                                 const std::string& schema_id) {
  std::vector<SwitchGroup> groups;
  if (!api || schema_id.empty())
    return groups;
  RimeConfig config{};
  if (!api->config_open((schema_id + ".schema").c_str(), &config))
    return groups;
  const auto count = api->config_list_size(&config, "switches");
  for (size_t i = 0; i < count && i < 128; ++i) {
    const auto prefix = "switches/@" + std::to_string(i);
    SwitchGroup group;
    group.index = static_cast<int>(i);
    group.name = SwitchConfigString(api, &config, prefix + "/name");
    const auto option_count =
        api->config_list_size(&config, (prefix + "/options").c_str());
    if (option_count) {
      for (size_t j = 0; j < option_count && j < 32; ++j)
        group.options.push_back(SwitchConfigString(
            api, &config, prefix + "/options/@" + std::to_string(j)));
    } else if (!group.name.empty()) {
      group.options.push_back(group.name);
    }
    const auto state_count =
        api->config_list_size(&config, (prefix + "/states").c_str());
    for (size_t j = 0; j < state_count && j < 32; ++j)
      group.states.push_back(SwitchConfigString(
          api, &config, prefix + "/states/@" + std::to_string(j)));
    api->config_get_int(&config, (prefix + "/reset").c_str(), &group.reset);
    if (!group.options.empty() &&
        (group.options.size() > 1 || group.states.size() >= 2))
      groups.push_back(std::move(group));
  }
  api->config_close(&config);
  return groups;
}

inline size_t SwitchStateCount(const SwitchGroup& group) {
  return group.options.size() > 1 ? group.options.size() : 2;
}

inline std::string SwitchDisplayName(const SwitchGroup& group, bool english) {
  const auto& option = group.options.front();
  struct Label {
    const char* option;
    const char* chinese;
    const char* english;
  };
  constexpr Label labels[] = {
      {"ascii_mode", "中英文输入", "Chinese / English input"},
      {"ascii_punct", "中英文标点", "Chinese / English punctuation"},
      {"full_shape", "全角与半角", "Full / half width"},
      {"emoji", "表情", "Emoji"},
      {"chinese_english", "翻译", "Translation"},
      {"raw_input", "原编码与全拼", "Raw code / full Pinyin"},
      {"s2s", "简繁转换", "Chinese conversion"},
      {"abbrev", "简码", "Abbreviations"},
      {"comment_off", "注释提示", "Candidate hints"},
      {"char_priority", "候选优先级", "Candidate priority"},
      {"english", "英文词语", "English words"},
  };
  for (const auto& label : labels) {
    if (option == label.option)
      return english ? label.english : label.chinese;
  }
  return group.name.empty() ? option : group.name;
}

}  // namespace weasel
