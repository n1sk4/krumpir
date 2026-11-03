#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>

#include "ecu_model.hpp"
#include "model.hpp"
#include "ui/ui.hpp"

namespace fs = std::filesystem;

// The first folder with a controllers/ folder, from `from` upwards ("" when none).
static std::string find_repo(fs::path from)
{
  std::error_code ec;
  for (from = fs::absolute(from, ec); !from.empty(); from = from.parent_path()) {
    if (fs::is_directory(from / "controllers", ec)) {
      return from.generic_string();
    }
    if (from == from.parent_path()) {
      break;
    }
  }
  return "";
}

int main(int argc, char **argv)
{
  std::string address = "127.0.0.1:7700", repo;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--repo") == 0 && i + 1 < argc) {
      repo = argv[++i];
    } else if (argv[i][0] != '-') {
      address = argv[i];
    } else {
      std::printf("usage: krumpir-tui [host:port] [--repo DIR]\n"
                  "  host:port  a `krumpir serve` (default 127.0.0.1:7700)\n"
                  "  --repo     the krumpir checkout with controllers/ (default: found upwards\n"
                  "             from the current folder, then from the program's)\n");
      return std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0 ? 0 : 2;
    }
  }
  if (repo.empty()) {
    repo = find_repo(fs::current_path());
  }
  if (repo.empty()) {
    repo = find_repo(fs::path(argv[0]).parent_path());
  }

  if (repo.empty()) {
    repo = ".";
  }

  auto app = ftxui::App::Fullscreen();
  auto redraw = [&app] { app.PostEvent(ftxui::Event::Custom); };
  // Declared after the app, so they are destroyed first: their workers post redraws to the app.
  krumpir::Model model(redraw);
  model.connect(address);
  std::vector<std::unique_ptr<krumpir::EcuModel>> ecus; // one link per controller
  krumpir::ui::Controllers cards;
  for (const auto &c : krumpir::find_controllers(repo)) {
    ecus.push_back(std::make_unique<krumpir::EcuModel>(redraw));
    ecus.back()->scan(repo);
    ecus.back()->open(c.dir);
    cards.emplace_back(c.name, ecus.back().get());
  }
  app.Loop(krumpir::ui::Root(model, cards, address, app.ExitLoopClosure()));
  return 0;
}
