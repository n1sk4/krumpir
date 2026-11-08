#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include <ftxui/component/event.hpp>

#include "split.hpp"
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
             text(Format("  wake max %.1f us", v.max_wake_ns / 1e3)),
             text(v.late > 0 ? Format("  late %.0f", v.late) : "") | color(Color::Yellow)};
  if (v.state == "FAULT") {
    e.push_back(text("  " + v.fault) | color(Color::Red));
  }
  return hbox(std::move(e));
}

} // namespace

Component MonitorTab(Model &m, std::shared_ptr<MonitorLayout> lay)
{
  struct State {
    std::vector<std::string> rows;
    int selected = 0;
    std::set<std::string> checked; // plotted; none: the selected one
    bool single = false;           // checked ones in one graph instead of one graph each
    Variable sel;     // the selected row, as last rendered
    std::string hint; // one-line feedback under the header

    std::string head;   // the column titles of the variables pane
    std::string filter; // narrows the list to the names that contain it
    int row = 1;        // focus in the pane: starts on the list, so the keys work
    std::vector<std::size_t> shown; // the list rows, as indexes into the snapshot's variables

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
  auto open_signal = [s, fill_defaults] {
    s->target = s->sel;
    fill_defaults();
    s->signal_row = 0;
    s->show_signal = true;
  };
  auto open_write = [s, current, open_signal] {
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
    std::vector<std::string> plotted;
    std::vector<std::vector<Sample>> samples;
    m.view([&](const Snapshot &v) {
      s->rows.clear();
      s->shown.clear();
      for (std::size_t i = 0; i < v.vars.size(); i++) {
        const auto &x = v.vars[i];
        if (s->checked.count(x.name)) {
          plotted.push_back(x.name); // plotted even when the filter hides it
        }
        if (!MatchesFilter(x.name, s->filter)) {
          continue;
        }
        s->shown.push_back(i);
        s->rows.push_back(Format("%s %-*s %-6s %12s", s->checked.count(x.name) ? "[x]" : "[ ]", nw,
                                 Fit(x.name, nw).c_str(), x.kind.c_str(),
                                 Number(x.value).c_str()));
      }
      s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->rows.size()) - 1));
      s->sel = s->shown.empty() ? Variable{} : v.vars[s->shown[static_cast<std::size_t>(s->selected)]];
      if (plotted.empty() && !s->sel.name.empty()) {
        plotted.push_back(s->sel.name);
      }
      for (const auto &name : plotted) {
        const auto it = v.history.find(name);
        samples.push_back(it != v.history.end() ? Thin(it->second) : std::vector<Sample>{});
      }
      header = Header(v);
      events = EventsPane(v.events);
      return 0;
    });

    s->head = Format("      %-*s %-6s %12s", nw, "name", "kind", "value");
    return vbox({
        hbox({header | flex, start->Render(), text(" "), stop->Render(), text(" "),
              reset->Render(), text(" "), unload->Render(), text(" ")}),
        s->hint.empty() ? text("") : text(" " + s->hint) | color(Color::Yellow),
        split->render(PlotPane(plotted, samples, s->single)),
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
      rows.push_back(hbox({text(Format("%-11s", p.fields[i])),
                           fields[i + 1]->Render() | underlined |
                               size(WIDTH, GREATER_THAN, kNumberBoxWidth)}));
    }
    rows.push_back(text(s->error) | color(Color::Red));
    rows.push_back(text("starts at the next step, replaces its signal") | dim);
    rows.push_back(signal_buttons->Render() | align_right);
    return window(text(" Signal for " + s->target.name + " "), vbox(std::move(rows))) |
           size(WIDTH, GREATER_THAN, kDialogWidth);
  });

  auto with_dialogs =
      Modal(Modal(view, write_dialog, &s->show_write), signal_dialog, &s->show_signal);
  return CatchEvent(with_dialogs, [&m, s, split, filter, open_write, close](const Event &e) {
    if (s->show_write || s->show_signal) { // the dialog gets the keys
      return EscapeCloses(e, close);
    }
    if (filter->Focused()) {
      return false; // typing
    }
    if (e.is_character()) {
      s->hint.clear();
    }
    if (!split->shown() && (e == Event::Character('w') ||
                            e == Event::Character(' '))) {
      s->hint = "the variables are hidden: v shows them";
      return true;
    }
    if (split->on_key(e)) {
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
    } else {
      return false;
    }
    return true;
  });
}

} // namespace krumpir::ui
