#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// The whole text as a finite number, or false.
bool ParseNumber(const std::string &s, double *out)
{
  const char *p = s.c_str();
  char *end = nullptr;
  *out = std::strtod(p, &end);
  while (end != nullptr && *end == ' ') {
    end++;
  }
  return end != p && end != nullptr && *end == '\0' && std::isfinite(*out);
}

} // namespace

Component MonitorTab(Model &m)
{
  struct State {
    std::vector<std::string> rows;
    int selected = 0;
    Variable sel;     // the selected row, as last rendered
    std::string hint; // one-line feedback under the header

    bool show_write = false, show_signal = false;
    Variable target; // what the open dialog writes to
    std::string value;
    std::vector<std::string> kinds;
    int kind = 0;
    std::string fields[4];
    std::string error;
    int write_row = 0, signal_row = 0; // focus in the dialogs: reset when one opens
  };
  auto s = std::make_shared<State>();
  for (const auto &p : presets()) {
    s->kinds.push_back(p.name);
  }

  auto current = [s] {
    return std::isfinite(s->target.value) ? Format("%.6g", s->target.value) : std::string("0");
  };
  auto fill_defaults = [s, current] {
    const Preset &p = presets()[static_cast<std::size_t>(s->kind)];
    for (std::size_t i = 0; i < 4; i++) {
      s->fields[i] = i < p.defaults.size() ? (p.defaults[i][0] ? p.defaults[i] : current()) : "";
    }
    s->error.clear();
  };
  auto open_write = [s, current] {
    if (s->sel.name.empty() || s->sel.kind == "output") {
      s->hint = s->sel.name.empty() ? "nothing to write" : s->sel.name + " is an output: read-only";
      return;
    }
    s->target = s->sel;
    s->value = current();
    s->error.clear();
    s->write_row = 0;
    s->show_write = true;
  };
  auto open_signal = [s, fill_defaults] {
    if (s->sel.kind != "input") {
      s->hint = "signals are for inputs; " +
                (s->sel.name.empty() ? "none selected" : s->sel.name + " is " + s->sel.kind);
      return;
    }
    s->target = s->sel;
    fill_defaults();
    s->signal_row = 0;
    s->show_signal = true;
  };
  auto apply_write = [&m, s] {
    double v;
    if (!ParseNumber(s->value, &v)) {
      s->error = "not a number";
      return;
    }
    m.write(s->target.name, v);
    s->show_write = false;
  };
  auto apply_signal = [&m, s] {
    const Preset &p = presets()[static_cast<std::size_t>(s->kind)];
    std::vector<double> f(p.fields.size());
    for (std::size_t i = 0; i < f.size(); i++) {
      if (!ParseNumber(s->fields[i], &f[i])) {
        s->error = std::string(p.fields[i]) + ": not a number";
        return;
      }
    }
    m.signal(preset(s->target.name, static_cast<std::size_t>(s->kind), f));
    s->show_signal = false;
  };
  auto close = [s] { s->show_write = s->show_signal = false; };

  // The run view
  auto start = Button("Start", [&m] { m.start(); }, ButtonOption::Ascii());
  auto stop = Button("Stop", [&m] { m.stop(); }, ButtonOption::Ascii());
  auto unload = Button("Unload", [&m] { m.unload(); }, ButtonOption::Ascii());
  MenuOption menu = MenuOption::Vertical();
  menu.entries = &s->rows;
  menu.selected = &s->selected;
  menu.on_enter = open_write;
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
      s->sel = v.vars.empty() ? Variable{} : v.vars[static_cast<std::size_t>(s->selected)];
      plotted = s->sel.name;
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
        s->hint.empty() ? text("") : text(" " + s->hint) | color(Color::Yellow),
        hbox({vars_pane, plot_pane}) | flex,
        window(text(" Events "), events) | size(HEIGHT, EQUAL, 8),
    });
  });

  // Write dialog: one value for an input or a tunable parameter
  InputOption one_line;
  one_line.multiline = false;
  auto value_opt = one_line;
  value_opt.on_enter = apply_write;
  auto value = Input(&s->value, "number", value_opt);
  auto write_buttons = Container::Horizontal({Button("Write", apply_write, ButtonOption::Ascii()),
                                              Button("Cancel", close, ButtonOption::Ascii())});
  auto write_dialog = Renderer(Container::Vertical({value, write_buttons}, &s->write_row), [=] {
    return window(text(" Write " + s->target.name + " (" + s->target.kind + ") "),
                  vbox({hbox({text(s->target.name + " = "),
                              value->Render() | underlined | size(WIDTH, GREATER_THAN, 14)}),
                        text(s->error) | color(Color::Red),
                        text(s->target.kind == "input" ? "replaces its signal from the next step"
                                                       : "from the next step") |
                            dim,
                        write_buttons->Render() | align_right})) |
           size(WIDTH, GREATER_THAN, 44);
  });

  // Signal dialog: a preset kind and its numbers
  MenuOption toggle = MenuOption::Toggle();
  toggle.entries = &s->kinds;
  toggle.selected = &s->kind;
  toggle.on_change = fill_defaults;
  auto kind = Menu(toggle);
  auto field_opt = one_line;
  field_opt.on_enter = apply_signal;
  Components fields{kind};
  for (std::size_t i = 0; i < 4; i++) {
    auto shown = [s, i] { return i < presets()[static_cast<std::size_t>(s->kind)].fields.size(); };
    fields.push_back(Maybe(Input(&s->fields[i], "number", field_opt), shown));
  }
  auto signal_buttons = Container::Horizontal({Button("Apply", apply_signal, ButtonOption::Ascii()),
                                               Button("Cancel", close, ButtonOption::Ascii())});
  fields.push_back(signal_buttons);
  auto signal_dialog = Renderer(Container::Vertical(fields, &s->signal_row), [=] {
    const Preset &p = presets()[static_cast<std::size_t>(s->kind)];
    Elements rows{kind->Render(), text("")};
    for (std::size_t i = 0; i < p.fields.size(); i++) {
      rows.push_back(hbox({text(Format("%-11s", p.fields[i])),
                           fields[i + 1]->Render() | underlined | size(WIDTH, GREATER_THAN, 14)}));
    }
    rows.push_back(text(s->error) | color(Color::Red));
    rows.push_back(text("starts at the next step, replaces its signal") | dim);
    rows.push_back(signal_buttons->Render() | align_right);
    return window(text(" Signal for " + s->target.name + " "), vbox(std::move(rows))) |
           size(WIDTH, GREATER_THAN, 50);
  });

  auto with_dialogs =
      Modal(Modal(view, write_dialog, &s->show_write), signal_dialog, &s->show_signal);
  return CatchEvent(with_dialogs, [&m, s, open_write, open_signal, close](const Event &e) {
    if (s->show_write || s->show_signal) { // the dialog gets the keys
      if (e == Event::Escape) {
        close();
        return true;
      }
      return false;
    }
    if (e.is_character()) {
      s->hint.clear();
    }
    if (e == Event::Character('s')) {
      m.start();
    } else if (e == Event::Character('x')) {
      m.stop();
    } else if (e == Event::Character('u')) {
      m.unload();
    } else if (e == Event::Character('w')) {
      open_write();
    } else if (e == Event::Character('g')) {
      open_signal();
    } else {
      return false;
    }
    return true;
  });
}

} // namespace krumpir::ui
