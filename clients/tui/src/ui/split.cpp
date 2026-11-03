#include "split.hpp"

#include <algorithm>
#include <cctype>

#include <ftxui/component/event.hpp>

#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

PaneSplit::PaneSplit(std::shared_ptr<MonitorLayout> layout, Component pane)
    : layout_(std::move(layout)), plot_el_(std::make_shared<Element>())
{
  auto el = plot_el_;
  plot_ = Renderer([el] { return *el ? *el : text(""); });
  ResizableSplitOption opt;
  opt.main = std::move(pane);
  opt.back = plot_;
  opt.main_size = &layout_->vars_width;
  opt.min = kMinVarsWidth;
  opt.max = &most_;
  split_ = ResizableSplit(std::move(opt));
  shown_ = Maybe(split_, &layout_->vars_shown);
}

void PaneSplit::frame()
{
  total_ = area_.x_max > area_.x_min ? area_.x_max - area_.x_min + 1 : 0;
  layout_->vars_width = ClampSplit(layout_->vars_width, total_, kMinVarsWidth, kMinPlotWidth);
  most_ = total_ > 0 ? std::max(kMinVarsWidth, total_ - 1 - kMinPlotWidth) : 1000;
}

Element PaneSplit::render(Element plot)
{
  *plot_el_ = std::move(plot);
  const Element body = layout_->vars_shown ? split_->Render() : plot_->Render();
  return body | reflect(area_) | flex;
}

bool PaneSplit::on_key(const Event &e)
{
  if (e == Event::Character('v')) {
    layout_->vars_shown = !layout_->vars_shown;
    focus_ = layout_->vars_shown ? 0 : 1; // not on a hidden pane
    return true;
  }
  if (e == Event::Character('[') || e == Event::Character(']')) {
    if (!layout_->vars_shown) {
      layout_->vars_shown = true;
      focus_ = 0;
    }
    const int step = e == Event::Character(']') ? kResizeStep : -kResizeStep;
    layout_->vars_width =
        ClampSplit(layout_->vars_width + step, total_, kMinVarsWidth, kMinPlotWidth);
    return true;
  }
  return false;
}

Component FilterInput(std::string *text)
{
  return Input(text, "filter", OneLine());
}

Element FilterRow(const Component &input)
{
  return hbox({ftxui::text("filter "), input->Render() | underlined});
}

bool MatchesFilter(const std::string &name, const std::string &filter)
{
  const auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
  };
  return filter.empty() || lower(name).find(lower(filter)) != std::string::npos;
}

} // namespace krumpir::ui
