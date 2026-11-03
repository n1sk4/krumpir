#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <ftxui/component/event.hpp>

#include "split.hpp"
#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

namespace {

// The text of an edit box: blank while there is no value to start from.
std::string EditText(double v) { return std::isfinite(v) ? Format("%.6g", v) : std::string(); }

std::string Name(const EcuSnapshot &v)
{
  for (const auto &c : v.controllers) {
    if (c.dir == v.selected) {
      return c.name;
    }
  }
  return "controller";
}

Element Link(const EcuSnapshot &v)
{
  return hbox({v.connected ? text("  * connected") | color(Color::Green)
                           : text("  o disconnected") | color(Color::Red),
               text(v.epk.empty() ? "" : "  EPK " + v.epk) | dim,
               text(v.connected ? Format("  rtt %.1f ms", v.rtt_ms) : "") | dim,
               text(v.busy ? "  ..." : "") | dim});
}

} // namespace

Component ControllerSetup(EcuModel &ecu)
{
  struct State {
    std::string address, opened; // opened: dir of the controller the grid was built for
    std::vector<CalParam> params;
    std::vector<std::string> edit, base; // typed text, and the text it started from
    std::vector<int> cursor;
    std::vector<int> input_of; // param -> index in the grid's inputs, -1: console only
    std::string hint;
  };
  auto s = std::make_shared<State>();

  auto connect = [&ecu, s] { ecu.connect(s->address); };
  auto write = [&ecu, s] {
    s->hint.clear();
    int sent = 0;
    for (std::size_t i = 0; i < s->params.size(); i++) {
      const CalParam &p = s->params[i];
      if (s->input_of[i] < 0 || s->edit[i] == s->base[i]) {
        continue;
      }
      double v;
      const std::string why = CheckValue(s->edit[i], p.min, p.max, &v);
      if (!why.empty()) {
        s->hint = p.name + ": " + why + ", not written";
        continue;
      }
      ecu.write(p.name, v);
      s->base[i] = s->edit[i];
      sent++;
    }
    if (sent == 0 && s->hint.empty()) {
      s->hint = "nothing changed";
    }
  };
  auto revert = [s] {
    for (std::size_t i = 0; i < s->params.size(); i++) {
      s->edit[i] = s->base[i] = EditText(s->params[i].value);
      s->cursor[i] = static_cast<int>(s->edit[i].size());
    }
  };

  InputOption addr_opt = OneLine();
  addr_opt.on_enter = connect;
  auto addr = Input(&s->address, "host:port", addr_opt);
  auto connect_btn = Button("Connect", connect, ButtonOption::Ascii());
  auto disconnect_btn = Button("Disconnect", [&ecu] { ecu.disconnect(); }, ButtonOption::Ascii());
  auto grid = Container::Vertical({}); // filled when the controller's description arrives
  auto write_btn = Button("Write", write, ButtonOption::Ascii());
  auto revert_btn = Button("Revert", revert, ButtonOption::Ascii());
  auto layout = Container::Vertical({Container::Horizontal({addr, connect_btn, disconnect_btn}),
                                     grid, Container::Horizontal({write_btn, revert_btn})});

  // The grid's inputs for the controller's parameters.
  auto rebuild = [s, grid, write](std::vector<CalParam> params) {
    s->params = std::move(params);
    const std::size_t n = s->params.size();
    s->edit.assign(n, "");
    s->base.assign(n, "");
    s->cursor.assign(n, 0);
    s->input_of.assign(n, -1);
    grid->DetachAllChildren();
    int k = 0;
    for (std::size_t i = 0; i < n; i++) {
      if (s->params[i].cal.empty()) {
        continue;
      }
      InputOption opt = OneLine();
      opt.on_enter = write;
      opt.cursor_position = &s->cursor[i];
      grid->Add(Input(&s->edit[i], "", opt));
      s->input_of[i] = k++;
    }
  };

  return Renderer(layout, [=, &ecu] {
    Element link, events, a2l_error;
    std::string name;
    ecu.view([&](const EcuSnapshot &v) {
      if (v.selected != s->opened) {
        s->opened = v.selected;
        s->address = v.address;
        rebuild(v.params);
      }
      for (std::size_t i = 0; i < s->params.size() && i < v.params.size(); i++) {
        s->params[i].value = v.params[i].value;
        if (s->base[i].empty() && std::isfinite(v.params[i].value)) { // first read: fill in
          s->edit[i] = s->base[i] = EditText(v.params[i].value);
          s->cursor[i] = static_cast<int>(s->edit[i].size());
        }
      }
      name = Name(v);
      link = Link(v);
      a2l_error = v.a2l_error.empty() ? text("") : paragraph(v.a2l_error) | color(Color::Red);
      events = EventsPane(v.events);
      return 0;
    });

    // name | value | unit | limits | read back | help
    std::vector<Elements> rows{{text("name ") | dim, text("value") | dim, text(" unit ") | dim,
                                text("limits ") | dim, text("ecu") | dim, text("") | dim}};
    for (std::size_t i = 0; i < s->params.size(); i++) {
      const CalParam &p = s->params[i];
      Element field = text("console only") | dim;
      if (s->input_of[i] >= 0) {
        double v;
        const bool ok =
            s->edit[i] == s->base[i] || CheckValue(s->edit[i], p.min, p.max, &v).empty();
        field = grid->ChildAt(static_cast<std::size_t>(s->input_of[i]))->Render() | underlined |
                (!ok                       ? color(Color::Red)
                 : s->edit[i] != s->base[i] ? color(Color::Yellow)
                                            : nothing);
      }
      rows.push_back({text(p.name + " ") | bold, field | size(WIDTH, EQUAL, kCalFieldWidth),
                      text(" " + (p.unit.empty() ? "-" : p.unit) + " "),
                      text(Format("%g..%g ", p.min, p.max)) | dim,
                      text(Format("%-10s", Number(p.value).c_str())), text(" " + p.help) | dim});
    }
    return vbox({
        hbox({text(" " + name + " ") | bold, addr->Render() | size(WIDTH, EQUAL, kAddressWidth) | underlined,
              text(" "), connect_btn->Render(), text(" "), disconnect_btn->Render(), link}),
        s->hint.empty() ? text("") : text(" " + s->hint) | color(Color::Yellow),
        window(text(" Calibration (Enter or Write sends the changed ones) "),
               vbox({gridbox(std::move(rows)), a2l_error,
                     hbox({filler(), write_btn->Render(), text(" "), revert_btn->Render()})})),
        filler(),
        events | size(HEIGHT, EQUAL, kEventsRows),
    });
  });
}

Component ControllerMonitor(EcuModel &ecu, std::shared_ptr<MonitorLayout> lay)
{
  struct State {
    std::string filter;
    std::vector<std::string> rows, names, watched; // watched: the ones checked
    std::string fallback; // watched only because none is checked: the selected one is plotted
    std::string sent;     // the fallback last asked for, so a frame does not ask twice
    int selected = 0;
    int row = 1; // focus in the signals pane: starts on the list, so the keys work
    bool single = false;
    std::string hint; // one-line feedback under the header
    bool connected = false;

    // The write dialog: pick a calibration parameter, type a value, the limits are checked
    bool show_write = false;
    std::vector<CalParam> writable;       // the parameters that have a calibration value
    std::vector<std::string> param_rows;  // their rows in the dialog
    int param = 0;                        // the row picked
    std::string value, error;
    int value_cursor = 0, write_row = 0;
  };
  auto s = std::make_shared<State>();

  auto fill = [s] { // the picked parameter's current value as the starting text
    if (s->param >= 0 && s->param < static_cast<int>(s->writable.size())) {
      s->value = EditText(s->writable[static_cast<std::size_t>(s->param)].value);
      s->value_cursor = static_cast<int>(s->value.size());
    }
    s->error.clear();
  };
  auto open_write = [s, fill] {
    if (!s->connected) {
      s->hint = "not connected: nothing to write";
      return;
    }
    if (s->writable.empty()) {
      s->hint = "this controller has no calibration parameters";
      return;
    }
    s->param = std::clamp(s->param, 0, static_cast<int>(s->writable.size()) - 1);
    fill();
    s->write_row = 0;
    s->show_write = true;
  };
  auto apply_write = [&ecu, s] {
    if (s->param < 0 || s->param >= static_cast<int>(s->writable.size())) {
      return;
    }
    const CalParam &p = s->writable[static_cast<std::size_t>(s->param)];
    double v;
    const std::string why = CheckValue(s->value, p.min, p.max, &v);
    if (!why.empty()) {
      s->error = why;
      return;
    }
    ecu.write(p.name, v);
    s->show_write = false;
  };
  auto close = [s] { s->show_write = false; };

  auto toggle = [&ecu, s] {
    if (s->selected < 0 || s->selected >= static_cast<int>(s->names.size())) {
      return;
    }
    const std::string &name = s->names[static_cast<std::size_t>(s->selected)];
    auto &w = s->watched;
    const auto it = std::find(w.begin(), w.end(), name);
    if (it != w.end()) {
      w.erase(it);
    } else {
      w.push_back(name);
    }
    s->fallback.clear(); // what is left is the user's choice
    s->sent.clear();
    ecu.watch(w);
  };

  auto start = Button("Start", [&ecu] { ecu.request("start"); }, ButtonOption::Ascii());
  auto stop = Button("Stop", [&ecu] { ecu.request("stop"); }, ButtonOption::Ascii());
  auto clear = Button("Clear", [&ecu] { ecu.request("clear"); }, ButtonOption::Ascii());
  auto filter = FilterInput(&s->filter);
  MenuOption opt = MenuOption::Vertical();
  opt.entries = &s->rows;
  opt.selected = &s->selected;
  opt.on_enter = toggle;
  auto sigs = Menu(opt);
  auto pane = Container::Vertical({filter, sigs}, &s->row);
  auto sig_pane = Renderer(pane, [filter, sigs] {
    return window(text(" Signals (space: plot) "),
                  vbox({FilterRow(filter), sigs->Render() | vscroll_indicator | frame | flex}));
  });
  auto split = std::make_shared<PaneSplit>(lay, sig_pane);
  auto layout = Container::Vertical(
      {split->component(), Container::Horizontal({start, stop, clear})}, split->focus());

  auto view = Renderer(layout, [=, &ecu] {
    split->frame();
    const int nw = split->name_width(16); // the other columns of a row
    Element header, events;
    std::vector<std::string> plotted;
    std::vector<std::vector<Sample>> samples;
    bool want_fallback = false;
    ecu.view([&](const EcuSnapshot &v) {
      const bool only_fallback =
          !s->fallback.empty() && v.watched.size() == 1 && v.watched[0] == s->fallback;
      s->watched = only_fallback ? std::vector<std::string>{} : v.watched;
      s->connected = v.connected;
      want_fallback = v.connected && s->watched.empty();
      s->writable.clear();
      s->param_rows.clear();
      for (const auto &p : v.params) {
        if (!p.cal.empty()) {
          s->writable.push_back(p);
          s->param_rows.push_back(Format("%-16s %10s %s", Fit(p.name, 16).c_str(),
                                         Number(p.value).c_str(), p.unit.c_str()));
        }
      }
      s->rows.clear();
      s->names.clear();
      for (const auto &name : v.measurements) {
        if (!MatchesFilter(name, s->filter)) {
          continue;
        }
        const bool on = std::find(s->watched.begin(), s->watched.end(), name) != s->watched.end();
        const auto val = v.values.find(name);
        s->rows.push_back(Format("%s %-*s %11s", on ? "[x]" : "[ ]", nw, Fit(name, nw).c_str(),
                                 val != v.values.end() ? Number(val->second).c_str() : ""));
        s->names.push_back(name);
      }
      s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->rows.size()) - 1));
      for (const auto &name : v.watched) {
        const auto it = v.history.find(name);
        plotted.push_back(name);
        samples.push_back(it != v.history.end() ? Thin(it->second) : std::vector<Sample>{});
      }
      const std::string run = v.running ? "RUNNING" : "STOPPED";
      const Element state = !v.connected ? text(" not connected: connect on F1 ") | dim
                                         : text(" " + run + " ") | bold | color(StateColor(run));
      const Element trip = v.trip != 0 ? text("  trip: " + v.trip_name + " (c clears) ") | bold |
                                             color(Color::Red)
                                       : text(v.connected ? "  no trip" : "") | dim;
      const std::size_t polled = v.watched.size() - static_cast<std::size_t>(v.daq_signals);
      const Element daq =
          !v.connected || v.watched.empty() ? text("")
          : v.daq_signals > 0
              ? text(Format("  DAQ %d @ %.0f/s", v.daq_signals, v.daq_rate) +
                     (polled > 0 ? Format(", %zu polled", polled) : std::string()) +
                     Format(", lost %llu", static_cast<unsigned long long>(v.daq_lost))) |
                    color(v.daq_lost > 0 ? Color(Color::Yellow) : Color(Color::Default))
              : text(Format("  polled %zu", polled)) | dim;
      header = hbox({text(" " + Name(v) + " ") | bold, state, trip, Link(v), daq});
      events = EventsPane(v.events);
      return 0;
    });
    // None checked: plot the selected one, as the runner does (outside the view: watch locks).
    if (!want_fallback) {
      s->fallback.clear();
      s->sent.clear();
    } else if (!s->names.empty() && s->sent != s->names[static_cast<std::size_t>(s->selected)]) {
      s->fallback = s->sent = s->names[static_cast<std::size_t>(s->selected)];
      ecu.watch({s->fallback});
    }

    return vbox({
        hbox({header | flex, start->Render(), text(" "), stop->Render(), text(" "),
              clear->Render(), text(" ")}),
        s->hint.empty() ? text("") : text(" " + s->hint) | color(Color::Yellow),
        split->render(PlotPane(plotted, samples, s->single)),
        events | size(HEIGHT, EQUAL, kEventsRows),
    });
  });

  // Write dialog: the parameter, then its value
  MenuOption pick;
  pick.entries = &s->param_rows;
  pick.selected = &s->param;
  pick.on_change = fill;
  pick.on_enter = [s] { s->write_row = 1; }; // on to the value
  auto params = Menu(pick);
  InputOption value_opt = OneLine();
  value_opt.on_enter = apply_write;
  value_opt.cursor_position = &s->value_cursor;
  auto value = Input(&s->value, "number", value_opt);
  auto write_buttons = Container::Horizontal({Button("Write", apply_write, ButtonOption::Ascii()),
                                              Button("Cancel", close, ButtonOption::Ascii())});
  auto write_dialog = Renderer(Container::Vertical({params, value, write_buttons}, &s->write_row),
                               [=] {
    const bool picked = s->param >= 0 && s->param < static_cast<int>(s->writable.size());
    const CalParam none;
    const CalParam &p = picked ? s->writable[static_cast<std::size_t>(s->param)] : none;
    return window(text(" Write a parameter "),
                  vbox({params->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 8),
                        hbox({text("value "), value->Render() | underlined |
                                                  size(WIDTH, GREATER_THAN, kNumberBoxWidth)}),
                        text(picked ? Format("%s  limits %g..%g", p.unit.empty() ? "-" : p.unit.c_str(),
                                             p.min, p.max)
                                    : std::string()) |
                            dim,
                        text(s->error) | color(Color::Red),
                        write_buttons->Render() | align_right})) |
           size(WIDTH, GREATER_THAN, kDialogWidth);
  });

  return CatchEvent(Modal(view, write_dialog, &s->show_write),
                    [&ecu, s, split, filter, toggle, open_write, close](const Event &e) {
    if (s->show_write) { // the dialog gets the keys
      return EscapeCloses(e, close);
    }
    if (filter->Focused()) {
      return false; // typing
    }
    if (e.is_character()) {
      s->hint.clear();
    }
    if (!split->shown() && e == Event::Character(' ')) {
      s->hint = "the signals are hidden: v shows them";
      return true;
    }
    if (split->on_key(e)) {
      return true;
    }
    if (e == Event::Character('w')) {
      open_write();
    } else if (e == Event::Character('s')) {
      ecu.request("start");
    } else if (e == Event::Character('x')) {
      ecu.request("stop");
    } else if (e == Event::Character('c')) {
      ecu.request("clear");
    } else if (e == Event::Character(' ')) {
      toggle();
    } else if (e == Event::Character('m')) {
      s->single = !s->single;
    } else {
      return false;
    }
    return true;
  });
}

} // namespace krumpir::ui
