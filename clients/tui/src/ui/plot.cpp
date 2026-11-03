#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include <ftxui/dom/canvas.hpp>

#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

namespace {

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
  return vbox({hbox({title, filler(), text("max " + Number(top)) | dim}), plot | flex,
               hbox({filler(), text("min " + Number(bottom)) | dim})});
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

} // namespace

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

Element PlotPane(const std::vector<std::string> &names,
                 const std::vector<std::vector<Sample>> &samples, bool single)
{
  // Stacked, one per variable, over the time all of them cover
  double t0 = INFINITY, t1 = -INFINITY;
  for (const auto &h : samples) {
    if (!h.empty()) {
      t0 = std::min(t0, h.front().t);
      t1 = std::max(t1, h.back().t);
    }
  }
  Elements plots;
  const bool one = single && names.size() > 1;
  if (one) {
    plots.push_back(Overlay(names, samples, t0, t1) | flex);
  }
  for (std::size_t i = 0; i < names.size() && !one; i++) {
    if (i > 0) {
      plots.push_back(separatorLight());
    }
    plots.push_back(Plot(names[i], samples[i], t0, t1, kColors[i % std::size(kColors)]) | flex);
  }
  if (names.empty()) {
    plots.push_back(text("nothing to plot") | dim | center | flex);
  } else if (t1 > t0) {
    plots.push_back(hbox({filler(), text(Format("t %.3f .. %.3f s", t0, t1)) | dim}));
  }
  return window(text(single ? " Plot: one graph, each scaled to its range (m: one each) "
                            : " Plot: one graph each (m: all in one) "),
                vbox(std::move(plots)));
}

Element EventsPane(const std::deque<std::string> &events)
{
  Elements lines;
  for (const auto &e : events) {
    const bool bad = e.find("error") != std::string::npos ||
                     e.find("fault") != std::string::npos ||
                     e.find("tripped") != std::string::npos ||
                     e.find("disconnected") != std::string::npos;
    lines.push_back(text(e) | (bad ? color(Color::Red) : nothing));
  }
  return window(text(" Events "), vbox(std::move(lines)) | focusPositionRelative(0, 1) | yframe);
}

} // namespace krumpir::ui
