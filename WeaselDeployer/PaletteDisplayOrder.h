#pragma once

#include <windows.h>
#include <string>

namespace palette_display {

// Sort by the name shown to the user, with the stable scheme ID as a tie break.
inline bool NameLess(const std::wstring& left_name,
                     const std::string& left_id,
                     const std::wstring& right_name,
                     const std::string& right_id) {
  const int order = ::CompareStringEx(
      LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
      left_name.c_str(), static_cast<int>(left_name.size()), right_name.c_str(),
      static_cast<int>(right_name.size()), nullptr, nullptr, 0);
  if (order == CSTR_LESS_THAN || order == CSTR_GREATER_THAN)
    return order == CSTR_LESS_THAN;
  const int ordinal = ::CompareStringOrdinal(
      left_name.c_str(), static_cast<int>(left_name.size()), right_name.c_str(),
      static_cast<int>(right_name.size()), FALSE);
  if (ordinal == CSTR_LESS_THAN || ordinal == CSTR_GREATER_THAN)
    return ordinal == CSTR_LESS_THAN;
  return left_id < right_id;
}

}  // namespace palette_display
