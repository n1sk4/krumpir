#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <vector>

#include <ftxui/component/event.hpp>

#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

Color StateColor(const std::string &state)
{
  if (state == "RUNNING") {
    return Color::Green;
  }
  if (state == "FAULT") {
    return Color::Red;
  }
  if (state == "STOPPED") {
    return Color::Yellow;
  }
  if (state == "INITIALIZED") {
    return Color::Cyan;
  }
  return Color::GrayLight;
}

std::string Format(const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return buf;
}

std::string Number(double v)
{
  if (std::isnan(v)) {
    return "nan"; // not "-nan", which printf gives for a NaN with the sign set
  }
  return std::isinf(v) ? (v < 0 ? "-inf" : "inf") : Format("%.6g", v);
}

std::string Fit(const std::string &s, int width)
{
  if (width <= 0) {
    return "";
  }
  return static_cast<int>(s.size()) > width ? s.substr(0, static_cast<std::size_t>(width) - 1) + "~"
                                            : s;
}

InputOption OneLine()
{
  InputOption o;
  o.multiline = false;
  return o;
}

bool EscapeCloses(const Event &e, const std::function<void()> &close)
{
  if (e != Event::Escape) {
    return false;
  }
  close();
  return true;
}

namespace {

Element StatusBar(const Snapshot &v)
{
  Elements e{text(" krumpir-tui ") | bold | inverted, text("  runner "),
             text(v.address.empty() ? "-" : v.address),
             v.connected ? text("  * connected") | color(Color::Green)
                         : text("  o disconnected") | color(Color::Red)};
  const std::string &what = !v.loaded.empty() ? v.loaded : v.fmu;
  if (!what.empty()) {
    e.push_back(text("  | " + what));
  }
  if (!v.state.empty()) {
    e.push_back(text("  " + v.state) | bold | color(StateColor(v.state)));
  }
  if (v.busy) {
    e.push_back(text("  ...") | dim);
  }
  return hbox(std::move(e));
}

// The targets as vertical tabs: one card each, the name green while connected.
Component Cards(Model &m, const Controllers &ecus, int *selected)
{
  auto names = std::make_shared<std::vector<std::string>>(1, "Runner");
  for (const auto &e : ecus) {
    names->push_back(e.first);
  }
  constexpr int kWidth = 10; // ponytail: longer names are cut, the card stays narrow
  MenuOption opt = MenuOption::Vertical();
  opt.entries = names.get();
  opt.selected = selected;
  opt.focused_entry = selected; // shared by the Load and Monitor cards
  opt.entries_option.transform = [names, &m, ecus](const EntryState &e) {
    const bool up = e.index == 0
                        ? m.view([](const Snapshot &v) { return v.connected; })
                        : ecus[static_cast<std::size_t>(e.index - 1)].second->view(
                              [](const EcuSnapshot &v) { return v.connected; });
    return text(Fit(e.label, kWidth)) | color(up ? Color::Green : Color::GrayDark) |
           (e.active ? bold : nothing) | (e.focused ? inverted : nothing) |
           size(WIDTH, EQUAL, kWidth) | (e.active ? borderHeavy : borderLight);
  };
  return Menu(opt);
}

// A tab with the cards on the left and the picked target's page on the right. keys: letters
// typed on the cards go to the page (its shortcuts; not on pages with fields to type in).
Component WithCards(Component cards, Components pages, int *selected, bool keys)
{
  auto content = Container::Tab(pages, selected);
  auto view = Renderer(Container::Horizontal({cards, content}), [cards, content] {
    return hbox({window(text(" Targets "), cards->Render()), content->Render() | flex});
  });
  // To the page itself: a container drops events while it is not focused.
  return CatchEvent(view, [cards, pages, selected, keys](const Event &e) {
    return keys && cards->Focused() && e.is_character() &&
           pages[static_cast<std::size_t>(*selected)]->OnEvent(e);
  });
}

} // namespace

Component Root(Model &m, Controllers ecus, std::string address, std::function<void()> quit)
{
  struct State {
    int tab = 0;
    int target = 0; // 0: the runner, then the controllers
    int focus = 1;  // the tab's content, not the tab bar
    std::vector<std::string> names{"F1 Load", "F2 Monitor"};
    std::string keys[2][2] = {
        {" Tab/arrows move   Enter load   F2 monitor   F10 quit",
         " Tab/arrows move   Enter in the grid: write   F2 monitor   F10 quit"},
        {" s start  x stop  r reset  u unload  space check  c uncheck all  m one/many graphs  "
         "w/Enter write  g signal  v hide  [ ] resize  Esc close  F1 load  F10 quit",
         " s start  x stop  c clear trip  space/Enter plot a signal  m one/many graphs  "
         "v hide  [ ] resize  F1 load  F10 quit"}};
  };
  auto s = std::make_shared<State>();

  auto pane = std::make_shared<MonitorLayout>(); // one variables pane size for every Monitor page
  Components load{LoadTab(m, std::move(address), [s] { s->tab = 1; })},
      monitor{MonitorTab(m, pane)};
  for (const auto &e : ecus) {
    load.push_back(ControllerSetup(*e.second));
    monitor.push_back(ControllerMonitor(*e.second, pane));
  }
  // One entry per tab: the F-key, the title in s->names, the component here.
  auto tabs = Container::Tab({WithCards(Cards(m, ecus, &s->target), load, &s->target, false),
                              WithCards(Cards(m, ecus, &s->target), monitor, &s->target, true)},
                             &s->tab);
  auto bar = Toggle(&s->names, &s->tab);
  auto layout = Container::Vertical({bar, tabs}, &s->focus);

  auto root = Renderer(layout, [&m, s, bar, tabs] {
    return vbox({m.view(StatusBar), separator(), bar->Render(), separator(),
                 tabs->Render() | flex, separator(),
                 text(s->keys[s->tab][s->target > 0 ? 1 : 0]) | dim});
  });
  return CatchEvent(root, [s, quit](const Event &e) {
    if (e == Event::F1 || e == Event::F2) {
      s->tab = e == Event::F1 ? 0 : 1;
      return true;
    }
    if (e == Event::F10) {
      quit();
      return true;
    }
    return false;
  });
}

} // namespace krumpir::ui
