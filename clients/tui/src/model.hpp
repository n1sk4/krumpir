#pragma once

#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "service.hpp"

namespace krumpir {

struct Variable {
  std::string name;
  std::string kind; // "input", "output" or "param" (tunable parameter)
  double value = NAN;
};

struct Sample {
  double t, v;
};

// What the UI shows. Written by the model's worker, read under Model::view.
struct Snapshot {
  std::string address;
  bool connected = false;
  bool busy = false; // a command is on its way
  std::vector<std::string> scenarios, fmus;
  std::string preview_path, preview;

  // The service's session ("" until the first reply)
  std::string state, fault, fmu;
  double t = 0, step = 0, duration = 0; // duration 0: runs until stopped
  double steps = 0, overruns = 0, max_step_ns = 0, max_wake_ns = 0;
  std::string loaded; // what this client loaded ("" when it found the run already loaded)

  std::vector<Variable> vars;                    // inputs, outputs, tunable parameters
  // Per variable. Inputs and outputs come from a capture armed at start (every step, also for
  // unpaced runs); parameters, and runs started elsewhere, from the polled values.
  std::map<std::string, std::deque<Sample>> history;
  std::deque<std::string> events;                // oldest first
};

// The runner as the UI sees it. A worker thread owns the connection: it runs the commands the UI
// posts, in order, and polls the session every `period` (state, then the variables). The UI never
// waits on the network; it reads the snapshot and is told when it changed.
class Model {
public:
  static constexpr std::size_t kHistory = 200000; // samples per variable, oldest dropped
  static constexpr std::size_t kEvents = 200;

  explicit Model(std::function<void()> on_change,
                 std::chrono::milliseconds period = std::chrono::milliseconds(100));
  ~Model();
  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  template <class F> auto view(F &&f) const
  {
    std::lock_guard<std::mutex> lock(mu_);
    return f(static_cast<const Snapshot &>(snap_));
  }

  void connect(std::string address);
  void refresh_files();
  void preview(std::string path);
  void load(std::string path); // a .toml as it is, an .fmu as its `init` starter scenario
  void start();
  void stop();
  void unload();

private:
  void post(std::function<void()> job);
  void worker();
  void guarded(const std::function<void()> &job);
  void poll();
  void fetch_vars();
  void fetch_capture();
  void add_sample(const std::string &name, double t, double v); // caller holds mu_
  void event(const std::string &text); // takes the lock

  Service svc_;
  int cap_id_ = -1;           // the capture armed at start, -1: none (worker thread only)
  std::size_t cap_from_ = 0;  // rows of it fetched so far
  std::vector<std::string> captured_; // its variables; the others are polled
  std::function<void()> on_change_;
  std::chrono::milliseconds period_;

  mutable std::mutex mu_; // guards snap_
  Snapshot snap_;

  std::mutex qmu_; // guards jobs_ and quit_
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  bool quit_ = false;
  std::thread thread_; // last: starts after everything above exists
};

bool ends_with(const std::string &s, const std::string &tail);

} // namespace krumpir
