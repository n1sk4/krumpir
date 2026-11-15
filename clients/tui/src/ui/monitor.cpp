#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include <ftxui/component/event.hpp>

#include "split.hpp"
#include "ui.hpp"

namespace krumpir::ui
{

using namespace ftxui;

namespace
{

Element Header(const Snapshot &v)
{
  if (v.state.empty() || v.state == "IDLE") {
    return text(" nothing loaded: pick a scenario or an FMU on F1") | dim;
  }
  const std::string t =
      v.duration > 0 ? Format("%.3f / %g s", v.t, v.duration) : Format("%.3f s", v.t);
  Elements e{text(" " + v.state + " ") | bold | color(StateColor(v.state)),
             text("  t " + t),
             text(Format("  steps %.0f", v.steps)),
             text(Format("  overruns %.0f", v.overruns)) |
                 color(v.overruns > 0 ? Color(Color::Red) : Color(Color::Default)),
             text(Format("  step max %.1f us", v.max_step_ns / 1e3)),
             text(Format("  wake max %.1f us", v.max_wake_ns / 1e3)),
             text(v.late > 0 ? Format("  late %.0f", v.late) : "") | color(Color::Yellow)};
  if (v.state == "FAULT") {
    e.push_back(text("  " + v.fault) | color(Color::Red));
  }
  return hbox(std::move(e));
}

// A connected controller's watched signal as a row of the runner's list: "<card>.<signal>".
struct CtrlRow {
  std::string name, signal;
  double value;
  const EcuSnapshot *e;
};

// Calls f with the controller rows, every controller's lock held. Locks: the Model's first (the
// caller is in m.view), then each EcuModel's in order, only on the UI thread; no worker takes
// another model's lock, so this cannot deadlock.
void ViewEcus(const Controllers &ecus, std::size_t i, std::vector<CtrlRow> &rows,
              const std::function<void()> &f)
{
  if (i == ecus.size()) {
    f();
    return;
  }
  ecus[i].second->view([&](const EcuSnapshot &e) {
    if (e.connected && std::isfinite(e.origin)) {
      for (const auto &w : e.watched) {
        const auto val = e.values.find(w);
        rows.push_back({ecus[i].first + "." + w, w, val != e.values.end() ? val->second : NAN, &e});
      }
    }
    ViewEcus(ecus, i + 1, rows, f);
    return 0;
  });
}

// Why the controller rows are not plotted on the run's t, "" when they are.
std::string Unmerged(const Snapshot &v)
{
  if (!std::isfinite(v.clock.offset)) {
    return "no run yet";
  }
  return v.clock.paced ? "" : "the run is not paced";
}

} // namespace

Component MonitorTab(Model &m, Controllers ecus, std::shared_ptr<MonitorLayout> lay)
{
  struct State {
    std::vector<std::string> rows;
    int selected = 0;
    std::set<std::string> checked; // plotted; none: the selected one
    bool single = false;           // checked ones in one graph instead of one graph each
    PlotState plot;                // zoom, freeze, cursors
    Variable sel;                 // the selected row, as last rendered
    std::string hint;              // one-line feedback under the header
    TimeWindow win{NAN, NAN};      // what the last frame showed, for e

    int ctrl_from = 0;   // the first controller row of the list
    bool merged = false; // the controller rows are plotted on the run's t
    int trim_ms = 0;     // added to their t; 0 again for each new fit
    double fit = NAN;    // the run's clock offset the trim is for

    std::string head;               // the column titles of the variables pane
    std::string filter;             // narrows the list to the names that contain it
    int row = 1;                    // focus in the pane: starts on the list, so the keys work
    std::vector<std::size_t> shown; // the list rows: indexes into the variables, then past them

    bool show_write = false, show_signal = false;
    Variable target; // what the open dialog writes to
    std::string value;
    std::vector<std::string> kinds;
    int kind = 0;
    std::string fields[4];
    std::string error;
    int write_row = 0, signal_row = 0;          // focus in the dialogs: reset when one opens
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
  auto open_signal = [s, fill_defaults] {
    s->target = s->sel;
    fill_defaults();
    s->signal_row = 0;
    s->show_signal = true;
  };
  auto open_write = [s, current, open_signal] {
    if (s->sel.kind == "ctrl") {
      s->hint = "a controller signal: write it on the controller's page";
      return;
    }
    if (s->sel.name.empty() || s->sel.kind == "output") {
      s->hint = s->sel.name.empty() ? "nothing to write" : s->sel.name + " is an output: read-only";
      return;
    }
    if (s->sel.kind == "input") { // an input takes a signal; "constant" is a plain value
      open_signal();
      return;
    }
    s->target = s->sel;
    s->value = current();
    s->value_cursor = static_cast<int>(s->value.size());
    s->error.clear();
    s->write_row = 0;
    s->show_write = true;
  };
  auto apply_write = [&m, s] {
    double v;
    const std::string why = CheckValue(s->value, s->target.min, s->target.max, &v);
    if (!why.empty()) {
      s->error = why;
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

  // What is plotted: the checked rows (the runner's, then the controllers'), or the selected one
  // when none is. A controller row only while merged, shifted to the run's t and from its t = 0.
  // note: what the title says of the controller rows. ponytail: after the run stops a controller
  // keeps sampling, so a live window moves on past the run's end (f freezes it).
  auto sources = [s](const Snapshot &v, const std::vector<CtrlRow> &ctrl, std::string &note) {
    std::vector<Source> out;
    bool checked = false, asked = false; // asked: a controller row is wanted
    const auto runner = [&](const std::string &name) {
      const auto it = v.history.find(name);
      out.push_back({name, it != v.history.end() ? &it->second : nullptr});
    };
    const auto controller = [&](const CtrlRow &c) {
      asked = true;
      const auto it = c.e->history.find(c.signal);
      if (s->merged) {
        out.push_back({c.name, it != c.e->history.end() ? &it->second : nullptr,
                       c.e->origin + v.clock.offset + s->trim_ms * 1e-3, 0});
      }
    };
    for (const auto &x : v.vars) {
      if (s->checked.count(x.name)) {
        checked = true;
        runner(x.name); // plotted even when the filter hides it
      }
    }
    for (const auto &c : ctrl) {
      if (s->checked.count(c.name)) {
        checked = true;
        controller(c);
      }
    }
    for (const auto &c : ctrl) {
      if (!checked && c.name == s->sel.name) {
        controller(c);
      }
    }
    if (!checked && s->sel.kind != "ctrl" && !s->sel.name.empty()) {
      runner(s->sel.name);
    }
    note.clear();
    if (asked && !s->merged) {
      note = "  ctrl: " + Unmerged(v);
    } else if (asked) {
      note = Format("  ctrl ±%.1f ms", v.clock.err * 1e3) +
             (s->trim_ms != 0 ? Format(" trim %+d ms", s->trim_ms) : "");
    }
    return out;
  };
  // e: the samples of what is plotted in the window shown, at full resolution, shifted.
  auto export_plotted = [&m, s, ecus, sources] {
    std::vector<std::string> names;
    std::vector<std::vector<Sample>> series;
    m.view([&](const Snapshot &v) {
      std::vector<CtrlRow> ctrl;
      ViewEcus(ecus, 0, ctrl, [&] {
        std::string note;
        for (const auto &src : sources(v, ctrl, note)) {
          names.push_back(src.name);
          series.emplace_back();
          for (std::size_t i = 0; src.h && i < src.h->size(); i++) {
            const double t = (*src.h)[i].t + src.shift;
            if (t >= src.from && t >= s->win.t0 && t <= s->win.t1) {
              series.back().push_back({t, (*src.h)[i].v});
            }
          }
        }
      });
      return 0;
    });
    m.export_csv(std::move(names), std::move(series));
  };

  // The run view
  auto start = Button("Start", [&m] { m.start(); }, ButtonOption::Ascii());
  auto stop = Button("Stop", [&m] { m.stop(); }, ButtonOption::Ascii());
  auto reset = Button("Reset", [&m] { m.reset(); }, ButtonOption::Ascii());
  auto unload = Button("Unload", [&m] { m.unload(); }, ButtonOption::Ascii());
  MenuOption menu = MenuOption::Vertical();
  menu.entries = &s->rows;
  menu.selected = &s->selected;
  menu.on_enter = open_write;
  menu.entries_option.transform = [s](const EntryState &e) {
    Element row = text((e.active ? "> " : "  ") + e.label); // as FTXUI draws it
    if (e.focused) {
      row = row | inverted;
    }
    if (e.active) {
      row = row | bold;
    }
    return !s->merged && e.index >= s->ctrl_from ? row | dim : row;
  };
  auto vars = Menu(menu);
  auto filter = FilterInput(&s->filter);
  auto vars_pane = Renderer(Container::Vertical({filter, vars}, &s->row), [s, filter, vars] {
    return window(text(" Variables (space: plot) "),
                  vbox({FilterRow(filter), text(s->head) | dim,
                        vars->Render() | vscroll_indicator | frame | flex}));
  });
  auto split = std::make_shared<PaneSplit>(lay, vars_pane);
  auto layout = Container::Vertical(
      {split->component(), Container::Horizontal({start, stop, reset, unload})}, split->focus());

  auto view = Renderer(layout, [=, &m] {
    split->frame();
    const int nw = split->name_width(24); // the other columns of a row
    Element header, events;
    PlotData data;
    m.view([&](const Snapshot &v) {
      std::vector<CtrlRow> ctrl;
      ViewEcus(ecus, 0, ctrl, [&] {
        // ponytail: a new fit moves the offset by seconds; a run started again within 0.5 s of
        // the last start keeps the trim
        if (!(std::fabs(v.clock.offset - s->fit) < 0.5)) {
          s->trim_ms = 0;
        }
        s->fit = v.clock.offset;
        s->merged = Unmerged(v).empty();
        s->rows.clear();
        s->shown.clear();
        const auto row = [&](const std::string &name, const std::string &kind, double value) {
          s->rows.push_back(Format("%s %-*s %-6s %12s", s->checked.count(name) ? "[x]" : "[ ]", nw,
                                   Fit(name, nw).c_str(), kind.c_str(), Number(value).c_str()));
        };
        for (std::size_t i = 0; i < v.vars.size(); i++) {
          if (MatchesFilter(v.vars[i].name, s->filter)) {
            s->shown.push_back(i);
            row(v.vars[i].name, v.vars[i].kind, v.vars[i].value);
          }
        }
        s->ctrl_from = static_cast<int>(s->rows.size());
        for (std::size_t k = 0; k < ctrl.size(); k++) { // indexes past the variables
          if (MatchesFilter(ctrl[k].name, s->filter)) {
            s->shown.push_back(v.vars.size() + k);
            row(ctrl[k].name, "ctrl", ctrl[k].value);
          }
        }
        s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->rows.size()) - 1));
        const std::size_t at =
            s->shown.empty() ? 0 : s->shown[static_cast<std::size_t>(s->selected)];
        s->sel = s->shown.empty()     ? Variable{}
                 : at < v.vars.size() ? v.vars[at]
                                      : Variable{ctrl[at - v.vars.size()].name, "ctrl",
                                                 ctrl[at - v.vars.size()].value};
        std::string note;
        data = Gather(sources(v, ctrl, note), s->plot);
        data.note = note;
        s->win = data.win;
      });
      header = Header(v);
      events = EventsPane(v.events);
      return 0;
    });

    s->head = Format("      %-*s %-6s %12s", nw, "name", "kind", "value");
    return vbox({
        hbox({header | flex, start->Render(), text(" "), stop->Render(), text(" "), reset->Render(),
              text(" "), unload->Render(), text(" ")}),
        s->hint.empty() ? text("") : text(" " + s->hint) | color(Color::Yellow),
        split->render(PlotPane(data, s->plot, s->single)),
        events | size(HEIGHT, EQUAL, kEventsRows),
    });
  });

  // Write dialog: one value for an input or a tunable parameter
  const InputOption one_line = OneLine();
  auto value_opt = one_line;
  value_opt.on_enter = apply_write;
  value_opt.cursor_position = &s->value_cursor;
  auto value = Input(&s->value, "number", value_opt);
  auto write_buttons = Container::Horizontal({Button("Write", apply_write, ButtonOption::Ascii()),
                                              Button("Cancel", close, ButtonOption::Ascii())});
  auto write_dialog = Renderer(Container::Vertical({value, write_buttons}, &s->write_row), [=] {
    return window(text(" Write " + s->target.name + " (" + s->target.kind + ") "),
                  vbox({hbox({text(s->target.name + " = "),
                              value->Render() | underlined |
                                  size(WIDTH, GREATER_THAN, kNumberBoxWidth)}),
                        std::isfinite(s->target.min) && std::isfinite(s->target.max)
                            ? text(Format("limits %g..%g", s->target.min, s->target.max)) | dim
                            : text(""),
                        text(s->error) | color(Color::Red),
                        text(s->target.kind == "input" ? "replaces its signal from the next step"
                                                       : "from the next step") |
                            dim,
                        write_buttons->Render() | align_right})) |
           size(WIDTH, GREATER_THAN, kDialogWidth);
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
      rows.push_back(hbox(
          {text(Format("%-11s", p.fields[i])),
           fields[i + 1]->Render() | underlined | size(WIDTH, GREATER_THAN, kNumberBoxWidth)}));
    }
    rows.push_back(text(s->error) | color(Color::Red));
    rows.push_back(text("starts at the next step, replaces its signal") | dim);
    rows.push_back(signal_buttons->Render() | align_right);
    return window(text(" Signal for " + s->target.name + " "), vbox(std::move(rows))) |
           size(WIDTH, GREATER_THAN, kDialogWidth);
  });

  auto with_dialogs =
      Modal(Modal(view, write_dialog, &s->show_write), signal_dialog, &s->show_signal);
  return CatchEvent(with_dialogs, [=, &m](const Event &e) {
    if (s->show_write || s->show_signal) { // the dialog gets the keys
      return EscapeCloses(e, close);
    }
    if (e.is_mouse()) { // over the plot: zoom and the cursors; elsewhere the page's own
      return PlotKeys(e, s->plot);
    }
    if (filter->Focused()) {
      return false; // typing
    }
    if (e.is_character()) {
      s->hint.clear();
    }
    if (!split->shown() && (e == Event::Character('w') || e == Event::Character(' '))) {
      s->hint = "the variables are hidden: v shows them";
      return true;
    }
    if (split->on_key(e) || PlotKeys(e, s->plot)) {
      return true;
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
    } else if (e == Event::Character(' ') && !s->sel.name.empty()) {
      if (!s->checked.erase(s->sel.name)) {
        s->checked.insert(s->sel.name);
      }
    } else if (e == Event::Character('c')) {
      s->checked.clear();
    } else if (e == Event::Character('m')) {
      s->single = !s->single;
    } else if (e == Event::Character('e')) {
      export_plotted();
    } else if (e == Event::Character('{') || e == Event::Character('}')) {
      s->trim_ms += e == Event::Character('}') ? 1 : -1; // the controller rows, by 1 ms
    } else {
      return false;
    }
    return true;
  });
}

} // namespace krumpir::ui
