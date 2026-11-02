#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <memory>
#include <set>
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

const Color kColors[] = {Color::Cyan, Color::Yellow, Color::Magenta, Color::Green, Color::Blue,
                         Color::Red};

// One variable's samples over [t0, t1] (shared by the stacked plots), scaled to the box.
Element Plot(const std::string &name, const std::vector<Sample> &h, double t0, double t1,
             Color colour)
{
  double lo = INFINITY, hi = -INFINITY;
  for (const auto &p : h) {
    if (std::isfinite(p.v)) {
      lo = std::min(lo, p.v);
      hi = std::max(hi, p.v);
    }
  }
  auto title = text(name) | bold | color(colour);
  if (h.size() < 2 || !(lo <= hi) || !(t1 > t0)) {
    return vbox({title, text(h.empty() ? "samples come in while it runs" : "not enough samples") |
                            dim | center | flex});
  }
  const double top = hi, bottom = lo;
  if (hi - lo < 1e-12 * std::max(1.0, std::fabs(hi))) { // flat: centre the line
    lo -= 1;
    hi += 1;
  }
  auto plot = canvas([h, lo, hi, t0, t1, colour](Canvas &c) {
    const double w = c.width() - 1, ht = c.height() - 1;
    auto x = [&](double t) { return static_cast<int>((t - t0) / (t1 - t0) * w); };
    auto y = [&](double v) { return static_cast<int>((hi - v) / (hi - lo) * ht); };
    for (std::size_t i = 1; i < h.size(); i++) {
      if (std::isfinite(h[i - 1].v) && std::isfinite(h[i].v)) {
        c.DrawPointLine(x(h[i - 1].t), y(h[i - 1].v), x(h[i].t), y(h[i].v), colour);
      }
    }
  });
  return vbox({hbox({title, filler(), text(Format("max %.6g", top)) | dim}), plot | flex,
               hbox({filler(), text(Format("min %.6g", bottom)) | dim})});
}

// Several variables in one graph, each scaled to its own min..max (their units differ), with a
// legend of the ranges.
Element Overlay(const std::vector<std::string> &names, const std::vector<std::vector<Sample>> &hs,
                double t0, double t1)
{
  std::vector<double> lo(hs.size(), INFINITY), hi(hs.size(), -INFINITY);
  Elements legend;
  for (std::size_t k = 0; k < hs.size(); k++) {
    for (const auto &p : hs[k]) {
      if (std::isfinite(p.v)) {
        lo[k] = std::min(lo[k], p.v);
        hi[k] = std::max(hi[k], p.v);
      }
    }
    const Color c = kColors[k % std::size(kColors)];
    legend.push_back(text("-- " + names[k]) | bold | color(c));
    legend.push_back(text(lo[k] <= hi[k] ? Format(" %.4g..%.4g   ", lo[k], hi[k]) : " -   ") |
                     dim);
  }
  if (!(t1 > t0)) {
    return vbox({hbox(std::move(legend)), text("samples come in while it runs") | dim | center |
                                              flex});
  }
  auto plot = canvas([hs, lo, hi, t0, t1](Canvas &c) {
    const double w = c.width() - 1, ht = c.height() - 1;
    for (std::size_t k = 0; k < hs.size(); k++) {
      const auto &h = hs[k];
      if (!(lo[k] <= hi[k])) {
        continue;
      }
      const double span = hi[k] - lo[k] > 0 ? hi[k] - lo[k] : 1.0;
      const double base = hi[k] - lo[k] > 0 ? lo[k] : lo[k] - 0.5; // flat: centred
      auto x = [&](double t) { return static_cast<int>((t - t0) / (t1 - t0) * w); };
      auto y = [&](double v) { return static_cast<int>((1.0 - (v - base) / span) * ht); };
      const Color col = kColors[k % std::size(kColors)];
      for (std::size_t i = 1; i < h.size(); i++) {
        if (std::isfinite(h[i - 1].v) && std::isfinite(h[i].v)) {
          c.DrawPointLine(x(h[i - 1].t), y(h[i - 1].v), x(h[i].t), y(h[i].v), col);
        }
      }
    }
  });
  return vbox({hbox(std::move(legend)), plot | flex});
}

// At most ~4000 points of a history, to draw.
std::vector<Sample> Thin(const std::deque<Sample> &h)
{
  std::vector<Sample> out;
  const std::size_t stride = std::max<std::size_t>(1, h.size() / 4000);
  for (std::size_t i = 0; i < h.size(); i += stride) {
    out.push_back(h[i]);
  }
  if (!h.empty() && out.back().t != h.back().t) {
    out.push_back(h.back());
  }
  return out;
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
    std::set<std::string> checked; // plotted; none: the selected one
    bool single = false;           // checked ones in one graph instead of one graph each
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
    int value_cursor = 0, field_cursor[4] = {}; // at the end of the filled-in text
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
      s->field_cursor[i] = static_cast<int>(s->fields[i].size());
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
    s->value_cursor = static_cast<int>(s->value.size());
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
  auto reset = Button("Reset", [&m] { m.reset(); }, ButtonOption::Ascii());
  auto unload = Button("Unload", [&m] { m.unload(); }, ButtonOption::Ascii());
  MenuOption menu = MenuOption::Vertical();
  menu.entries = &s->rows;
  menu.selected = &s->selected;
  menu.on_enter = open_write;
  auto vars = Menu(menu);
  auto layout =
      Container::Vertical({vars, Container::Horizontal({start, stop, reset, unload})});

  auto view = Renderer(layout, [=, &m] {
    Element header, events;
    std::vector<std::string> plotted;
    std::vector<std::vector<Sample>> samples;
    m.view([&](const Snapshot &v) {
      s->rows.clear();
      for (const auto &x : v.vars) {
        s->rows.push_back(Format("%s %-22.22s %-6s %12.6g", s->checked.count(x.name) ? "[x]" : "[ ]",
                                 x.name.c_str(), x.kind.c_str(), x.value));
        if (s->checked.count(x.name)) {
          plotted.push_back(x.name);
        }
      }
      s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->rows.size()) - 1));
      s->sel = v.vars.empty() ? Variable{} : v.vars[static_cast<std::size_t>(s->selected)];
      if (plotted.empty() && !s->sel.name.empty()) {
        plotted.push_back(s->sel.name);
      }
      for (const auto &name : plotted) {
        const auto it = v.history.find(name);
        samples.push_back(it != v.history.end() ? Thin(it->second) : std::vector<Sample>{});
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
               vbox({text(Format("      %-22s %-6s %12s", "name", "kind", "value")) | dim,
                     vars->Render() | vscroll_indicator | frame | flex})) |
        size(WIDTH, EQUAL, 50);
    // Stacked, one per variable, over the time all of them cover
    double t0 = INFINITY, t1 = -INFINITY;
    for (const auto &h : samples) {
      if (!h.empty()) {
        t0 = std::min(t0, h.front().t);
        t1 = std::max(t1, h.back().t);
      }
    }
    Elements plots;
    const bool single = s->single && plotted.size() > 1;
    if (single) {
      plots.push_back(Overlay(plotted, samples, t0, t1) | flex);
    }
    for (std::size_t i = 0; i < plotted.size() && !single; i++) {
      if (i > 0) {
        plots.push_back(separatorLight());
      }
      plots.push_back(Plot(plotted[i], samples[i], t0, t1, kColors[i % std::size(kColors)]) | flex);
    }
    if (plotted.empty()) {
      plots.push_back(text("nothing to plot") | dim | center | flex);
    } else if (t1 > t0) {
      plots.push_back(hbox({filler(), text(Format("t %.3f .. %.3f s", t0, t1)) | dim}));
    }
    auto plot_pane =
        window(text(s->single ? " Plot: one graph, each scaled to its range (m: one each) "
                              : " Plot: one graph each (m: all in one) "),
               vbox(std::move(plots))) |
        flex;
    return vbox({
        hbox({header | flex, start->Render(), text(" "), stop->Render(), text(" "),
              reset->Render(), text(" "), unload->Render(), text(" ")}),
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
  value_opt.cursor_position = &s->value_cursor;
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
    auto opt = field_opt;
    opt.cursor_position = &s->field_cursor[i];
    fields.push_back(Maybe(Input(&s->fields[i], "number", opt), shown));
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
    } else if (e == Event::Character('r')) {
      m.reset();
    } else if (e == Event::Character('w')) {
      open_write();
    } else if (e == Event::Character('g')) {
      open_signal();
    } else if (e == Event::Character(' ') && !s->sel.name.empty()) {
      if (!s->checked.erase(s->sel.name)) {
        s->checked.insert(s->sel.name);
      }
    } else if (e == Event::Character('c')) {
      s->checked.clear();
    } else if (e == Event::Character('m')) {
      s->single = !s->single;
    } else {
      return false;
    }
    return true;
  });
}

} // namespace krumpir::ui
