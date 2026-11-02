#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/canvas.hpp>

#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

namespace {

Element Header(const Snapshot &v)
{
  if (v.state.empty() || v.state == "IDLE") {
    return text(" nothing loaded: pick a scenario or an FMU on F1") | dim;
  }
  const std::string t = v.duration > 0 ? Format("%.3f / %g s", v.t, v.duration)
                                       : Format("%.3f s", v.t);
  Elements e{text(" " + v.state + " ") | bold | color(StateColor(v.state)),
             text("  t " + t),
             text(Format("  steps %.0f", v.steps)),
             text(Format("  overruns %.0f", v.overruns)) |
                 color(v.overruns > 0 ? Color(Color::Red) : Color(Color::Default)),
             text(Format("  step max %.1f us", v.max_step_ns / 1e3)),
             text(Format("  wake max %.1f us", v.max_wake_ns / 1e3))};
  if (v.state == "FAULT") {
    e.push_back(text("  " + v.fault) | color(Color::Red));
  }
  return hbox(std::move(e));
}

// The samples of one variable over the time they cover, scaled to the box.
Element Plot(std::vector<Sample> h)
{
  if (h.size() < 2) {
    return text("samples come in while it runs") | dim | center;
  }
  double lo = INFINITY, hi = -INFINITY;
  for (const auto &p : h) {
    if (std::isfinite(p.v)) {
      lo = std::min(lo, p.v);
      hi = std::max(hi, p.v);
    }
  }
  if (!(lo <= hi)) {
    return text("no finite values") | dim | center;
  }
  if (hi - lo < 1e-12 * std::max(1.0, std::fabs(hi))) { // flat: centre the line
    lo -= 1;
    hi += 1;
  }
  const double t0 = h.front().t, t1 = h.back().t;
  auto plot = canvas([h, lo, hi, t0, t1](Canvas &c) {
    const double w = c.width() - 1, ht = c.height() - 1;
    auto x = [&](double t) { return static_cast<int>((t - t0) / (t1 - t0) * w); };
    auto y = [&](double v) { return static_cast<int>((hi - v) / (hi - lo) * ht); };
    for (std::size_t i = 1; i < h.size(); i++) {
      if (std::isfinite(h[i - 1].v) && std::isfinite(h[i].v)) {
        c.DrawPointLine(x(h[i - 1].t), y(h[i - 1].v), x(h[i].t), y(h[i].v), Color::Cyan);
      }
    }
  });
  return vbox({text(Format("%.6g", hi)) | dim, plot | flex,
               hbox({text(Format("%.6g", lo)) | dim, filler(),
                     text(Format("t %.3f .. %.3f s", t0, t1)) | dim})});
}

} // namespace

Component MonitorTab(Model &m)
{
  struct State {
    std::vector<std::string> rows;
    int selected = 0;
  };
  auto s = std::make_shared<State>();

  auto start = Button("Start", [&m] { m.start(); }, ButtonOption::Ascii());
  auto stop = Button("Stop", [&m] { m.stop(); }, ButtonOption::Ascii());
  auto unload = Button("Unload", [&m] { m.unload(); }, ButtonOption::Ascii());
  MenuOption menu = MenuOption::Vertical();
  menu.entries = &s->rows;
  menu.selected = &s->selected;
  auto vars = Menu(menu);
  auto layout = Container::Vertical({vars, Container::Horizontal({start, stop, unload})});

  auto view = Renderer(layout, [=, &m] {
    Element header, events;
    std::string plotted;
    std::vector<Sample> samples;
    m.view([&](const Snapshot &v) {
      s->rows.clear();
      for (const auto &x : v.vars) {
        s->rows.push_back(Format("%-24.24s %-6s %13.6g", x.name.c_str(), x.kind.c_str(), x.value));
      }
      s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->rows.size()) - 1));
      if (!v.vars.empty()) {
        plotted = v.vars[static_cast<std::size_t>(s->selected)].name;
        const auto it = v.history.find(plotted);
        if (it != v.history.end() && !it->second.empty()) { // at most ~4000 points to draw
          const auto &h = it->second;
          const std::size_t stride = std::max<std::size_t>(1, h.size() / 4000);
          for (std::size_t i = 0; i < h.size(); i += stride) {
            samples.push_back(h[i]);
          }
          if (samples.back().t != h.back().t) {
            samples.push_back(h.back());
          }
        }
      }
      header = Header(v);
      Elements lines;
      for (const auto &e : v.events) {
        const bool bad = e.find("error") != std::string::npos ||
                         e.find("fault") != std::string::npos ||
                         e.find("disconnected") != std::string::npos;
        lines.push_back(text(e) | (bad ? color(Color::Red) : nothing));
      }
      events = vbox(std::move(lines)) | focusPositionRelative(0, 1) | yframe;
      return 0;
    });

    auto vars_pane =
        window(text(" Variables "),
               vbox({text(Format("  %-24s %-6s %13s", "name", "kind", "value")) | dim,
                     vars->Render() | vscroll_indicator | frame | flex})) |
        size(WIDTH, EQUAL, 50);
    auto plot_pane = window(text(" Plot: " + (plotted.empty() ? "-" : plotted) + " "),
                            Plot(std::move(samples))) |
                     flex;
    return vbox({
        hbox({header | flex, start->Render(), text(" "), stop->Render(), text(" "),
              unload->Render(), text(" ")}),
        hbox({vars_pane, plot_pane}) | flex,
        window(text(" Events "), events) | size(HEIGHT, EQUAL, 8),
    });
  });
  return CatchEvent(view, [&m](const Event &e) {
    if (e == Event::Character('s')) {
      m.start();
    } else if (e == Event::Character('x')) {
      m.stop();
    } else if (e == Event::Character('u')) {
      m.unload();
    } else {
      return false;
    }
    return true;
  });
}

} // namespace krumpir::ui
