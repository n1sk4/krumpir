#include "layout.hpp"

#include <algorithm>

namespace krumpir
{

int ClampSplit(int wanted, int total, int min_left, int min_right)
{
  if (total <= 0) {
    return std::max(wanted, min_left);
  }
  const int most = std::max(min_left, total - 1 - min_right); // 1: the divider
  return std::clamp(wanted, min_left, most);
}

int NameWidth(int pane_width, int other_columns)
{
  return std::clamp(pane_width - 4 - other_columns, 8, 40);
}

} // namespace krumpir

namespace krumpir
{

namespace
{

// The window for the range remembered in v, no side effects.
TimeWindow Window(const PlotView &v)
{
  const double all = v.d1 - v.d0;
  const double span = v.span <= 0 || v.span >= all ? all : v.span;
  return v.follow ? TimeWindow{v.d1 - span, v.d1} : TimeWindow{v.end - span, v.end};
}

bool HasData(const PlotView &v) { return v.d1 > v.d0; }

} // namespace

TimeWindow Visible(PlotView &v, double d0, double d1)
{
  // ponytail: "cleared" = the newest time went back by more than 1% of the history; dropping the
  // series that ended last moves it back by a sample only
  if (std::isfinite(v.d1) && d1 < v.d1 - 0.01 * (v.d1 - v.d0)) {
    v.follow = true;
    v.a = v.b = NAN;
  }
  v.d0 = d0;
  v.d1 = d1;
  const TimeWindow w = Window(v);
  if (!v.follow && (w.t0 > d1 || w.t1 < d0)) {
    v.follow = true;
  }
  return Window(v);
}

void Zoom(PlotView &v, double factor, double around)
{
  if (!HasData(v)) {
    return;
  }
  const TimeWindow w = Window(v);
  const double all = v.d1 - v.d0;
  const double span = std::clamp((w.t1 - w.t0) * factor, std::min(kMinSpan, all), all);
  if (v.follow) { // a "last N seconds" view: the right edge stays at the newest sample
    v.span = span >= all ? 0 : span;
    return;
  }
  // Frozen: the time around stays at the same place on screen: the pointer, else the active
  // cursor when it is in view, else the centre
  const double c = v.on_b ? v.b : v.a;
  if (!std::isfinite(around)) {
    around = std::isfinite(c) && c >= w.t0 && c <= w.t1 ? c : (w.t0 + w.t1) / 2;
  }
  const double frac = w.t1 > w.t0 ? (around - w.t0) / (w.t1 - w.t0) : 0.5;
  const double t0 = std::clamp(around - frac * span, v.d0, v.d1 - span);
  v.span = span;
  v.end = t0 + span;
}

void Pan(PlotView &v, double fraction)
{
  if (!HasData(v)) {
    return;
  }
  const TimeWindow w = Window(v);
  const double span = w.t1 - w.t0;
  v.follow = false;
  v.span = span;
  v.end = std::clamp(w.t1 + fraction * span, v.d0 + span, v.d1);
}

void Freeze(PlotView &v)
{
  if (!v.follow || !HasData(v)) {
    v.follow = true;
    return;
  }
  const TimeWindow w = Window(v);
  v.follow = false;
  v.span = w.t1 - w.t0;
  v.end = w.t1;
}

void Whole(PlotView &v)
{
  v.follow = true;
  v.span = 0;
}

void Cursor(PlotView &v, bool b)
{
  double &c = b ? v.b : v.a;
  if (std::isfinite(c)) {
    c = NAN;
    v.on_b = !b && std::isfinite(v.b); // the other one, if it is on
    return;
  }
  if (HasData(v)) {
    const TimeWindow w = Window(v);
    Place(v, b, (w.t0 + w.t1) / 2);
  }
}

void Place(PlotView &v, bool b, double t)
{
  (b ? v.b : v.a) = t;
  v.on_b = b;
}

void Step(PlotView &v, int columns, int cols)
{
  double &c = v.on_b ? v.b : v.a;
  if (!std::isfinite(c) || !HasData(v)) {
    return;
  }
  const TimeWindow w = Window(v);
  c = std::clamp(c + columns * (w.t1 - w.t0) / std::max(1, cols), v.d0, v.d1);
}

double TimeAt(const PlotView &v, int column, int cols)
{
  const TimeWindow w = Window(v);
  return w.t0 + (column + 0.5) / std::max(1, cols) * (w.t1 - w.t0);
}

} // namespace krumpir
