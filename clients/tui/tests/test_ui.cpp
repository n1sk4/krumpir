// The screens drawn without a terminal: a FTXUI Screen of a fixed size, events sent to the
// component. Covers the variables pane of the Monitor pages (hide, resize, drag, limits).
#include <chrono>
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

namespace {

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
    auto body = Renderer(Container::Vertical({split->component(), rest}, split->focus()),
                         [s = split] {
                           s->frame();
                           return vbox({s->render(window(text(" Plot "), text("curve") | flex)),
                                        text("buttons")});
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

namespace {

// The whole frame with a runner and one controller, none of them connected.
struct Rig {
  krumpir::Model m{[] {}, 10ms};
  krumpir::EcuModel ecu{[] {}, 10ms, 300ms};
  Component root;

  Rig() { root = krumpir::ui::Root(m, {{"Motor", &ecu}}, "127.0.0.1:1", [] {}); }
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

TEST_CASE("ui root: the Load tab of the runner and of a controller both have Connect and Disconnect")
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
