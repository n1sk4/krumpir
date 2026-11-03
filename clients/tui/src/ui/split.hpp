#pragma once

#include <memory>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "layout.hpp"

namespace krumpir::ui {

// The pane (variables or signals) and the plot of a Monitor page, side by side with a divider that
// can be dragged, and the keys that hide and resize the pane. Every page shares one MonitorLayout,
// so the size does not change when you switch target.
//
//   split = make_shared<PaneSplit>(layout, pane);
//   page  = Container::Vertical({split->component(), buttons}, split->focus());
//   render: split->frame(); ...build rows with split->name_width(...)...; split->render(plot)
//   events: split->on_key(e)
class PaneSplit {
public:
  PaneSplit(std::shared_ptr<MonitorLayout> layout, ftxui::Component pane);
  PaneSplit(const PaneSplit &) = delete;
  PaneSplit &operator=(const PaneSplit &) = delete;

  // The first child of the page's container: not focusable while the pane is hidden.
  ftxui::Component component() const { return shown_; }
  // The container's selector: 0 the pane, 1 the rest. Moved off the pane when it is hidden.
  int *focus() { return &focus_; }
  bool shown() const { return layout_->vars_shown; }

  // Call first in every render: takes the width the area had last frame and keeps the pane in it.
  void frame();
  // Room for a name in the pane, when the rest of the row takes other_columns.
  int name_width(int other_columns) const { return NameWidth(layout_->vars_width, other_columns); }
  // The split, or the plot alone while the pane is hidden. Fills the room it is given.
  ftxui::Element render(ftxui::Element plot);
  // v hides or shows the pane, [ and ] make it narrower or wider. True when the key was one of them.
  bool on_key(const ftxui::Event &e);

private:
  std::shared_ptr<MonitorLayout> layout_;
  std::shared_ptr<ftxui::Element> plot_el_; // what the plot half draws this frame
  ftxui::Component split_, plot_, shown_;
  ftxui::Box area_; // the room of the split, as last drawn
  int total_ = 0;   // its width, 0 before the first frame
  int most_ = 1000; // widest pane that leaves the plot its room
  int focus_ = 0;
};

} // namespace krumpir::ui
