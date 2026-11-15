#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "ecu_model.hpp"
#include "layout.hpp"
#include "model.hpp"

// The screens. Each tab is a component that reads the model's snapshot when it renders and
// posts commands to it; none of them talks to the network.
namespace krumpir::ui
{

// The controllers of the repo, by name, one model each.
using Controllers = std::vector<std::pair<std::string, EcuModel *>>;

// The frame: status bar, F-key tabs, the active tab, key bar. Adding a tab = one line in root.cpp.
// Load and Monitor have a card per target on the left (the runner, then each controller); the
// card picked is the same in both.
ftxui::Component Root(Model &m, Controllers ecus, std::string address, std::function<void()> quit);

// F1: connect, pick a scenario or FMU on the runner, edit it as a form, check, save, load it.
ftxui::Component LoadTab(Model &m, std::string address, std::function<void()> on_loaded);

// F2: run state and controls, live variables, plot of the selected one, events. The variables pane
// can be hidden (v) and resized (drag the divider, [ and ]); layout is shared by every Monitor
// page. The connected controllers' watched signals are rows too, plotted on the run's t while it
// is paced.
ftxui::Component MonitorTab(Model &m, Controllers ecus, std::shared_ptr<MonitorLayout> layout);

// A controller's card on Load: address, connect, calibration grid.
ftxui::Component ControllerSetup(EcuModel &ecu);
// A controller's card on Monitor: state, trip, start/stop/clear, signals and their plot.
ftxui::Component ControllerMonitor(EcuModel &ecu, std::shared_ptr<MonitorLayout> layout);

// Shared look
ftxui::Color StateColor(const std::string &state);
std::string Format(const char *fmt, ...);
// A value as the lists and the plot show it: up to 6 significant digits; "nan", "inf" or "-inf"
// when it is not finite, never blank.
std::string Number(double v);
// s cut to width characters, ending in "~" when it was longer: the same cut everywhere a name
// has to fit a column.
std::string Fit(const std::string &s, int width);

// Shared ways of building the screens
ftxui::InputOption OneLine(); // a text box that stays on one line
// For the key handler of a screen with dialogs: while one is open the dialog gets the keys, and
// Esc closes it. Returns true when the key was Esc.
bool EscapeCloses(const ftxui::Event &e, const std::function<void()> &close);

// Shared pieces (plot.cpp)
// The samples of h in [t0, t1] and one either side, so lines reach the edges; at most ~4000, to
// draw. ponytail: a stride, so a one-step spike can drop out of a wide window; min/max per column
// if that is ever missed (zooming in shows it).
std::vector<Sample> Thin(const std::deque<Sample> &h, double t0, double t1);
const Sample *Nearest(const std::deque<Sample> &h, double t); // nullptr when h is empty or t NaN
bool ParseNumber(const std::string &s, double *out);   // the whole text as a finite number
// The text typed in a value box: a number and, when min and max are both finite, inside them.
// Returns "" with the number in *out, or why it is refused. Every place that writes a value to a
// target uses this, so the checks are the same.
std::string CheckValue(const std::string &text, double min, double max, double *out);
// The plot of a Monitor page: its view in time (layout.hpp) and where it was drawn (the mouse,
// the cursor's column step).
struct PlotState {
  PlotView view;
  ftxui::Box box;
};
// What one frame plots: the samples in the window, and what the cursors read. A cursor reads the
// nearest sample of the full history, with that sample's own t, never an interpolation.
struct PlotData {
  std::vector<std::string> names;
  std::vector<std::vector<Sample>> samples; // thinned to the window
  std::vector<double> at_a, at_b;           // per name: the value at the cursor, NaN: none
  double ta = NAN, tb = NAN;                // the t of those samples (the first name that has one)
  TimeWindow win{NAN, NAN};                 // NaN: no samples yet
  std::string note;                         // in the title, after the mode ("" none)
};
// One series to plot: a history whose t is shifted by shift s; samples before from (shifted) are
// not shown. The window, the cursors and what is returned are in shifted time.
struct Source {
  std::string name;
  const std::deque<Sample> *h; // nullptr: no samples yet
  double shift = 0;
  double from = -INFINITY;
};
// Call inside the views of the models whose histories the sources point into.
PlotData Gather(const std::vector<Source> &sources, PlotState &p);
// The names of history, unshifted.
PlotData Gather(const std::map<std::string, std::deque<Sample>> &history,
                const std::vector<std::string> &names, PlotState &p);
// One graph per name stacked, or all in one each scaled to its range (single).
ftxui::Element PlotPane(const PlotData &d, PlotState &p, bool single);
// Zoom, pan, freeze and the cursors, by key or mouse over the plot. True when it was one of them.
bool PlotKeys(const ftxui::Event &e, PlotState &p);
ftxui::Element EventsPane(const std::deque<std::string> &events);
constexpr int kEventsRows = 8;       // the height of the events pane, the same on every page
constexpr int kAddressWidth = 24;    // the address box of a Load tab, the same for every target
constexpr int kFilesWidth = 36;      // the file list of the runner's Load tab
constexpr int kLabelWidth = 32;      // the widest label column of the Load form
constexpr int kFormFieldWidth = 24;  // a widget of the Load form
constexpr int kCalFieldWidth = 14;   // a value box of the calibration grid
constexpr int kNumberBoxWidth = 14;  // at least, for a number typed in a dialog
constexpr int kDialogWidth = 50;     // at least, for a dialog
constexpr int kWideDialogWidth = 56; // at least, for a dialog with a path to type

} // namespace krumpir::ui
