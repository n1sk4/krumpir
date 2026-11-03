#pragma once

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "model.hpp" // Sample
#include "xcp.hpp"

namespace krumpir {

// A controller of the repo: controllers/<dir>/src/gen/ctrl_gen.json (written by
// tools/codegen/gen_controller.py) and the A2L file it names.
struct Controller {
  std::string dir;  // controllers/<dir>
  std::string name; // e.g. "t12"
  std::string a2l;  // path of its A2L file (exists only after an XCP build, see the READMEs)
  json desc;        // ctrl_gen.json
};

// Every controller under root/controllers, sorted by folder.
std::vector<Controller> find_controllers(const std::string &root);

struct CalParam {
  std::string name, cal, unit, help; // cal: the A2L name of its calibration value ("" when none)
  double min = 0, max = 0, def = 0;
  double value = NAN; // read from the ECU
};

struct EcuSnapshot {
  std::vector<Controller> controllers;
  std::string selected; // dir of the opened controller ("" when none)
  std::string a2l_error; // why its A2L could not be read
  std::string epk;       // of the opened A2L

  std::string address; // host:port in use
  bool connected = false, busy = false;
  double rtt_ms = 0; // last poll: round trip of one read

  bool running = false;
  long long trip = 0; // 0: none
  std::string trip_name;

  std::vector<CalParam> params;
  std::vector<std::string> measurements; // readable scalar names of the A2L, sorted
  std::vector<std::string> monitor;      // the description's default signals
  std::vector<std::string> watched;      // polled now
  std::map<std::string, double> values;  // last values of the watched names
  std::map<std::string, std::deque<Sample>> history; // t: s since connecting
  std::deque<std::string> events;
};

// A controller as the UI sees it, like Model for the runner: a worker thread owns the XCP link,
// runs the commands the UI posts in order and polls the status, the calibration values and the
// watched signals every `period`.
class EcuModel {
public:
  static constexpr std::size_t kHistory = 100000;
  static constexpr std::size_t kEvents = 200;

  explicit EcuModel(std::function<void()> on_change,
                    std::chrono::milliseconds period = std::chrono::milliseconds(50),
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
  ~EcuModel();
  EcuModel(const EcuModel &) = delete;
  EcuModel &operator=(const EcuModel &) = delete;

  template <class F> auto view(F &&f) const
  {
    std::lock_guard<std::mutex> lock(mu_);
    return f(static_cast<const EcuSnapshot &>(snap_));
  }

  void scan(std::string root);   // find the controllers
  void open(std::string dir);    // a controller of the scan: its description and A2L
  void connect(std::string address); // "" : the A2L's address
  void disconnect();
  void write(std::string param, double value); // by parameter name, checked against its limits
  void request(std::string command);           // "start", "stop" or "clear"
  void watch(std::vector<std::string> names);  // the signals to poll and keep a history of

private:
  void post(std::function<void()> job);
  void worker();
  void guarded(const std::function<void()> &job);
  void poll();
  void event(const std::string &text);

  Ecu ecu_;
  A2l a2l_; // of the opened controller (worker thread only)
  json desc_;
  std::chrono::steady_clock::time_point t0_;
  std::function<void()> on_change_;
  std::chrono::milliseconds period_;

  mutable std::mutex mu_; // guards snap_
  EcuSnapshot snap_;

  std::mutex qmu_; // guards jobs_ and quit_
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  bool quit_ = false;
  std::thread thread_; // last: starts after everything above exists
};

} // namespace krumpir
