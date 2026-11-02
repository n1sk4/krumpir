#pragma once

#include <functional>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "model.hpp"

// The screens. Each tab is a component that reads the model's snapshot when it renders and
// posts commands to it; none of them talks to the network.
namespace krumpir::ui {

// The frame: status bar, F-key tabs, the active tab, key bar. Adding a tab = one line in root.cpp.
ftxui::Component Root(Model &m, std::string address, std::function<void()> quit);

// F1: connect, pick a scenario or FMU on the runner, preview it, load it.
ftxui::Component LoadTab(Model &m, std::string address, std::function<void()> on_loaded);

// F2: run state and controls, live variables, plot of the selected one, events.
ftxui::Component MonitorTab(Model &m);

// Shared look
ftxui::Color StateColor(const std::string &state);
std::string Format(const char *fmt, ...);

} // namespace krumpir::ui
