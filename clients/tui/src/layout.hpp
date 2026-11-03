#pragma once

// The size of the variables pane on the Monitor pages. No UI types here, so it can be tested.
namespace krumpir {

struct MonitorLayout {
  bool vars_shown = true;
  int vars_width = 54; // outer width of the variables pane, border included
};

constexpr int kMinVarsWidth = 36; // the shortest pane that still shows a name
constexpr int kMinPlotWidth = 20;
constexpr int kResizeStep = 4;

// The pane width to use: wanted, but at least min_left, and leaving the plot (and the divider) at
// least min_right of total. total <= 0 means the width is not known yet.
int ClampSplit(int wanted, int total, int min_left, int min_right);

// Room for a name in a pane of pane_width, when the rest of the row takes other columns. The pane
// border and the menu marker take 4 more.
int NameWidth(int pane_width, int other_columns);

} // namespace krumpir
