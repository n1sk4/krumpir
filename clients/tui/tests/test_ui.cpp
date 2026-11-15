// The screens drawn without a terminal: a FTXUI Screen of a fixed size, events sent to the
// component. Covers the variables pane of the Monitor pages (hide, resize, drag, limits).
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <string>

#include <doctest/doctest.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "ecu_model.hpp"
#include "model.hpp"
#include "split.hpp"
#include "ui.hpp"

using namespace ftxui;
using namespace std::chrono_literals;

namespace
{

// Draws twice: the second frame sees the sizes the first one measured.
Screen draw(const Component &c, int w, int h)
{
  Screen s = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
  for (int i = 0; i < 2; i++) {
    s = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(s, c->Render());
  }
  return s;
}

// The column where the ASCII text t starts on some row, or -1.
int col_of(const Screen &s, const std::string &t)
{
  for (int y = 0; y < s.dimy(); y++) {
    for (int x = 0; x + static_cast<int>(t.size()) <= s.dimx(); x++) {
      bool same = true;
      for (std::size_t i = 0; i < t.size() && same; i++) {
        same = s.PixelAt(x + static_cast<int>(i), y).character == std::string(1, t[i]);
      }
      if (same) {
        return x;
      }
    }
  }
  return -1;
}

bool has(const Screen &s, const std::string &t) { return col_of(s, t) >= 0; }

// A window of width w ends with this corner on its top row.
bool pane_ends_at(const Screen &s, int w) { return s.PixelAt(w - 1, 0).character == "╮"; }

Event mouse(int x, int y, Mouse::Motion motion)
{
  Mouse m;
  m.button = Mouse::Left;
  m.motion = motion;
  m.x = x;
  m.y = y;
  return Event::Mouse("", m);
}

// The pane and plot of a Monitor page on their own: a pane of fixed text, a plot, a spare row.
struct Page {
  std::shared_ptr<krumpir::MonitorLayout> lay = std::make_shared<krumpir::MonitorLayout>();
  std::shared_ptr<krumpir::ui::PaneSplit> split;
  Component page;

  Page()
  {
    split = std::make_shared<krumpir::ui::PaneSplit>(
        lay, Renderer([] { return window(text(" Pane "), text("rows") | flex); }));
    auto rest = Renderer([] { return text("buttons"); });
    auto body =
        Renderer(Container::Vertical({split->component(), rest}, split->focus()), [s = split] {
          s->frame();
          return vbox({s->render(window(text(" Plot "), text("curve") | flex)), text("buttons")});
        });
    page = CatchEvent(body, [s = split](const Event &e) { return s->on_key(e); });
  }
};

} // namespace

TEST_CASE("ui split: the pane starts at its default width and the plot gets the rest")
{
  Page p;
  const Screen s = draw(p.page, 120, 10);
  CHECK(pane_ends_at(s, 54));
  CHECK(has(s, " Pane "));
  CHECK(has(s, " Plot "));
  CHECK(col_of(s, " Plot ") > 54);
}

TEST_CASE("ui split: ] and [ resize by one step")
{
  Page p;
  draw(p.page, 120, 10);
  p.page->OnEvent(Event::Character(']'));
  CHECK(p.lay->vars_width == 54 + krumpir::kResizeStep);
  CHECK(pane_ends_at(draw(p.page, 120, 10), 54 + krumpir::kResizeStep));
  p.page->OnEvent(Event::Character('['));
  p.page->OnEvent(Event::Character('['));
  CHECK(p.lay->vars_width == 54 - krumpir::kResizeStep);
}

TEST_CASE("ui split: the pane stops at its minimum and leaves the plot its room")
{
  Page p;
  draw(p.page, 120, 10);
  for (int i = 0; i < 40; i++) {
    p.page->OnEvent(Event::Character('['));
  }
  CHECK(p.lay->vars_width == krumpir::kMinVarsWidth);
  for (int i = 0; i < 60; i++) {
    p.page->OnEvent(Event::Character(']'));
  }
  CHECK(p.lay->vars_width == 120 - 1 - krumpir::kMinPlotWidth);
  const Screen s = draw(p.page, 120, 10);
  CHECK(has(s, " Plot "));
}

TEST_CASE("ui split: a narrower terminal squeezes the pane, never the plot below its room")
{
  Page p;
  draw(p.page, 120, 10);
  p.page->OnEvent(Event::Character(']'));
  p.page->OnEvent(Event::Character(']'));
  const Screen s = draw(p.page, 70, 10);
  CHECK(p.lay->vars_width == 70 - 1 - krumpir::kMinPlotWidth);
  CHECK(pane_ends_at(s, p.lay->vars_width));
}

TEST_CASE("ui split: v hides the pane, the plot takes its room, and v brings it back")
{
  Page p;
  CHECK(has(draw(p.page, 120, 10), " Pane "));
  p.page->OnEvent(Event::Character('v'));
  const Screen hidden = draw(p.page, 120, 10);
  CHECK_FALSE(has(hidden, " Pane "));
  CHECK(col_of(hidden, " Plot ") < 5);
  p.page->OnEvent(Event::Character('v'));
  CHECK(has(draw(p.page, 120, 10), " Pane "));
}

TEST_CASE("ui split: resizing a hidden pane shows it again")
{
  Page p;
  draw(p.page, 120, 10);
  p.page->OnEvent(Event::Character('v'));
  p.page->OnEvent(Event::Character(']'));
  CHECK(has(draw(p.page, 120, 10), " Pane "));
  CHECK(p.lay->vars_width == 54 + krumpir::kResizeStep);
}

TEST_CASE("ui split: dragging the divider moves it, within the limits")
{
  Page p;
  draw(p.page, 120, 10);
  // The divider is the column after the pane (54 wide: columns 0 to 53).
  p.page->OnEvent(mouse(54, 3, Mouse::Pressed));
  p.page->OnEvent(mouse(64, 3, Mouse::Moved));
  p.page->OnEvent(mouse(64, 3, Mouse::Released));
  CHECK(p.lay->vars_width == 64);
  CHECK(pane_ends_at(draw(p.page, 120, 10), 64)); // also redraws, so the divider is at 64

  p.page->OnEvent(mouse(64, 3, Mouse::Pressed));
  p.page->OnEvent(mouse(2, 3, Mouse::Moved));
  p.page->OnEvent(mouse(2, 3, Mouse::Released));
  CHECK(p.lay->vars_width == krumpir::kMinVarsWidth);
  draw(p.page, 120, 10); // the divider moved: the next drag starts where it is now

  const int edge = krumpir::kMinVarsWidth;
  p.page->OnEvent(mouse(edge, 3, Mouse::Pressed));
  p.page->OnEvent(mouse(119, 3, Mouse::Moved));
  p.page->OnEvent(mouse(119, 3, Mouse::Released));
  CHECK(p.lay->vars_width == 120 - 1 - krumpir::kMinPlotWidth);
}

namespace
{

// The whole frame with a runner and one controller, none of them connected.
struct Rig {
  krumpir::Model m{[] {}, 10ms};
  krumpir::EcuModel ecu{[] {}, 10ms, 300ms};
  Component root;

  Rig()
  {
    root = krumpir::ui::Root(m, {{"Motor", &ecu}}, "127.0.0.1:1", [] {});
  }
};

} // namespace

TEST_CASE("ui root: the Monitor tab has the cards, the variables and the plot")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  const Screen s = draw(r.root, 130, 36);
  CHECK(has(s, " Targets "));
  CHECK(has(s, " Variables "));
  CHECK(has(s, "v hide"));
}

TEST_CASE("ui root: hiding the variables leaves the cards where they are")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  const int cards = col_of(draw(r.root, 130, 36), " Targets ");
  r.root->OnEvent(Event::Character('v'));
  const Screen s = draw(r.root, 130, 36);
  CHECK_FALSE(has(s, " Variables "));
  CHECK(col_of(s, " Targets ") == cards);
  r.root->OnEvent(Event::Character('v'));
  CHECK(has(draw(r.root, 130, 36), " Variables "));
}

TEST_CASE("ui root: the pane setting is shared by every Monitor page")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  draw(r.root, 130, 36);
  r.root->OnEvent(Event::Character('v')); // on the runner page
  r.root->OnEvent(Event::ArrowDown);      // the controller's card
  Screen s = draw(r.root, 130, 36);
  CHECK(has(s, "Motor"));
  CHECK_FALSE(has(s, " Signals"));
  r.root->OnEvent(Event::Character('v'));
  CHECK(has(draw(r.root, 130, 36), " Signals"));
}

TEST_CASE("ui number: six digits, and a word for what is not finite")
{
  using krumpir::ui::Number;
  CHECK(Number(0.5) == "0.5");
  CHECK(Number(2748.33) == "2748.33");
  CHECK(Number(1.0 / 3.0) == "0.333333");
  CHECK(Number(1e-9) == "1e-09");
  CHECK(Number(NAN) == "nan");
  CHECK(Number(-NAN) == "nan");
  CHECK(Number(INFINITY) == "inf");
  CHECK(Number(-INFINITY) == "-inf");
}

TEST_CASE("ui fit: a name is cut to the column and ends in ~ when it was longer")
{
  using krumpir::ui::Fit;
  CHECK(Fit("speed", 10) == "speed");
  CHECK(Fit("speed", 5) == "speed"); // exactly fits
  CHECK(Fit("speed_rpm", 5) == "spee~");
  CHECK(Fit("speed_rpm", 1) == "~");
  CHECK(Fit("speed_rpm", 0) == "");
}

TEST_CASE("ui dialogs: Esc closes, any other key goes to the dialog")
{
  using krumpir::ui::EscapeCloses;
  int closed = 0;
  const auto close = [&] { closed++; };
  CHECK_FALSE(EscapeCloses(Event::Character('a'), close));
  CHECK_FALSE(EscapeCloses(Event::Return, close));
  CHECK(closed == 0);
  CHECK(EscapeCloses(Event::Escape, close));
  CHECK(closed == 1);
}

TEST_CASE("ui value check: a number, and inside the limits when there are some")
{
  using krumpir::ui::CheckValue;
  double v = -1;
  CHECK(CheckValue("2.5", 0, 10, &v).empty());
  CHECK(v == 2.5);
  CHECK(CheckValue("0", 0, 10, &v).empty()); // the limits themselves are inside
  CHECK(CheckValue("10", 0, 10, &v).empty());
  CHECK(CheckValue("10.5", 0, 10, &v).find("outside 0..10") != std::string::npos);
  CHECK(CheckValue("-0.1", 0, 10, &v).find("outside") != std::string::npos);
  CHECK(CheckValue("abc", 0, 10, &v).find("not a number") != std::string::npos);
  CHECK(CheckValue("", 0, 10, &v).find("not a number") != std::string::npos);
  CHECK(CheckValue("nan", 0, 10, &v).find("not a number") != std::string::npos);
  v = -1;
  CHECK(CheckValue("abc", 0, 10, &v) != ""); // a refusal leaves the value alone
  CHECK(v == -1);
  // No limits (NaN): any finite number, as the runner's variables are today
  CHECK(CheckValue("12345", NAN, NAN, &v).empty());
  CHECK(v == 12345);
  CHECK(CheckValue("abc", NAN, NAN, &v) == "not a number");
  CHECK(CheckValue("1", 0, NAN, &v).empty()); // one limit missing: none
}

TEST_CASE("ui root: w on a controller that is not connected says so")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  r.root->OnEvent(Event::ArrowDown); // the controller's card
  r.root->OnEvent(Event::Character('w'));
  const Screen s = draw(r.root, 130, 40);
  CHECK(has(s, "nothing to write"));
  CHECK_FALSE(has(s, " Write a parameter "));
}

TEST_CASE("ui filter: a substring of the name, any case, empty shows all")
{
  using krumpir::ui::MatchesFilter;
  CHECK(MatchesFilter("speed_rpm", ""));
  CHECK(MatchesFilter("speed_rpm", "rpm"));
  CHECK(MatchesFilter("speed_rpm", "SPEED"));
  CHECK(MatchesFilter("Speed_RPM", "d_r"));
  CHECK_FALSE(MatchesFilter("speed_rpm", "duty"));
  CHECK_FALSE(MatchesFilter("", "x"));
}

TEST_CASE("ui root: the runner and the controller pages both have the filter box")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  CHECK(has(draw(r.root, 130, 36), "filter "));
  r.root->OnEvent(Event::ArrowDown); // the controller's card
  const Screen s = draw(r.root, 130, 36);
  CHECK(has(s, " Signals"));
  CHECK(has(s, "filter "));
}

TEST_CASE(
    "ui root: the Load tab of the runner and of a controller both have Connect and Disconnect")
{
  Rig r;
  Screen s = draw(r.root, 130, 36);
  CHECK(has(s, "Connect"));
  CHECK(has(s, "Disconnect"));
  r.root->OnEvent(Event::ArrowDown); // the controller's card
  s = draw(r.root, 130, 36);
  CHECK(has(s, "Connect"));
  CHECK(has(s, "Disconnect"));
}

TEST_CASE("ui root: the runner's file list on Load has the filter box too")
{
  Rig r;
  const Screen s = draw(r.root, 130, 40);
  CHECK(has(s, " Files on the runner "));
  CHECK(has(s, "filter "));
}

TEST_CASE("ui root: the Load tab of every target has the events pane")
{
  Rig r;
  CHECK(has(draw(r.root, 130, 40), " Events "));
  r.root->OnEvent(Event::ArrowDown); // the controller's card
  CHECK(has(draw(r.root, 130, 40), " Events "));
}

TEST_CASE("ui root: the Load tab is not affected by the pane keys")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  r.root->OnEvent(Event::Character('v'));
  r.root->OnEvent(Event::F1);
  const Screen s = draw(r.root, 130, 36);
  CHECK(has(s, " Targets "));
  CHECK_FALSE(has(s, " Variables "));
}

// The plot on its own, fed a history of its own: t in s, v = 100 t.
namespace
{

struct PlotRig {
  std::map<std::string, std::deque<krumpir::Sample>> history;
  std::vector<std::string> names{"speed"};
  krumpir::ui::PlotState p;
  bool single = false;
  Component c;

  PlotRig()
  {
    c = CatchEvent(Renderer([this] {
                     const auto d = krumpir::ui::Gather(history, names, p);
                     return krumpir::ui::PlotPane(d, p, single);
                   }),
                   [this](const Event &e) { return krumpir::ui::PlotKeys(e, p); });
    fill("speed", 10);
  }
  void fill(const std::string &name, double until) // on from the last sample, every 10 ms
  {
    auto &h = history[name];
    for (int i = static_cast<int>(h.size()); i * 0.01 <= until + 1e-9; i++) {
      h.push_back({i * 0.01, i * 1.0});
    }
  }
  int cols() const { return p.box.x_max - p.box.x_min + 1; }
};

Event button(int x, int y, Mouse::Button b)
{
  Mouse m;
  m.button = b;
  m.motion = Mouse::Pressed;
  m.x = x;
  m.y = y;
  return Event::Mouse("", m);
}

} // namespace

TEST_CASE("ui thin: the window and one sample either side, at most ~4000")
{
  using krumpir::ui::Thin;
  std::deque<krumpir::Sample> h;
  for (int i = 0; i < 100000; i++) {
    h.push_back({static_cast<double>(i), 2.0 * i});
  }
  auto t = Thin(h, 10, 20); // full resolution in a narrow window
  CHECK(t.size() == 13);
  CHECK(t.front().t == 9);
  CHECK(t.back().t == 21);
  t = Thin(h, 0, 99999);
  CHECK(t.size() <= 4001);
  CHECK(t.front().t == 0);
  CHECK(t.back().t == 99999); // the newest is always drawn
  CHECK(Thin(h, 200000, 300000).size() == 1); // past the data: the last sample, for the edge
  CHECK(Thin({}, 0, 1).empty());
}

TEST_CASE("ui nearest: the sample closest in time, the earlier one on a tie")
{
  using krumpir::ui::Nearest;
  const std::deque<krumpir::Sample> h{{0, 10}, {1, 11}, {2, 12}};
  CHECK(Nearest(h, 1.4)->t == 1);
  CHECK(Nearest(h, 1.6)->t == 2);
  CHECK(Nearest(h, 0.5)->t == 0);
  CHECK(Nearest(h, -5)->t == 0);
  CHECK(Nearest(h, 50)->t == 2);
  CHECK(Nearest(h, NAN) == nullptr);
  CHECK(Nearest({}, 1) == nullptr);
}

TEST_CASE("ui plot: + shows the last half, - and 0 the whole history again")
{
  PlotRig r;
  CHECK(has(draw(r.c, 100, 24), "t 0.000 .. 10.000 s"));
  CHECK(has(draw(r.c, 100, 24), " LIVE"));
  r.c->OnEvent(Event::Character('+'));
  CHECK(has(draw(r.c, 100, 24), "t 5.000 .. 10.000 s"));
  r.c->OnEvent(Event::Character('-'));
  CHECK(has(draw(r.c, 100, 24), "t 0.000 .. 10.000 s"));
  r.c->OnEvent(Event::Character('+'));
  r.c->OnEvent(Event::Character('0'));
  CHECK(has(draw(r.c, 100, 24), "t 0.000 .. 10.000 s"));
}

TEST_CASE("ui plot: f freezes the window while samples come in, f again follows them")
{
  PlotRig r;
  draw(r.c, 100, 24);
  r.c->OnEvent(Event::Character('+'));
  r.c->OnEvent(Event::Character('f'));
  r.fill("speed", 20);
  Screen s = draw(r.c, 100, 24);
  CHECK(has(s, " FROZEN"));
  CHECK(has(s, "t 5.000 .. 10.000 s"));
  r.c->OnEvent(Event::Character('f'));
  s = draw(r.c, 100, 24);
  CHECK(has(s, " LIVE"));
  CHECK(has(s, "t 15.000 .. 20.000 s"));
}

TEST_CASE("ui plot: a reads the nearest sample, b and a step give the difference")
{
  PlotRig r;
  draw(r.c, 100, 24);
  r.c->OnEvent(Event::Character('a')); // at the centre: t 5, v 500
  Screen s = draw(r.c, 100, 24);
  CHECK(has(s, "A 500"));
  CHECK(has(s, "A 5.0000 s"));
  r.c->OnEvent(Event::Character('b'));
  for (int i = 0; i < 10; i++) {
    r.c->OnEvent(Event::Character('.')); // B, the last placed, 10 columns right
  }
  const auto *b = krumpir::ui::Nearest(r.history["speed"], r.p.view.b);
  REQUIRE(b != nullptr);
  CHECK(b->t > 5);
  s = draw(r.c, 100, 24);
  CHECK(has(s, krumpir::ui::Format("dt %.4f s", b->t - 5)));
  CHECK(has(s, "B-A " + krumpir::ui::Number(b->v - 500)));
  r.c->OnEvent(Event::Character('a')); // A off: no difference any more
  CHECK_FALSE(has(draw(r.c, 100, 24), "dt "));
}

TEST_CASE("ui plot: a click places A, a right click B, the wheel zooms")
{
  PlotRig r;
  draw(r.c, 100, 24);
  const int x = r.p.box.x_min + 10, y = r.p.box.y_min + 5;
  const double t = krumpir::TimeAt(r.p.view, 10, r.cols());
  CHECK(r.c->OnEvent(button(x, y, Mouse::Left)));
  CHECK(r.p.view.a == doctest::Approx(t));
  CHECK(r.c->OnEvent(button(x + 20, y, Mouse::Right)));
  CHECK(r.p.view.b > r.p.view.a);
  CHECK_FALSE(r.c->OnEvent(button(r.p.box.x_max + 5, y, Mouse::Left))); // outside the plot
  r.c->OnEvent(button(x, y, Mouse::WheelUp));
  CHECK(has(draw(r.c, 100, 24), "t 5.000 .. 10.000 s")); // following: the right edge stays
}

TEST_CASE("ui plot: all in one graph has the readout in the legend")
{
  PlotRig r;
  r.names = {"speed", "current"};
  r.fill("current", 10);
  r.single = true;
  draw(r.c, 120, 24);
  r.c->OnEvent(Event::Character('a'));
  const Screen s = draw(r.c, 120, 24);
  CHECK(has(s, "-- current"));
  CHECK(has(s, "A 500"));
}

TEST_CASE("ui root: both Monitor pages have the plot's keys in its title")
{
  Rig r;
  r.root->OnEvent(Event::F2);
  CHECK(has(draw(r.root, 160, 36), "+- zoom"));
  r.root->OnEvent(Event::ArrowDown); // the controller's card
  CHECK(has(draw(r.root, 160, 36), "+- zoom"));
}
