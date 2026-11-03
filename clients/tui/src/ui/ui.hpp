#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "ecu_model.hpp"
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

// F2: run state and controls, live variables, plot of the selected one, events.
ftxui::Component MonitorTab(Model &m);

// A controller's card on Load: address, connect, calibration grid.
ftxui::Component ControllerSetup(EcuModel &ecu);
// A controller's card on Monitor: state, trip, start/stop/clear, signals and their plot.
ftxui::Component ControllerMonitor(EcuModel &ecu);

// Shared look
ftxui::Color StateColor(const std::string &state);
std::string Format(const char *fmt, ...);

// Shared pieces (plot.cpp)
std::vector<Sample> Thin(const std::deque<Sample> &h); // at most ~4000 points, to draw
bool ParseNumber(const std::string &s, double *out);   // the whole text as a finite number
// One graph per name stacked, or all in one each scaled to its range (single).
ftxui::Element PlotPane(const std::vector<std::string> &names,
                        const std::vector<std::vector<Sample>> &samples, bool single);
ftxui::Element EventsPane(const std::deque<std::string> &events);

} // namespace krumpir::ui
