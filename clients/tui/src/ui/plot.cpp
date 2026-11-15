#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/canvas.hpp>

#include "ui.hpp"

namespace krumpir::ui
{

using namespace ftxui;

namespace
{

const Color kColors[] = {Color::Cyan,  Color::Yellow, Color::Magenta,
                         Color::Green, Color::Blue,   Color::Red};

// What the canvases draw of the cursors: their times, NaN off.
struct Cursors {
  double a, b;
};

// The cursors as vertical lines: A solid, B dotted. Outside [t0, t1] they are not drawn.
void DrawCursors(Canvas &c, const Cursors &k, double t0, double t1)
{
  const int w = c.width() - 1;
  for (int n = 0; n < 2; n++) {
    const double t = n == 0 ? k.a : k.b;
    if (!(t >= t0 && t <= t1)) {
      continue;
    }
    const int x = static_cast<int>((t - t0) / (t1 - t0) * w);
    for (int y = 0; y < c.height(); y += n == 0 ? 1 : 2) {
      c.DrawPoint(x, y, true, Color::White);
    }
  }
}

// What the cursors read on one series: "A v  B v  B-A dv", only the parts that are on.
std::string Readout(const Cursors &k, double va, double vb)
{
  std::string r;
  if (std::isfinite(k.a)) {
    r += "  A " + Number(va);
  }
  if (std::isfinite(k.b)) {
    r += "  B " + Number(vb);
  }
  if (std::isfinite(k.a) && std::isfinite(k.b)) {
    r += "  B-A " + Number(vb - va);
  }
  return r;
}

// One variable's samples over [t0, t1] (shared by the stacked plots), scaled to the box.
Element Plot(const std::string &name, const std::vector<Sample> &h, double t0, double t1,
             Color colour, const Cursors &k, const std::string &readout)
{
  double lo = INFINITY, hi = -INFINITY;
  for (const auto &p : h) {
    if (std::isfinite(p.v)) {
      lo = std::min(lo, p.v);
      hi = std::max(hi, p.v);
    }
  }
  auto title = hbox({text(name) | bold | color(colour), text(readout)});
  if (h.size() < 2 || !(lo <= hi) || !(t1 > t0)) {
    return vbox({title, text(h.empty() ? "samples come in while it runs" : "not enough samples") |
                            dim | center | flex});
  }
  const double top = hi, bottom = lo;
  if (hi - lo < 1e-12 * std::max(1.0, std::fabs(hi))) { // flat: centre the line
    lo -= 1;
    hi += 1;
  }
  auto plot = canvas([h, lo, hi, t0, t1, colour, k](Canvas &c) {
    const double w = c.width() - 1, ht = c.height() - 1;
    auto x = [&](double t) { return static_cast<int>((t - t0) / (t1 - t0) * w); };
    auto y = [&](double v) { return static_cast<int>((hi - v) / (hi - lo) * ht); };
    for (std::size_t i = 1; i < h.size(); i++) {
      if (std::isfinite(h[i - 1].v) && std::isfinite(h[i].v)) {
        c.DrawPointLine(x(h[i - 1].t), y(h[i - 1].v), x(h[i].t), y(h[i].v), colour);
      }
    }
    DrawCursors(c, k, t0, t1);
  });
  return vbox({hbox({title, filler(), text("max " + Number(top)) | dim}), plot | flex,
               hbox({filler(), text("min " + Number(bottom)) | dim})});
}

// Several variables in one graph, each scaled to its own min..max (their units differ), with a
// legend of the ranges and what the cursors read.
Element Overlay(const PlotData &d, const Cursors &k)
{
  const auto &hs = d.samples;
  const double t0 = d.win.t0, t1 = d.win.t1;
  std::vector<double> lo(hs.size(), INFINITY), hi(hs.size(), -INFINITY);
  Elements legend;
  for (std::size_t i = 0; i < hs.size(); i++) {
    for (const auto &p : hs[i]) {
      if (std::isfinite(p.v)) {
        lo[i] = std::min(lo[i], p.v);
        hi[i] = std::max(hi[i], p.v);
      }
    }
    const Color c = kColors[i % std::size(kColors)];
    legend.push_back(text("-- " + d.names[i]) | bold | color(c));
    legend.push_back(text(lo[i] <= hi[i] ? Format(" %.4g..%.4g", lo[i], hi[i]) : " -") | dim);
    legend.push_back(text(Readout(k, d.at_a[i], d.at_b[i]) + "   "));
  }
  if (!(t1 > t0)) {
    return vbox(
        {hbox(std::move(legend)), text("samples come in while it runs") | dim | center | flex});
  }
  auto plot = canvas([hs, lo, hi, t0, t1, k](Canvas &c) {
    const double w = c.width() - 1, ht = c.height() - 1;
    for (std::size_t i = 0; i < hs.size(); i++) {
      const auto &h = hs[i];
      if (!(lo[i] <= hi[i])) {
        continue;
      }
      const double span = hi[i] - lo[i] > 0 ? hi[i] - lo[i] : 1.0;
      const double base = hi[i] - lo[i] > 0 ? lo[i] : lo[i] - 0.5; // flat: centred
      auto x = [&](double t) { return static_cast<int>((t - t0) / (t1 - t0) * w); };
      auto y = [&](double v) { return static_cast<int>((1.0 - (v - base) / span) * ht); };
      const Color col = kColors[i % std::size(kColors)];
      for (std::size_t j = 1; j < h.size(); j++) {
        if (std::isfinite(h[j - 1].v) && std::isfinite(h[j].v)) {
          c.DrawPointLine(x(h[j - 1].t), y(h[j - 1].v), x(h[j].t), y(h[j].v), col);
        }
      }
    }
    DrawCursors(c, k, t0, t1);
  });
  return vbox({hbox(std::move(legend)), plot | flex});
}

bool Before(const Sample &p, double t) { return p.t < t; }

} // namespace

std::vector<Sample> Thin(const std::deque<Sample> &h, double t0, double t1)
{
  auto first = std::lower_bound(h.begin(), h.end(), t0, Before);
  auto last = std::upper_bound(h.begin(), h.end(), t1,
                               [](double t, const Sample &p) { return t < p.t; });
  if (first != h.begin()) {
    --first;
  }
  if (last != h.end()) {
    ++last;
  }
  std::vector<Sample> out;
  const std::ptrdiff_t n = last - first;
  const std::ptrdiff_t stride = std::max<std::ptrdiff_t>(1, n / 4000);
  for (std::ptrdiff_t i = 0; i < n; i += stride) {
    out.push_back(first[i]);
  }
  if (n > 0 && out.back().t != last[-1].t) {
    out.push_back(last[-1]);
  }
  return out;
}

const Sample *Nearest(const std::deque<Sample> &h, double t)
{
  if (h.empty() || std::isnan(t)) {
    return nullptr;
  }
  auto it = std::lower_bound(h.begin(), h.end(), t, Before);
  if (it == h.end() || (it != h.begin() && t - (it - 1)->t <= it->t - t)) {
    --it;
  }
  return &*it;
}

PlotData Gather(const std::map<std::string, std::deque<Sample>> &history,
                const std::vector<std::string> &names, PlotState &p)
{
  static const std::deque<Sample> none;
  std::vector<const std::deque<Sample> *> hs;
  double d0 = INFINITY, d1 = -INFINITY;
  for (const auto &name : names) {
    const auto it = history.find(name);
    hs.push_back(it != history.end() ? &it->second : &none);
    if (!hs.back()->empty()) {
      d0 = std::min(d0, hs.back()->front().t);
      d1 = std::max(d1, hs.back()->back().t);
    }
  }
  PlotData d;
  d.names = names;
  const bool any = d1 >= d0;
  if (any) {
    d.win = Visible(p.view, d0, d1);
  }
  const PlotView &v = p.view;
  for (const auto *h : hs) {
    d.samples.push_back(any ? Thin(*h, d.win.t0, d.win.t1) : std::vector<Sample>{});
    const Sample *a = Nearest(*h, v.a), *b = Nearest(*h, v.b);
    d.at_a.push_back(a ? a->v : NAN);
    d.at_b.push_back(b ? b->v : NAN);
    if (a && std::isnan(d.ta)) {
      d.ta = a->t;
    }
    if (b && std::isnan(d.tb)) {
      d.tb = b->t;
    }
  }
  return d;
}

bool PlotKeys(const Event &e, PlotState &p)
{
  PlotView &v = p.view;
  const int cols = p.box.x_max - p.box.x_min + 1;
  if (e.is_mouse()) {
    const Mouse mo = Event(e).mouse(); // mouse() is not const in this FTXUI
    if (!p.box.Contain(mo.x, mo.y)) {
      return false;
    }
    const double t = TimeAt(v, mo.x - p.box.x_min, cols);
    if (mo.button == Mouse::WheelUp || mo.button == Mouse::WheelDown) {
      Zoom(v, mo.button == Mouse::WheelUp ? 0.5 : 2.0, t);
    } else if (mo.motion == Mouse::Pressed &&
               (mo.button == Mouse::Left || mo.button == Mouse::Right)) {
      Place(v, mo.button == Mouse::Right, t);
    } else {
      return false;
    }
    return true;
  }
  if (e == Event::Character('+') || e == Event::Character('=')) {
    Zoom(v, 0.5, NAN);
  } else if (e == Event::Character('-')) {
    Zoom(v, 2.0, NAN);
  } else if (e == Event::Character('<') || e == Event::Character('>')) {
    Pan(v, e == Event::Character('<') ? -0.25 : 0.25);
  } else if (e == Event::Character('f')) {
    Freeze(v);
  } else if (e == Event::Character('0')) {
    Whole(v);
  } else if (e == Event::Character('a') || e == Event::Character('b')) {
    Cursor(v, e == Event::Character('b'));
  } else if (e == Event::Character(',') || e == Event::Character('.')) {
    Step(v, e == Event::Character(',') ? -1 : 1, cols);
  } else {
    return false;
  }
  return true;
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

std::string CheckValue(const std::string &text, double min, double max, double *out)
{
  const bool limited = std::isfinite(min) && std::isfinite(max);
  double v;
  if (!ParseNumber(text, &v)) {
    return limited ? Format("'%s' is not a number in %g..%g", text.c_str(), min, max)
                   : std::string("not a number");
  }
  if (limited && !(v >= min && v <= max)) {
    return Format("%g is outside %g..%g", v, min, max);
  }
  *out = v;
  return "";
}

Element PlotPane(const PlotData &d, PlotState &p, bool single)
{
  const PlotView &v = p.view;
  const Cursors k{v.a, v.b};
  Elements plots;
  const bool one = single && d.names.size() > 1;
  if (one) {
    plots.push_back(Overlay(d, k) | flex);
  }
  for (std::size_t i = 0; i < d.names.size() && !one; i++) {
    if (i > 0) {
      plots.push_back(separatorLight());
    }
    plots.push_back(Plot(d.names[i], d.samples[i], d.win.t0, d.win.t1,
                         kColors[i % std::size(kColors)], k, Readout(k, d.at_a[i], d.at_b[i])) |
                    flex);
  }
  if (d.names.empty()) {
    plots.push_back(text("nothing to plot") | dim | center | flex);
  } else if (d.win.t1 > d.win.t0) {
    std::string at;
    if (std::isfinite(v.a)) {
      at += Format("A %.4f s  ", d.ta);
    }
    if (std::isfinite(v.b)) {
      at += Format("B %.4f s  ", d.tb);
    }
    if (std::isfinite(v.a) && std::isfinite(v.b)) {
      at += Format("dt %.4f s  ", d.tb - d.ta);
    }
    plots.push_back(
        hbox({v.follow ? text(" LIVE") | dim : text(" FROZEN") | bold | color(Color::Yellow),
              text("  " + at), filler(),
              text(Format("t %.3f .. %.3f s", d.win.t0, d.win.t1)) | dim}));
  }
  // The plot's own keys in its title, where the eye is: the key bar is full
  const std::string keys = "  +- zoom  <> pan  f freeze  a b cursors  ,. move  0 all ";
  return window(text(single ? " Plot: all in one (m: one each)" + keys
                            : " Plot: one each (m: all in one)" + keys),
                vbox(std::move(plots)) | reflect(p.box));
}

Element EventsPane(const std::deque<std::string> &events)
{
  Elements lines;
  for (const auto &e : events) {
    const bool bad = e.find("error") != std::string::npos || e.find("fault") != std::string::npos ||
                     e.find("tripped") != std::string::npos ||
                     e.find("disconnected") != std::string::npos;
    lines.push_back(text(e) | (bad ? color(Color::Red) : nothing));
  }
  return window(text(" Events "), vbox(std::move(lines)) | focusPositionRelative(0, 1) | yframe);
}

} // namespace krumpir::ui
