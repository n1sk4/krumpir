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

} // namespace

Component Root(Model &m, std::string address, std::function<void()> quit)
{
  struct State {
    int tab = 0;
    int focus = 1; // the tab's content, not the tab bar
    std::vector<std::string> names{"F1 Load", "F2 Monitor"};
    std::vector<std::string> keys{
        " Tab/arrows move   Enter load   F2 monitor   F10 quit",
        " s start  x stop  u unload  up/down: variable  w/Enter write  g signal  Esc close  "
        "F1 load  F10 quit"};
  };
  auto s = std::make_shared<State>();

  // One entry per tab: the F-key, the title in s->names, the component here.
  auto tabs = Container::Tab({LoadTab(m, std::move(address), [s] { s->tab = 1; }), MonitorTab(m)},
                             &s->tab);
  auto bar = Toggle(&s->names, &s->tab);
  auto layout = Container::Vertical({bar, tabs}, &s->focus);

  auto root = Renderer(layout, [&m, s, bar, tabs] {
    return vbox({m.view(StatusBar), separator(), bar->Render(), separator(),
                 tabs->Render() | flex, separator(), text(s->keys[s->tab]) | dim});
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
