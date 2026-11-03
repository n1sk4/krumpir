// The variables pane sizing of the Monitor pages.
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
  CHECK(NameWidth(54, 24) == 26); // runner rows: 3 + 1 + name + 1 + 6 + 1 + 12
  CHECK(NameWidth(54, 16) == 34); // controller rows: 3 + 1 + name + 1 + 11
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
