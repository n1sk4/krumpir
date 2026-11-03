#include <algorithm>
#include <deque>
#include <memory>
#include <sstream>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include "form.hpp"
#include "ui.hpp"

namespace krumpir::ui {

using namespace ftxui;

namespace {

// scenarios/<name>.toml for a picked file: the same name for a scenario, the FMU's for an FMU.
std::string save_as_default(const std::string &path)
{
  const auto slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  name = name.substr(0, name.find_last_of('.'));
  return "scenarios/" + name + (ends_with(path, ".toml") ? "_edited" : "") + ".toml";
}

// A one-line input that leaves up and down to the grid, so the arrows walk the rows.
class RowInput : public ComponentBase {
public:
  explicit RowInput(Component input) { Add(std::move(input)); }
  bool OnEvent(Event e) override
  {
    return e != Event::ArrowUp && e != Event::ArrowDown && ComponentBase::OnEvent(e);
  }
};

} // namespace

Component LoadTab(Model &m, std::string address, std::function<void()> on_loaded)
{
  struct State {
    std::string address, path; // path: the file picked
    std::vector<std::string> files; // the runner's scenarios, then its FMUs
    int selected = 0, row = 1;
    int scroll = 0; // first text line shown
    Box text_box;
    int form_scroll = 0, form_rows = 0; // first form row shown, rows in all
    int focus_row = -1;                 // the focused field's row when last drawn
    Box form_box;

    Form form;                  // of `path`, from the runner's description
    std::string built_from;     // the description the form was built from
    std::vector<int> widget_of; // field -> child of the grid, -1: none
    std::vector<int> slot_of;   // field -> its pick or tick
    std::deque<int> picks;      // dropdown choices
    std::deque<bool> ticks;     // checkboxes
    std::deque<int> cursors;    // inputs: start at the end of the text
    bool raw = false;           // the text instead of the form
    int view = 0;               // 0 form, 1 text
    std::string hint, saving;   // saving: the path a save is on its way to

    bool ask_discard = false, ask_save = false, ask_save_as = false, replace = false;
    int pending = 0; // the file to switch to once the edits are discarded
    std::string save_path;
    int discard_row = 0, save_row = 0, save_as_row = 0;
  };
  auto s = std::make_shared<State>();
  s->address = std::move(address);

  auto describe = [&m, s](const std::string &path) {
    s->path = path;
    s->scroll = s->form_scroll = 0;
    s->focus_row = -1;
    s->hint.clear();
    m.describe(path);
  };
  auto pick = [s, describe] {
    if (s->selected >= 0 && s->selected < static_cast<int>(s->files.size())) {
      describe(s->files[static_cast<std::size_t>(s->selected)]);
    }
  };
  auto toml = [s] { return s->form.to_toml(); };
  auto usable = [s] {
    s->form.check();
    if (!s->form.ok()) {
      s->hint = "fix the fields in red first";
    }
    return s->form.ok();
  };

  auto check = [&m, s, usable, toml] {
    if (!s->form.fields.empty() && usable()) {
      m.check(toml());
    }
  };
  auto load = [&m, s, usable, toml, on_loaded] {
    if (s->path.empty() || (!s->form.fields.empty() && !usable())) {
      return;
    }
    if (s->form.dirty()) {
      m.load_text(s->path, toml());
    } else {
      m.load(s->path);
    }
    on_loaded();
  };
  auto save = [s, usable] {
    if (s->form.fields.empty() || !usable()) {
      return;
    }
    if (ends_with(s->path, ".toml") && s->path.rfind("scenarios/", 0) == 0) {
      s->save_row = 0;
      s->ask_save = true;
    } else {
      s->save_path = save_as_default(s->path);
      s->save_as_row = 0;
      s->ask_save_as = true;
    }
  };
  auto save_as = [s, usable] {
    if (!s->form.fields.empty() && usable()) {
      s->save_path = save_as_default(s->path);
      s->replace = false;
      s->save_as_row = 0;
      s->ask_save_as = true;
    }
  };
  auto revert = [s] {
    for (std::size_t i = 0; i < s->form.fields.size(); i++) {
      Field &f = s->form.fields[i];
      f.value = f.original;
      if (s->slot_of[i] >= 0 && f.kind == Field::Choice) {
        const auto it = std::find(f.choices.begin(), f.choices.end(), f.value);
        s->picks[static_cast<std::size_t>(s->slot_of[i])] =
            static_cast<int>(it == f.choices.end() ? 0 : it - f.choices.begin());
      } else if (s->slot_of[i] >= 0 && f.kind == Field::Flag) {
        s->ticks[static_cast<std::size_t>(s->slot_of[i])] = f.value == "true";
      }
    }
    s->form.check();
  };

  InputOption one_line;
  one_line.multiline = false;
  auto addr_opt = one_line;
  addr_opt.on_enter = [&m, s] { m.connect(s->address); };
  auto addr = Input(&s->address, "host:port", addr_opt);
  auto connect = Button("Connect", [&m, s] { m.connect(s->address); }, ButtonOption::Ascii());
  auto disconnect = Button("Disconnect", [&m] { m.disconnect(); }, ButtonOption::Ascii());
  auto refresh = Button("Refresh", [&m] { m.refresh_files(); }, ButtonOption::Ascii());

  MenuOption menu = MenuOption::Vertical();
  menu.entries = &s->files;
  menu.selected = &s->selected;
  menu.on_change = [s, pick] {
    const auto it = std::find(s->files.begin(), s->files.end(), s->path);
    const int shown = static_cast<int>(it - s->files.begin());
    if (s->form.dirty() && s->selected != shown && it != s->files.end()) {
      s->pending = s->selected; // ask before the edits are lost
      s->selected = shown;
      s->discard_row = 0;
      s->ask_discard = true;
      return;
    }
    pick();
  };
  menu.on_enter = load;
  auto files = Menu(menu);

  // The text scrolls with the arrows when focused, and with the mouse wheel over it.
  // A focusable stand-in: the renderer below draws the text, this only takes the keys.
  auto text_view = CatchEvent(Renderer([](bool) { return text(""); }), [s](Event e) {
    int d = 0;
    if (e == Event::ArrowDown) d = 1;
    if (e == Event::ArrowUp) d = -1;
    if (e == Event::PageDown) d = 10;
    if (e == Event::PageUp) d = -10;
    if (e.is_mouse() && s->text_box.Contain(e.mouse().x, e.mouse().y)) {
      if (e.mouse().button == Mouse::WheelDown) d = 3;
      if (e.mouse().button == Mouse::WheelUp) d = -3;
    }
    s->scroll = std::max(0, s->scroll + d);
    return d != 0;
  });

  auto grid = Container::Vertical({}); // one widget per editable field, built per description
  auto buttons = Container::Horizontal({
      Button("Check", check, ButtonOption::Ascii()), Button("Load", load, ButtonOption::Ascii()),
      Button("Save", save, ButtonOption::Ascii()), Button("Save as", save_as, ButtonOption::Ascii()),
      Button("Revert", revert, ButtonOption::Ascii()),
      Button("Text", [s] { s->raw = !s->raw; }, ButtonOption::Ascii())});
  auto form_view = Container::Vertical({grid, buttons});

  auto path_opt = one_line;
  path_opt.on_enter = [s, describe] { describe(s->path); };
  auto path = Input(&s->path, "path on the runner, e.g. scenarios/dcmotor.toml", path_opt);

  auto layout = Container::Vertical({
      Container::Horizontal({addr, connect, disconnect, refresh}),
      Container::Horizontal({files, Container::Tab({form_view, text_view}, &s->view)}),
      path,
  }, &s->row);

  // The widgets of a new description. Pointers into the form's fields stay valid: the field
  // list is not resized until the next build.
  auto build = [s, grid](const json &reply, const std::vector<std::string> &fmus) {
    grid->DetachAllChildren();
    s->picks.clear();
    s->ticks.clear();
    s->cursors.clear();
    s->form = Form::from_describe(reply, fmus);
    const std::size_t n = s->form.fields.size();
    s->widget_of.assign(n, -1);
    s->slot_of.assign(n, -1);
    int k = 0;
    for (std::size_t i = 0; i < n; i++) {
      Field &f = s->form.fields[i];
      Component w;
      if (f.kind == Field::Number || f.kind == Field::Integer || f.kind == Field::Text) {
        s->cursors.push_back(static_cast<int>(f.value.size()));
        InputOption o;
        o.multiline = false;
        o.cursor_position = &s->cursors.back();
        w = Make<RowInput>(Input(&f.value, "", o));
      } else if (f.kind == Field::Choice) {
        const auto it = std::find(f.choices.begin(), f.choices.end(), f.value);
        s->picks.push_back(static_cast<int>(it == f.choices.end() ? 0 : it - f.choices.begin()));
        s->slot_of[i] = static_cast<int>(s->picks.size()) - 1;
        DropdownOption o; // one line while closed, the choices under it when open
        o.radiobox.entries = &f.choices;
        o.radiobox.selected = &s->picks.back();
        o.transform = [](bool open, Element checkbox, Element radiobox) {
          return open ? vbox({checkbox, radiobox | borderLight}) : checkbox;
        };
        w = Dropdown(o);
      } else if (f.kind == Field::Flag) {
        s->ticks.push_back(f.value == "true");
        s->slot_of[i] = static_cast<int>(s->ticks.size()) - 1;
        w = Checkbox("", &s->ticks.back());
      } else {
        continue;
      }
      grid->Add(Maybe(w, &f.shown));
      s->widget_of[i] = k++;
    }
  };

  auto view = Renderer(layout, [=, &m] {
    std::string note;
    bool note_ok = false;
    Element events;
    m.view([&](const Snapshot &v) {
      events = EventsPane(v.events);
      std::vector<std::string> list = v.scenarios;
      list.insert(list.end(), v.fmus.begin(), v.fmus.end());
      if (list != s->files) {
        s->files = std::move(list);
        // The menu sets -1 while the list is empty: clamp from both sides.
        s->selected = std::clamp(s->selected, 0, std::max(0, static_cast<int>(s->files.size()) - 1));
        if (s->path.empty() && !s->files.empty()) {
          pick();
        }
      }
      if (v.described_path == s->path && !s->path.empty()) {
        std::string dump = v.described.dump();
        if (dump != s->built_from) {
          s->built_from = std::move(dump);
          build(v.described, v.fmus);
        }
        note = v.note;
        note_ok = v.note_ok;
      }
      return 0;
    });
    if (!s->saving.empty() && note_ok && note == "saved " + s->saving) { // now the file's text
      for (auto &f : s->form.fields) {
        f.original = f.value;
      }
      s->saving.clear();
    }

    // Widgets -> values, then the checks
    Form &form = s->form;
    for (std::size_t i = 0; i < form.fields.size(); i++) {
      Field &f = form.fields[i];
      const int slot = s->slot_of[i];
      if (slot >= 0 && f.kind == Field::Choice && !f.choices.empty()) {
        f.value = f.choices[static_cast<std::size_t>(
            std::clamp(s->picks[static_cast<std::size_t>(slot)], 0,
                       static_cast<int>(f.choices.size()) - 1))];
      } else if (slot >= 0 && f.kind == Field::Flag) {
        f.value = s->ticks[static_cast<std::size_t>(slot)] ? "true" : "false";
      }
    }
    form.check();
    const bool described = !s->built_from.empty();
    s->view = s->raw || form.fields.empty() ? 1 : 0;

    Element body;
    if (s->view == 0) {
      // label | widget | unit | error or hint, a header row per section
      // Rows scroll by leaving out the first ones, so the column widths are fixed from all
      // fields (else they would change while scrolling).
      std::vector<Elements> rows;
      std::string section;
      int label_width = 0, unit_width = 0, focused_row = -1;
      for (const auto &f : form.fields) {
        label_width = std::max(label_width, static_cast<int>(f.label.size()) + 2);
        unit_width = std::max(unit_width, f.unit.empty() ? 0 : static_cast<int>(f.unit.size()) + 2);
      }
      for (std::size_t i = 0; i < form.fields.size(); i++) {
        const Field &f = form.fields[i];
        if (!f.shown) {
          continue;
        }
        if (f.section != section) {
          section = f.section;
          rows.push_back({text("[" + section + "]") | bold | color(Color::Cyan), text(""),
                          text(""), text("") | xflex_shrink}); // a column shrinks if all its cells do
        }
        const bool changed = f.value != f.original;
        const bool named = !note_ok && !note.empty() &&
                           note.find("'" + f.label + "'") != std::string::npos;
        Element label = text(" " + f.label + " ") | (changed ? bold : nothing) |
                        (f.from_fmu && !changed ? dim : nothing) |
                        (named || !f.error.empty() ? color(Color::Red) : nothing) |
                        size(WIDTH, EQUAL, std::min(label_width, 32));
        Element widget = text(f.value) | dim;
        if (s->widget_of[i] >= 0) {
          const Component w = grid->ChildAt(static_cast<std::size_t>(s->widget_of[i]));
          widget = w->Render();
          if (f.kind == Field::Number || f.kind == Field::Integer || f.kind == Field::Text) {
            widget = widget | underlined;
          }
          if (!f.error.empty()) {
            widget = widget | color(Color::Red);
          } else if (f.from_fmu && !changed) {
            widget = widget | dim;
          }
          if (w->Focused()) {
            focused_row = static_cast<int>(rows.size());
          }
        }
        widget = widget | size(WIDTH, EQUAL, 24);
        rows.push_back({label, widget,
                        text(f.unit.empty() ? "" : " " + f.unit + " ") | dim |
                            size(WIDTH, EQUAL, unit_width),
                        // the hints give way first when the pane is narrow
                        (f.error.empty() ? text(" " + f.hint) | dim
                                         : text(" " + f.error) | color(Color::Red)) |
                            xflex_shrink});
      }
      // Keep the focused field in view when the focus moves; the wheel scrolls freely.
      const int n = static_cast<int>(rows.size());
      const int visible = std::max(1, s->form_box.y_max - s->form_box.y_min + 1);
      if (focused_row >= 0 && focused_row != s->focus_row) {
        s->focus_row = focused_row;
        if (focused_row < s->form_scroll + 1) {
          s->form_scroll = focused_row - 1; // with its section header when that is just above
        } else if (focused_row >= s->form_scroll + visible) {
          s->form_scroll = focused_row - visible + 1;
        }
      }
      s->form_scroll = std::clamp(s->form_scroll, 0, std::max(0, n - visible));
      s->form_rows = n;
      const std::string where =
          n > visible ? Format(" rows %d-%d of %d ", s->form_scroll + 1,
                               std::min(n, s->form_scroll + visible), n)
                      : std::string();
      // Only the rows that fit: a focused input would make the frame scroll on its own.
      rows = std::vector<Elements>(rows.begin() + s->form_scroll,
                                   rows.begin() + std::min(n, s->form_scroll + visible));
      // A scroll bar while it does not all fit: the thumb is the part on screen.
      Element bar = text("");
      if (n > visible) {
        const int len = std::max(1, visible * visible / n);
        const int top = std::min(visible - len, s->form_scroll * visible / n);
        Elements cells;
        for (int y = 0; y < visible; y++) {
          const bool thumb = y >= top && y < top + len;
          cells.push_back(thumb ? text("┃") | color(Color::Cyan) : text("│") | dim);
        }
        bar = vbox(std::move(cells));
      }
      body = vbox({hbox({gridbox(std::move(rows)) | yframe | flex | reflect(s->form_box), bar}) |
                       flex,
                   separatorLight(), hbox({buttons->Render(), filler(), text(where) | dim})});
    } else {
      // The text: as edited when there are edits, else as the runner described it
      Elements lines;
      if (!form.problem.empty()) {
        lines.push_back(paragraph(form.problem) | color(Color::Red));
      }
      const std::string shown = form.dirty() ? form.to_toml() : form.text;
      s->scroll = std::min(s->scroll, std::max(0, static_cast<int>(std::count(
                                                      shown.begin(), shown.end(), '\n')) - 1));
      std::istringstream in(shown);
      std::string line;
      for (int n = 0; std::getline(in, line); n++) {
        if (n >= s->scroll) {
          const bool comment = !line.empty() && line[0] == '#';
          const bool table = !line.empty() && line[0] == '[';
          lines.push_back(text(line) | (comment ? dim : table ? bold : nothing));
        }
      }
      if (!described) {
        lines = {text(s->path.empty() ? "pick a scenario or an FMU" : "...") | dim};
      }
      body = vbox(std::move(lines)) | yframe | reflect(s->text_box);
      if (!form.fields.empty()) {
        body = vbox({body | flex, separatorLight(), buttons->Render()});
      }
    }

    const std::string title = " " + (s->path.empty() ? std::string("Scenario") : s->path) +
                              (ends_with(s->path, ".fmu") ? " (starter scenario)" : "") +
                              (form.dirty() ? " (edited)" : "") + (s->raw ? " as text " : " ");
    Element notes = text("");
    if (!s->hint.empty()) {
      notes = text(" " + s->hint) | color(Color::Yellow);
    } else if (!note.empty()) {
      notes = text(" " + note) | color(note_ok ? Color::Green : Color::Red);
    }
    auto files_pane = window(text(" Files on the runner "),
                             files->Render() | vscroll_indicator | frame) |
                      size(WIDTH, EQUAL, 36);
    auto form_pane = window(text(title) | (text_view->Focused() ? inverted : nothing),
                            vbox({notes, body | flex})) |
                     flex;
    return vbox({
        hbox({text(" Runner "), addr->Render() | size(WIDTH, EQUAL, kAddressWidth) | underlined,
              text(" "),
              connect->Render(), text(" "), disconnect->Render(), text(" "), refresh->Render()}),
        text(""),
        hbox({files_pane, form_pane}) | flex,
        hbox({text(" Path "), path->Render() | underlined | flex}),
        events | size(HEIGHT, EQUAL, kEventsRows),
    });
  });

  // Dialogs
  auto close = [s] { s->ask_discard = s->ask_save = s->ask_save_as = false; };
  auto discard_buttons = Container::Horizontal(
      {Button("Discard", [s, pick] {
         s->ask_discard = false;
         s->selected = s->pending;
         pick();
       }, ButtonOption::Ascii()),
       Button("Keep editing", close, ButtonOption::Ascii())},
      &s->discard_row);
  auto discard_dialog = Renderer(discard_buttons, [=] {
    return window(text(" Unsaved edits "),
                  vbox({text("The edits to " + s->path + " are not saved."),
                        text("Discard them and open the other file?"),
                        discard_buttons->Render() | align_right})) |
           size(WIDTH, GREATER_THAN, 50);
  });

  auto save_buttons = Container::Horizontal(
      {Button("Save", [&m, s, toml] {
         s->ask_save = false;
         s->saving = s->path;
         m.save(s->path, toml(), true);
       }, ButtonOption::Ascii()),
       Button("Cancel", close, ButtonOption::Ascii())},
      &s->save_row);
  auto save_dialog = Renderer(save_buttons, [=] {
    return window(text(" Save "),
                  vbox({text("Write the form to " + s->path + " on the runner?"),
                        text("The file is written from the form: its comments and layout are "
                             "not kept.") | dim,
                        save_buttons->Render() | align_right})) |
           size(WIDTH, GREATER_THAN, 50);
  });

  auto do_save_as = [&m, s, toml] {
    s->ask_save_as = false;
    s->saving = s->save_path;
    m.save(s->save_path, toml(), s->replace);
  };
  auto as_opt = one_line;
  as_opt.on_enter = do_save_as;
  auto as_path = Input(&s->save_path, "scenarios/<name>.toml", as_opt);
  auto as_replace = Checkbox("replace it if it exists", &s->replace);
  auto as_buttons = Container::Horizontal({Button("Save", do_save_as, ButtonOption::Ascii()),
                                           Button("Cancel", close, ButtonOption::Ascii())});
  auto save_as_dialog =
      Renderer(Container::Vertical({as_path, as_replace, as_buttons}, &s->save_as_row), [=] {
        return window(text(" Save as "),
                      vbox({hbox({text("path "), as_path->Render() | underlined | flex}),
                            as_replace->Render(), text("on the runner, in scenarios/") | dim,
                            as_buttons->Render() | align_right})) |
               size(WIDTH, GREATER_THAN, 56);
      });

  auto with_dialogs = Modal(Modal(Modal(view, discard_dialog, &s->ask_discard), save_dialog,
                                  &s->ask_save),
                            save_as_dialog, &s->ask_save_as);
  return CatchEvent(with_dialogs, [s, files, text_view, close](const Event &e) {
    if (s->ask_discard || s->ask_save || s->ask_save_as) {
      if (e == Event::Escape) {
        close();
        return true;
      }
      return false;
    }
    if (e.is_character()) {
      s->hint.clear();
    }
    // The form scrolls with the wheel over it, and a page at a time with PageUp / PageDown.
    const int page = std::max(1, s->form_box.y_max - s->form_box.y_min);
    int d = 0;
    Event ev = e; // mouse() is not const
    if (s->view == 0 && ev.is_mouse() && s->form_box.Contain(ev.mouse().x, ev.mouse().y)) {
      d = ev.mouse().button == Mouse::WheelDown ? 3 : ev.mouse().button == Mouse::WheelUp ? -3 : 0;
    } else if (s->view == 0 && (e == Event::PageDown || e == Event::PageUp)) {
      d = e == Event::PageDown ? page : -page;
    }
    if (d != 0) {
      s->form_scroll = std::clamp(s->form_scroll + d, 0, std::max(0, s->form_rows - 1));
      return true;
    }
    if (e == Event::Character('v') && (files->Focused() || text_view->Focused())) {
      s->raw = !s->raw;
      return true;
    }
    return false;
  });
}

} // namespace krumpir::ui
