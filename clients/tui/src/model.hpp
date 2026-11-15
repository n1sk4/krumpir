#pragma once

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "service.hpp"

namespace krumpir
{

struct Variable {
  std::string name;
  std::string kind; // "input", "output" or "param" (tunable parameter)
  double value = NAN;
  double min = NAN, max = NAN; // limits for a written value; NaN: the service gave none
};

struct Sample {
  double t, v;
};

// What the UI shows. Written by the model's worker, read under Model::view.
struct Snapshot {
  std::string address;
  bool connected = false;
  bool reconnecting = false; // the link was lost: tried again every `retry` until Disconnect
  bool busy = false;         // a command is on its way
  std::vector<std::string> scenarios, fmus;
  // The runner's `describe` reply for described_path (a scenario, or an FMU's starter)
  std::string described_path;
  json described;
  // The last check, save or load of the form: what the runner said ("" none yet)
  std::string note;
  bool note_ok = false;

  // The service's session ("" until the first reply)
  std::string state, fault, fmu;
  double t = 0, step = 0, duration = 0; // duration 0: runs until stopped
  double steps = 0, overruns = 0, max_step_ns = 0, max_wake_ns = 0;
  double late = 0;    // steps the uart controller did not answer in time
  std::string loaded; // what this client loaded ("" when it found the run already loaded)

  std::vector<Variable> vars; // inputs, outputs, tunable parameters
  // Per variable. Inputs and outputs come from a capture armed at start (every step, also for
  // unpaced runs); parameters, and runs started elsewhere, from the polled values.
  std::map<std::string, std::deque<Sample>> history;
  std::deque<std::string> events; // oldest first
};

// The runner as the UI sees it. A worker thread owns the connection: it runs the commands the UI
// posts, in order, and polls the session every `period` (state, then the variables). The UI never
// waits on the network; it reads the snapshot and is told when it changed. A lost link is tried
// again every `retry`, keeping what is shown; nothing is sent again.
class Model
{
public:
  static constexpr std::size_t kHistory = 200000; // samples per variable, oldest dropped
  static constexpr std::size_t kEvents = 200;

  explicit Model(std::function<void()> on_change,
                 std::chrono::milliseconds period = std::chrono::milliseconds(100),
                 std::chrono::milliseconds retry = std::chrono::seconds(2));
  ~Model();
  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  template <class F> auto view(F &&f) const
  {
    std::lock_guard<std::mutex> lock(mu_);
    return f(static_cast<const Snapshot &>(snap_));
  }

  void connect(std::string address);
  void disconnect(); // closes the link and clears what was shown; a run goes on in the runner
  void refresh_files();
  void describe(std::string path); // a .toml, or an .fmu as its `init` starter scenario
  void load(std::string path);     // a .toml as it is, an .fmu as its `init` starter scenario
  void load_text(std::string label, std::string toml); // an edited scenario; label: its file
  void check(std::string toml);                        // against the FMU, without loading
  void save(std::string path, std::string toml, bool overwrite); // scenarios/<name>.toml
  void start(); // on a STOPPED or FAULT run: resets first, so it runs again from t = 0
  void stop();
  void reset(); // STOPPED or FAULT -> INITIALIZED: the same scenario again
  void unload();
  void write(std::string name, double value); // an input (replaces its signal) or a tunable param
  void signal(json stimulus);                 // one io.stimulus entry, see preset()
  // The samples of names with t in [t0, t1] (NaN: all) to a CSV in the current directory; the
  // event says where. Needs no link.
  void export_csv(std::vector<std::string> names, double t0, double t1);

private:
  void post(std::function<void()> job);
  void worker();
  void guarded(const std::function<void()> &job);
  void reconnect(); // worker thread
  void poll();
  void fetch_vars();
  void fetch_capture();
  void free_capture();                                          // worker thread
  void add_sample(const std::string &name, double t, double v); // caller holds mu_
  void do_reset();                                              // worker thread
  void event(const std::string &text);                          // takes the lock
  void note(const std::string &text, bool ok); // the form's note and an event (takes the lock)
  void do_load(const std::string &label, const json &args); // worker thread

  Service svc_;
  std::map<std::string, std::pair<bool, json>>
      setpoints_;                     // per name: (is a signal, its value or
                                      // stimulus); replayed after a reset (worker thread only)
  int cap_id_ = -1;                   // the capture armed at start, -1: none (worker thread only)
  std::size_t cap_from_ = 0;          // rows of it fetched so far
  std::vector<std::string> captured_; // its variables; the others are polled
  bool want_ = false; // connected by the user: a lost link is tried again (worker thread only)
  std::chrono::steady_clock::time_point tried_; // the last loss or attempt
  std::function<void()> on_change_;
  std::chrono::milliseconds period_, retry_;

  mutable std::mutex mu_; // guards snap_
  Snapshot snap_;

  std::mutex qmu_; // guards jobs_ and quit_
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  bool quit_ = false;
  std::thread thread_; // last: starts after everything above exists
};

bool ends_with(const std::string &s, const std::string &tail);

// The series as CSV: `t,<name>,...`, one row per distinct t of any of them, ascending; a series
// without a sample at that t, or a NaN or inf value, is an empty cell (never interpolated).
// Numbers %.17g, \n line ends. series[k] is names[k]'s, sorted by t. Returns the rows written.
std::size_t write_csv(std::ostream &out, const std::vector<std::string> &names,
                      const std::vector<std::vector<Sample>> &series);
// The samples of names in history with t in [t0, t1] (NaN: all), copied under mu, written to
// dir/krumpir-<stem>-YYYYMMDD-HHMMSS.csv (dir "": the current directory); a file that exists is
// never written over: -2, -3, ... Returns the event text. Worker thread (std::localtime).
std::string write_csv_file(std::mutex &mu, const std::map<std::string, std::deque<Sample>> &history,
                           const std::vector<std::string> &names, double t0, double t1,
                           const std::string &stem, const std::string &dir);

// Signal presets for an input, as the `signal` op takes them. Each has up to four numbers, in
// the order of its `fields` (the UI's labels and defaults).
struct Preset {
  const char *name;
  std::vector<const char *> fields;
  std::vector<const char *> defaults; // "" = the input's current value
};
const std::vector<Preset> &presets();
// The stimulus for presets()[kind] with numbers f (same order as its fields), starting now.
json preset(const std::string &input, std::size_t kind, const std::vector<double> &f);

} // namespace krumpir
