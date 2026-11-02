#include <cstdio>
#include <cstring>
#include <string>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>

#include "model.hpp"
#include "ui/ui.hpp"

int main(int argc, char **argv)
{
  if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
    std::printf("usage: krumpir-tui [host:port]   (default 127.0.0.1:7700, a `krumpir serve`)\n");
    return argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)
               ? 0
               : 2;
  }
  const std::string address = argc == 2 ? argv[1] : "127.0.0.1:7700";

  auto app = ftxui::App::Fullscreen();
  // Declared after the app, so it is destroyed first: its worker posts redraws to the app.
  krumpir::Model model([&app] { app.PostEvent(ftxui::Event::Custom); });
  model.connect(address);
  app.Loop(krumpir::ui::Root(model, address, app.ExitLoopClosure()));
  return 0;
}
