#include <algorithm>
#include <memory>
#include <sstream>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

Component LoadTab(Model &m, std::string address, std::function<void()> on_loaded)
{
  struct State {
    std::string address, path;
    std::vector<std::string> files; // the runner's scenarios, then its FMUs
    int selected = 0;
    int scroll = 0; // first preview line shown
    int row = 1;    // focus starts on the file list
    Box preview_box;
  };
  auto s = std::make_shared<State>();
  s->address = std::move(address);

  auto pick = [&m, s](const std::string &path) {
    s->path = path;
    s->scroll = 0;
    m.preview(path);
  };
  auto load = [&m, s, on_loaded] {
    if (!s->path.empty()) {
      m.load(s->path);
      on_loaded();
    }
  };

  InputOption one_line;
  one_line.multiline = false;
  auto addr_opt = one_line;
  addr_opt.on_enter = [&m, s] { m.connect(s->address); };
  auto addr = Input(&s->address, "host:port", addr_opt);
  auto connect = Button("Connect", [&m, s] { m.connect(s->address); }, ButtonOption::Ascii());
  auto refresh = Button("Refresh", [&m] { m.refresh_files(); }, ButtonOption::Ascii());

  MenuOption menu = MenuOption::Vertical();
  menu.entries = &s->files;
  menu.selected = &s->selected;
  menu.on_change = [s, pick] {
    if (s->selected >= 0 && s->selected < static_cast<int>(s->files.size())) {
      pick(s->files[static_cast<std::size_t>(s->selected)]);
    }
  };
  menu.on_enter = load;
  auto files = Menu(menu);

  // The preview scrolls with the arrows when focused, and with the mouse wheel over it.
  // A focusable stand-in: the renderer below draws the text, this only takes the keys.
  auto preview = CatchEvent(Renderer([](bool) { return text(""); }), [s](Event e) {
                         int d = 0;
                         if (e == Event::ArrowDown) d = 1;
                         if (e == Event::ArrowUp) d = -1;
                         if (e == Event::PageDown) d = 10;
                         if (e == Event::PageUp) d = -10;
                         if (e.is_mouse() && s->preview_box.Contain(e.mouse().x, e.mouse().y)) {
                           if (e.mouse().button == Mouse::WheelDown) d = 3;
                           if (e.mouse().button == Mouse::WheelUp) d = -3;
                         }
                         s->scroll = std::max(0, s->scroll + d);
                         return d != 0;
                       });

  auto path_opt = one_line;
  path_opt.on_enter = load;
  auto path = Input(&s->path, "path on the runner, e.g. scenarios/dcmotor.toml", path_opt);
  auto load_btn = Button("Load", load, ButtonOption::Ascii());

  auto layout = Container::Vertical({
      Container::Horizontal({addr, connect, refresh}),
      Container::Horizontal({files, preview}),
      Container::Horizontal({path, load_btn}),
  }, &s->row);

  return Renderer(layout, [=, &m] {
    std::string text_shown, shown_path;
    m.view([&](const Snapshot &v) {
      std::vector<std::string> list = v.scenarios;
      list.insert(list.end(), v.fmus.begin(), v.fmus.end());
      if (list != s->files) {
        s->files = std::move(list);
        // The menu sets -1 while the list is empty: clamp from both sides.
        s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->files.size()) - 1));
        if (s->path.empty() && !s->files.empty()) {
          pick(s->files[static_cast<std::size_t>(s->selected)]);
        }
      }
      text_shown = v.preview;
      shown_path = v.preview_path;
      return 0;
    });

    Elements lines;
    if (s->path.empty()) {
      lines.push_back(text("pick a scenario or an FMU") | dim);
    } else if (shown_path != s->path) {
      lines.push_back(text("...") | dim);
    } else {
      std::istringstream in(text_shown);
      std::string line;
      for (int n = 0; std::getline(in, line); n++) {
        if (n >= s->scroll) {
          const bool comment = !line.empty() && line[0] == '#';
          const bool table = !line.empty() && line[0] == '[';
          lines.push_back(text(line) | (comment ? dim : table ? bold : nothing));
        }
      }
    }
    const std::string title = ends_with(s->path, ".fmu") ? " Starter scenario (krumpir init) "
                                                         : " Preview ";
    auto files_pane = window(text(" Files on the runner "),
                             files->Render() | vscroll_indicator | frame) |
                      size(WIDTH, EQUAL, 36);
    auto preview_pane = window(text(title) | (preview->Focused() ? inverted : nothing),
                               vbox(std::move(lines)) | yframe | reflect(s->preview_box)) |
                        flex;
    return vbox({
        hbox({text(" Runner "), addr->Render() | size(WIDTH, EQUAL, 24) | underlined, text(" "),
              connect->Render(), text(" "), refresh->Render()}),
        text(""),
        hbox({files_pane, preview_pane}) | flex,
        hbox({text(" Path "), path->Render() | underlined | flex, text(" "), load_btn->Render()}),
    });
  });
}

} // namespace krumpir::ui
