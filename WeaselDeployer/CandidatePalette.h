#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace candidate_palette {

struct Role {
  const char* key;
  const wchar_t* label;
  unsigned group;
  uint32_t light;
  uint32_t dark;
};

// Values are RRGGBBAA, the format written to user palette definitions.
inline constexpr std::array<Role, 22> kRoles{{
    {"back_color", L"背景", 0, 0xf9f9f9ff, 0x2c2c2cff},
    {"border_color", L"边框", 0, 0xd5d5d5ff, 0x505050ff},
    {"shadow_color", L"阴影", 0, 0x00000028, 0x00000055},
    {"text_color", L"普通输入码文字", 1, 0x202020ff, 0xf5f5f5ff},
    {"hilited_text_color", L"高亮输入码文字", 1, 0x111111ff, 0xffffffff},
    {"hilited_back_color", L"高亮输入码背景", 1, 0x00000000, 0x00000000},
    {"candidate_text_color", L"候选文字", 2, 0x202020ff, 0xf2f2f2ff},
    {"label_color", L"候选标签", 2, 0x686868ff, 0xb0b0b0ff},
    {"comment_text_color", L"注释文字", 2, 0x686868ff, 0xb0b0b0ff},
    {"candidate_back_color", L"背景", 2, 0x00000000, 0x00000000},
    {"hilited_candidate_text_color", L"候选文字", 3, 0x111111ff, 0xffffffff},
    {"hilited_label_color", L"候选标签", 3, 0x0067c0ff, 0x60cdffff},
    {"hilited_comment_text_color", L"注释文字", 3, 0x505050ff, 0xd0d0d0ff},
    {"hilited_candidate_back_color", L"背景", 3, 0xe5e5e5ff, 0x424242ff},
    {"hilited_candidate_border_color", L"边框", 3, 0x00000000, 0x00000000},
    {"hilited_mark_color", L"候选前标记", 3, 0x0067c0ff, 0x60cdffff},
    {"prevpage_color", L"上一页箭头", 4, 0x00000000, 0x00000000},
    {"nextpage_color", L"下一页箭头", 4, 0x00000000, 0x00000000},
    {"candidate_border_color", L"普通候选边框", 4, 0x00000000, 0x00000000},
    {"candidate_shadow_color", L"普通候选阴影", 4, 0x00000000, 0x00000000},
    {"hilited_candidate_shadow_color", L"选中候选阴影", 4, 0x00000000,
     0x00000000},
    {"hilited_shadow_color", L"高亮输入码阴影", 4, 0x00000000, 0x00000000},
}};

inline constexpr std::array<const wchar_t*, 5> kGroups{
    L"候选框", L"输入码", L"普通候选", L"选中候选", L"其它"};

using Colors = std::array<uint32_t, kRoles.size()>;

struct Draft {
  std::wstring name;
  std::array<std::string, 4> ids;
  std::array<std::string, 4> sources;
  std::array<Colors, 4> colors{};
  std::array<std::array<bool, kRoles.size()>, 4> edited{};
  std::array<bool, 4> changed{};
};

struct ImportedScheme {
  std::string source_id;
  std::string target_id;
  std::string name;
  bool dark = false;
  Colors colors{};
  bool unspecified = false;
};

struct ImportedGroup {
  std::string target_stem;
  std::string name;
  std::string light_id;
  std::string dark_id;
};

struct ImportPlan {
  std::string normalized_yaml;
  bool acrylic = true;
  std::vector<ImportedScheme> schemes;
  std::vector<ImportedGroup> groups;
};

}  // namespace candidate_palette
