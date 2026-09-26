#pragma once

#include <WeaselIPC.h>

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace weasel {

// Shared by the service notification icon and the TSF input-state icon.
class QuickSwitchMenu {
 public:
  using Select = std::function<bool(const QuickSwitchSnapshot&, int, int)>;

  void Populate(HMENU menu, QuickSwitchSnapshot snapshot) {
    actions_.clear();
    snapshot_ = std::move(snapshot);
    HMENU quick = ::CreatePopupMenu();
    if (!quick)
      return;

    const bool available = snapshot_.session_id && !snapshot_.groups.empty();
    const bool english = PRIMARYLANGID(GetThreadUILanguage()) != LANG_CHINESE;
    if (!available) {
      ::AppendMenuW(
          quick, MF_STRING | MF_GRAYED, 0,
          english ? L"No active input session" : L"没有可用的输入会话");
    } else {
      UINT next_id = 41000;
      for (const auto& group : snapshot_.groups) {
        if (group.states.size() < 2 || group.selected < 0 ||
            static_cast<size_t>(group.selected) >= group.states.size())
          continue;
        const int next = static_cast<int>(
            (static_cast<size_t>(group.selected) + 1) % group.states.size());
        // A tab lets the native menu align the current/next state on the right.
        const auto text = group.label + L"\t" + group.states[group.selected] +
                          L" → " + group.states[next];
        if (::AppendMenuW(quick, MF_STRING, next_id, text.c_str()))
          actions_[next_id++] = {group.schema_index, next};
      }
      if (actions_.empty())
        ::AppendMenuW(quick, MF_STRING | MF_GRAYED, 0,
                      english ? L"No available switches" : L"没有可用开关");
    }

    if (!::InsertMenuW(menu, 0,
                       MF_BYPOSITION | MF_POPUP | (available ? 0 : MF_GRAYED),
                       reinterpret_cast<UINT_PTR>(quick),
                       english ? L"Feature switches" : L"功能开关")) {
      ::DestroyMenu(quick);
      actions_.clear();
      return;
    }
    ::InsertMenuW(menu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
  }

  bool HandleCommand(UINT id, const Select& select) {
    const auto action = actions_.find(id);
    if (action == actions_.end())
      return false;
    if (select)
      select(snapshot_, action->second.schema_index, action->second.state);
    return true;
  }

 private:
  struct Action {
    int schema_index = -1;
    int state = -1;
  };

  QuickSwitchSnapshot snapshot_;
  std::map<UINT, Action> actions_;
};

}  // namespace weasel
