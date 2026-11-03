#pragma once

#include <deque>
#include <functional>
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
namespace krumpir::ui {

// The controllers of the repo, by name, one model each.
using Controllers = std::vector<std::pair<std::string, EcuModel *>>;

// The frame: status bar, F-key tabs, the active tab, key bar. Adding a tab = one line in root.cpp.
// Load and Monitor have a card per target on the left (the runner, then each controller); the
// card picked is the same in both.
ftxui::Component Root(Model &m, Controllers ecus, std::string address, std::function<void()> quit);

// F1: connect, pick a scenario or FMU on the runner, edit it as a form, check, save, load it.
ftxui::Component LoadTab(Model &m, std::string address, std::function<void()> on_loaded);

// F2: run state and controls, live variables, plot of the selected one, events. The variables pane
// can be hidden (v) and resized (drag the divider, [ and ]); layout is shared by every Monitor page.
ftxui::Component MonitorTab(Model &m, std::shared_ptr<MonitorLayout> layout);

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
std::vector<Sample> Thin(const std::deque<Sample> &h); // at most ~4000 points, to draw
bool ParseNumber(const std::string &s, double *out);   // the whole text as a finite number
// One graph per name stacked, or all in one each scaled to its range (single).
ftxui::Element PlotPane(const std::vector<std::string> &names,
                        const std::vector<std::vector<Sample>> &samples, bool single);
ftxui::Element EventsPane(const std::deque<std::string> &events);
constexpr int kEventsRows = 8;    // the height of the events pane, the same on every page
constexpr int kAddressWidth = 24; // the address box of a Load tab, the same for every target
constexpr int kFilesWidth = 36;   // the file list of the runner's Load tab
constexpr int kLabelWidth = 32;   // the widest label column of the Load form
constexpr int kFormFieldWidth = 24; // a widget of the Load form
constexpr int kCalFieldWidth = 14;  // a value box of the calibration grid
constexpr int kNumberBoxWidth = 14; // at least, for a number typed in a dialog
constexpr int kDialogWidth = 50;    // at least, for a dialog
constexpr int kWideDialogWidth = 56; // at least, for a dialog with a path to type

} // namespace krumpir::ui
