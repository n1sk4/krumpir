#pragma once

#include <cmath>

// The size of the variables pane on the Monitor pages. No UI types here, so it can be tested.
namespace krumpir
{

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

namespace krumpir
{

// What the plot of a Monitor page shows in time: following the newest sample or frozen, how many
// seconds, and two cursors. The model keeps collecting while it is frozen; this is only the view.
struct PlotView {
  bool follow = true;          // the window ends at the newest sample
  double span = 0;             // seconds shown; 0: the whole history
  double end = 0;              // the right edge while frozen
  double a = NAN, b = NAN;     // the cursors' times, s; NaN: off
  bool on_b = false;           // the cursor , and . move: the last one placed
  double d0 = NAN, d1 = NAN;   // the time the data covers, as last drawn
};

struct TimeWindow {
  double t0, t1;
};

constexpr double kMinSpan = 1e-4; // s; ponytail: fixed floor, the runner steps at >= 1e-4 s

// The window to draw for data covering [d0, d1]; remembers the range. The newest time going back
// means the history was cleared (reset, reload, reconnect): following again, cursors off. A
// frozen window the data has left (the oldest samples dropped) follows again too.
TimeWindow Visible(PlotView &v, double d0, double d1);
void Zoom(PlotView &v, double factor, double around); // < 1: in. Around a time, NaN: see .cpp
void Pan(PlotView &v, double fraction);               // of the window; freezes
void Freeze(PlotView &v);                             // freezes what is shown, or follows again
void Whole(PlotView &v);                              // the whole history, following
void Cursor(PlotView &v, bool b);                     // A or B: on at the centre, or off
void Place(PlotView &v, bool b, double t);            // A or B at t
void Step(PlotView &v, int columns, int cols);        // the active cursor, cols: the plot's width
double TimeAt(const PlotView &v, int column, int cols); // the time under a column of the plot

} // namespace krumpir
