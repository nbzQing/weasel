#pragma once

#include <WeaselIPC.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace weasel {

// The named-pipe response is UTF-16. Length-prefixed fields preserve labels
// from schema files without giving tabs or newlines special meaning.
inline void AppendQuickSwitchNumber(std::wstring& output, uint32_t value) {
  output += std::to_wstring(value);
  output += L'\n';
}

inline void AppendQuickSwitchField(std::wstring& output,
                                   const std::wstring& value) {
  AppendQuickSwitchNumber(output, static_cast<uint32_t>(value.size()));
  output += value;
  output += L'\n';
}

inline std::wstring SerializeQuickSwitchSnapshot(
    const QuickSwitchSnapshot& snapshot) {
  std::wstring output;
  AppendQuickSwitchNumber(output, 1);  // wire format version
  AppendQuickSwitchNumber(output, snapshot.session_id);
  AppendQuickSwitchField(output, u8tow(snapshot.schema_id));
  AppendQuickSwitchNumber(output,
                          static_cast<uint32_t>(snapshot.groups.size()));
  for (const auto& group : snapshot.groups) {
    AppendQuickSwitchNumber(output, static_cast<uint32_t>(group.schema_index));
    AppendQuickSwitchNumber(output, static_cast<uint32_t>(group.selected));
    AppendQuickSwitchField(output, group.label);
    AppendQuickSwitchNumber(output, static_cast<uint32_t>(group.states.size()));
    for (const auto& state : group.states)
      AppendQuickSwitchField(output, state);
  }
  return output;
}

inline bool ReadQuickSwitchNumber(std::wstring_view wire,
                                  size_t& cursor,
                                  uint32_t& value) {
  const size_t end = wire.find(L'\n', cursor);
  if (end == std::wstring_view::npos || end == cursor || end - cursor > 10)
    return false;
  uint64_t number = 0;
  for (size_t i = cursor; i < end; ++i) {
    if (wire[i] < L'0' || wire[i] > L'9')
      return false;
    number = number * 10 + static_cast<uint32_t>(wire[i] - L'0');
    if (number > (std::numeric_limits<uint32_t>::max)())
      return false;
  }
  value = static_cast<uint32_t>(number);
  cursor = end + 1;
  return true;
}

inline bool ReadQuickSwitchField(std::wstring_view wire,
                                 size_t& cursor,
                                 std::wstring& value) {
  uint32_t length = 0;
  if (!ReadQuickSwitchNumber(wire, cursor, length) ||
      length >= wire.size() - cursor || wire[cursor + length] != L'\n')
    return false;
  value.assign(wire.substr(cursor, length));
  cursor += length + 1;
  return true;
}

inline bool ParseQuickSwitchSnapshot(std::wstring_view wire,
                                     QuickSwitchSnapshot& snapshot) {
  QuickSwitchSnapshot parsed;
  size_t cursor = 0;
  uint32_t version = 0;
  uint32_t session_id = 0;
  uint32_t count = 0;
  std::wstring schema_id;
  if (!ReadQuickSwitchNumber(wire, cursor, version) || version != 1 ||
      !ReadQuickSwitchNumber(wire, cursor, session_id) ||
      !ReadQuickSwitchField(wire, cursor, schema_id) ||
      !ReadQuickSwitchNumber(wire, cursor, count) || count > 512)
    return false;
  parsed.session_id = session_id;
  parsed.schema_id = wtou8(schema_id);
  for (uint32_t i = 0; i < count; ++i) {
    QuickSwitchGroup group;
    uint32_t index = 0;
    uint32_t selected = 0;
    uint32_t states = 0;
    if (!ReadQuickSwitchNumber(wire, cursor, index) ||
        index > static_cast<uint32_t>((std::numeric_limits<int>::max)()) ||
        !ReadQuickSwitchNumber(wire, cursor, selected) ||
        !ReadQuickSwitchField(wire, cursor, group.label) ||
        !ReadQuickSwitchNumber(wire, cursor, states) || states < 2 ||
        states > 64 || selected >= states)
      return false;
    group.schema_index = static_cast<int>(index);
    group.selected = static_cast<int>(selected);
    for (uint32_t j = 0; j < states; ++j) {
      std::wstring state;
      if (!ReadQuickSwitchField(wire, cursor, state))
        return false;
      group.states.push_back(std::move(state));
    }
    parsed.groups.push_back(std::move(group));
  }
  if (cursor != wire.size())
    return false;
  snapshot = std::move(parsed);
  return true;
}

}  // namespace weasel
