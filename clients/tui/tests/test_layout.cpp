// The variables pane sizing of the Monitor pages, and the plot's view in time.
#include <cmath>

#include <doctest/doctest.h>

#include "layout.hpp"

using namespace krumpir;

TEST_CASE("layout: the pane never goes under its minimum or takes the plot's room")
{
  CHECK(ClampSplit(54, 120, 36, 20) == 54);
  CHECK(ClampSplit(10, 120, 36, 20) == 36);  // too narrow
  CHECK(ClampSplit(200, 120, 36, 20) == 99); // 120 - 1 divider - 20 for the plot
  CHECK(ClampSplit(99, 120, 36, 20) == 99);  // exactly at the limit
  CHECK(ClampSplit(100, 120, 36, 20) == 99);
}

TEST_CASE("layout: a terminal too narrow for both gives the pane its minimum")
{
  CHECK(ClampSplit(54, 50, 36, 20) == 36);
}

TEST_CASE("layout: before the first frame the width is not known")
{
  CHECK(ClampSplit(54, 0, 36, 20) == 54);
  CHECK(ClampSplit(10, 0, 36, 20) == 36);
}

TEST_CASE("layout: name width follows the pane")
{
  CHECK(NameWidth(54, 24) == 26);           // runner rows: 3 + 1 + name + 1 + 6 + 1 + 12
  CHECK(NameWidth(54, 16) == 34);           // controller rows: 3 + 1 + name + 1 + 11
  CHECK(NameWidth(kMinVarsWidth, 24) == 8); // the narrowest pane still shows 8 characters
  CHECK(NameWidth(20, 24) == 8);
  CHECK(NameWidth(200, 16) == 40);
}

TEST_CASE("layout: the default pane keeps the old name widths")
{
  MonitorLayout l;
  CHECK(NameWidth(l.vars_width, 24) >= 22);
  CHECK(NameWidth(l.vars_width, 16) >= 32);
}

// The plot's view in time: following, frozen, zoom, pan, cursors.
using doctest::Approx;

TEST_CASE("plot view: following shows the whole history, zoomed the last seconds of it")
{
  PlotView v;
  TimeWindow w = Visible(v, 0, 10);
  CHECK(w.t0 == 0);
  CHECK(w.t1 == 10);
  Zoom(v, 0.5, NAN);
  CHECK(v.follow);
  w = Visible(v, 0, 20); // the right edge stays at the newest sample
  CHECK(w.t0 == Approx(15));
  CHECK(w.t1 == Approx(20));
  Whole(v);
  CHECK(Visible(v, 0, 20).t0 == 0);
}

TEST_CASE("plot view: frozen, the window stays while the data grows; f again follows")
{
  PlotView v;
  Visible(v, 0, 10);
  Zoom(v, 0.5, NAN);
  Freeze(v);
  TimeWindow w = Visible(v, 0, 30);
  CHECK_FALSE(v.follow);
  CHECK(w.t0 == Approx(5));
  CHECK(w.t1 == Approx(10));
  Freeze(v);
  w = Visible(v, 0, 30);
  CHECK(v.follow);
  CHECK(w.t0 == Approx(25));
  CHECK(w.t1 == Approx(30));
}

TEST_CASE("plot view: zoom while frozen keeps the cursor, or the pointer, where it is on screen")
{
  PlotView v;
  Visible(v, 0, 10);
  Freeze(v);
  Place(v, false, 2.0);
  Zoom(v, 0.5, NAN); // A at 20% of the window before and after
  TimeWindow w = Visible(v, 0, 10);
  CHECK(w.t0 == Approx(1));
  CHECK(w.t1 == Approx(6));
  Zoom(v, 0.5, 3.5); // the pointer in the middle
  w = Visible(v, 0, 10);
  CHECK(w.t0 == Approx(2.25));
  CHECK(w.t1 == Approx(4.75));
}

TEST_CASE("plot view: with no cursor in view a frozen zoom keeps the centre")
{
  PlotView v;
  Visible(v, 0, 10);
  Freeze(v);
  Place(v, false, 20); // outside the data, out of view
  Zoom(v, 0.5, NAN);
  const TimeWindow w = Visible(v, 0, 10);
  CHECK(w.t0 == Approx(2.5));
  CHECK(w.t1 == Approx(7.5));
}

TEST_CASE("plot view: pan freezes and stops at the ends of the data")
{
  PlotView v;
  Visible(v, 0, 10);
  Zoom(v, 0.5, NAN);
  Pan(v, -0.25);
  CHECK_FALSE(v.follow);
  TimeWindow w = Visible(v, 0, 10);
  CHECK(w.t0 == Approx(3.75));
  CHECK(w.t1 == Approx(8.75));
  for (int i = 0; i < 10; i++) {
    Pan(v, -1);
  }
  w = Visible(v, 0, 10);
  CHECK(w.t0 == Approx(0));
  CHECK(w.t1 == Approx(5));
  Pan(v, 100);
  w = Visible(v, 0, 10);
  CHECK(w.t0 == Approx(5));
  CHECK(w.t1 == Approx(10));
}

TEST_CASE("plot view: never narrower than the floor, never wider than the data")
{
  PlotView v;
  Visible(v, 0, 1);
  for (int i = 0; i < 40; i++) {
    Zoom(v, 0.5, NAN);
  }
  TimeWindow w = Visible(v, 0, 1);
  CHECK(w.t1 - w.t0 == Approx(kMinSpan));
  Zoom(v, 1e9, NAN);
  w = Visible(v, 0, 1);
  CHECK(v.span == 0); // following the whole history again
  CHECK(w.t0 == 0);
}

TEST_CASE("plot view: a cleared history follows again and drops the cursors")
{
  PlotView v;
  Visible(v, 0, 10);
  Freeze(v);
  Cursor(v, false);
  Visible(v, 0, 9.999); // a series that ended a sample earlier left the plot: kept
  CHECK_FALSE(v.follow);
  CHECK(std::isfinite(v.a));
  Visible(v, 0, 0.5); // reset: the run starts again from t = 0
  CHECK(v.follow);
  CHECK(std::isnan(v.a));
}

TEST_CASE("plot view: a frozen window the data has left follows again")
{
  PlotView v;
  Visible(v, 0, 10);
  Zoom(v, 0.5, NAN);
  Pan(v, -1); // [0, 5], frozen
  Visible(v, 6, 20); // the oldest samples dropped past it
  CHECK(v.follow);
}

TEST_CASE("plot view: a cursor comes on at the centre, goes off again; the last placed one steps")
{
  PlotView v;
  Visible(v, 0, 10);
  Cursor(v, false);
  CHECK(v.a == Approx(5));
  Cursor(v, true);
  CHECK(v.b == Approx(5));
  CHECK(v.on_b);
  Step(v, 1, 100); // one column of 100 over 10 s
  CHECK(v.b == Approx(5.1));
  CHECK(v.a == Approx(5));
  Cursor(v, true); // B off: A is the active one again
  CHECK(std::isnan(v.b));
  CHECK_FALSE(v.on_b);
  Step(v, -2, 100);
  CHECK(v.a == Approx(4.8));
  Place(v, false, 0);
  Step(v, -5, 100); // stops at the start of the data
  CHECK(v.a == 0);
}

TEST_CASE("plot view: the time under a column is its middle")
{
  PlotView v;
  Visible(v, 0, 10);
  CHECK(TimeAt(v, 0, 10) == Approx(0.5));
  CHECK(TimeAt(v, 9, 10) == Approx(9.5));
}
