#include "model.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace krumpir {

namespace {

double num(const json &j, const char *key)
{
  const auto it = j.find(key);
  return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

std::string str(const json &j, const char *key)
{
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool readable(const std::string &state)
{
  return state == "INITIALIZED" || state == "RUNNING" || state == "STOPPED";
}

} // namespace

bool ends_with(const std::string &s, const std::string &tail)
{
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

Model::Model(std::function<void()> on_change, std::chrono::milliseconds period)
    : on_change_(std::move(on_change)), period_(period), thread_([this] { worker(); })
{
}

Model::~Model()
{
  {
    std::lock_guard<std::mutex> lock(qmu_);
    quit_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void Model::post(std::function<void()> job)
{
  {
    std::lock_guard<std::mutex> lock(qmu_);
    jobs_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void Model::event(const std::string &text)
{
  char stamp[16];
  const std::time_t now = std::time(nullptr);
  std::strftime(stamp, sizeof stamp, "%H:%M:%S ", std::localtime(&now)); // worker thread only
  std::lock_guard<std::mutex> lock(mu_);
  snap_.events.push_back(stamp + text);
  if (snap_.events.size() > kEvents) {
    snap_.events.pop_front();
  }
}

// Runs one step of work and turns its failure into an event: a refused request leaves the
// connection as it is, a broken link marks the model disconnected.
void Model::guarded(const std::function<void()> &job)
{
  try {
    job();
  } catch (const ServiceError &e) {
    event(std::string("error: ") + e.what());
  } catch (const LinkError &e) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.connected = false;
    }
    event(std::string("disconnected: ") + e.what());
  } catch (const json::exception &e) {
    event(std::string("bad reply: ") + e.what());
  }
}

void Model::worker()
{
  std::unique_lock<std::mutex> lock(qmu_);
  while (!quit_) {
    cv_.wait_for(lock, period_, [this] { return quit_ || !jobs_.empty(); });
    while (!jobs_.empty() && !quit_) {
      auto job = std::move(jobs_.front());
      jobs_.pop_front();
      lock.unlock();
      {
        std::lock_guard<std::mutex> g(mu_);
        snap_.busy = true;
      }
      on_change_();
      guarded(job);
      {
        std::lock_guard<std::mutex> g(mu_);
        snap_.busy = false;
      }
      lock.lock();
    }
    if (quit_) {
      break;
    }
    lock.unlock();
    if (svc_.is_open()) {
      guarded([this] { poll(); });
    }
    on_change_();
    lock.lock();
  }
}

void Model::poll()
{
  const json st = svc_.call("state");
  const std::string state = str(st, "state");
  std::string from;
  bool need_vars = false, read = false;
  std::vector<std::string> names;
  {
    std::lock_guard<std::mutex> lock(mu_);
    from = snap_.state;
    snap_.state = state;
    snap_.fault = str(st, "fault");
    snap_.fmu = str(st, "fmu");
    snap_.t = num(st, "t");
    snap_.step = num(st, "step");
    snap_.duration = num(st, "duration");
    snap_.steps = num(st, "steps");
    snap_.overruns = num(st, "overruns");
    snap_.max_step_ns = num(st, "max_step_ns");
    snap_.max_wake_ns = num(st, "max_wake_ns");
    if (state == "IDLE") {
      snap_.vars.clear();
      snap_.history.clear();
      snap_.loaded.clear();
    }
    need_vars = state != "IDLE" && snap_.vars.empty();
    read = readable(state);
  }
  if (!from.empty() && from != state) {
    event("state " + from + " -> " + state);
    if (state == "FAULT") {
      event("fault: " + str(st, "fault"));
    }
  }
  if (state == "IDLE") {
    cap_id_ = -1;
  }
  if (need_vars) {
    fetch_vars();
  }
  if (!read) {
    return;
  }
  if (cap_id_ >= 0) {
    fetch_capture();
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &v : snap_.vars) {
      names.push_back(v.name);
    }
  }
  if (names.empty()) {
    return;
  }
  const json values = svc_.call("read", {{"names", names}}).at("values");
  std::lock_guard<std::mutex> lock(mu_);
  for (auto &v : snap_.vars) {
    const auto it = values.find(v.name);
    v.value = it != values.end() && it->is_number() ? it->get<double>() : NAN;
    const bool captured = cap_id_ >= 0 && std::find(captured_.begin(), captured_.end(), v.name) !=
                                              captured_.end();
    if (state == "RUNNING" && !captured) {
      add_sample(v.name, snap_.t, v.value);
    }
  }
}

void Model::add_sample(const std::string &name, double t, double v)
{
  auto &h = snap_.history[name];
  if (h.empty() || t > h.back().t) {
    h.push_back({t, v});
    if (h.size() > kHistory) {
      h.pop_front();
    }
  }
}

// The rows the start capture recorded since the last fetch. When it has finished with nothing
// new (the run ended, or its buffer is full on an endless run) the polled values take over.
void Model::fetch_capture()
{
  const json r = svc_.call("capture_fetch", {{"id", cap_id_}, {"from", cap_from_}});
  const json &names = r.at("names");
  const json &rows = r.at("rows");
  if (rows.empty() && str(r, "capture_state") == "FINISHED") {
    cap_id_ = -1;
    return;
  }
  cap_from_ += rows.size();
  captured_ = names.get<std::vector<std::string>>();
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto &row : rows) {
    const double t = row.at(0).get<double>();
    for (std::size_t k = 1; k < names.size() && k < row.size(); k++) {
      add_sample(names[k].get<std::string>(), t, row[k].is_number() ? row[k].get<double>() : NAN);
    }
  }
}

// The Float64 variables a run is watched and steered by, inputs first, then outputs, then
// tunable parameters.
void Model::fetch_vars()
{
  const json all = svc_.call("variables").at("variables");
  std::vector<Variable> vars;
  for (const char *kind : {"input", "output", "param"}) {
    for (const auto &v : all) {
      const std::string causality = str(v, "causality");
      const bool param = causality == "parameter" && v.value("tunable", false);
      if (str(v, "type") == "Float64" && (causality == kind || (param && kind[0] == 'p'))) {
        vars.push_back({str(v, "name"), kind});
      }
    }
  }
  std::lock_guard<std::mutex> lock(mu_);
  snap_.vars = std::move(vars);
}

void Model::connect(std::string address)
{
  post([this, address] {
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto events = std::move(snap_.events);
      snap_ = Snapshot{};
      snap_.address = address;
      snap_.events = std::move(events);
    }
    try {
      svc_.connect(address);
    } catch (const LinkError &e) {
      event(std::string("cannot connect to ") + e.what());
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.connected = true;
    }
    event("connected to " + address);
    const json files = svc_.call("files");
    std::lock_guard<std::mutex> lock(mu_);
    snap_.scenarios = files.at("scenarios").get<std::vector<std::string>>();
    snap_.fmus = files.at("fmus").get<std::vector<std::string>>();
  });
}

void Model::refresh_files()
{
  post([this] {
    const json files = svc_.call("files");
    std::lock_guard<std::mutex> lock(mu_);
    snap_.scenarios = files.at("scenarios").get<std::vector<std::string>>();
    snap_.fmus = files.at("fmus").get<std::vector<std::string>>();
  });
}

void Model::preview(std::string path)
{
  post([this, path] {
    std::string text;
    try {
      text = ends_with(path, ".fmu") ? str(svc_.call("init", {{"fmu", path}}), "toml")
                                     : str(svc_.call("text", {{"path", path}}), "text");
    } catch (const ServiceError &e) {
      text = std::string("# ") + e.what();
    }
    std::lock_guard<std::mutex> lock(mu_);
    snap_.preview_path = path;
    snap_.preview = text;
  });
}

void Model::load(std::string path)
{
  post([this, path] {
    if (str(svc_.call("state"), "state") != "IDLE") {
      svc_.call("unload");
      event("unloaded the previous run");
    }
    if (ends_with(path, ".fmu")) {
      const std::string toml = str(svc_.call("init", {{"fmu", path}}), "toml");
      svc_.call("load", {{"toml", toml}});
    } else {
      svc_.call("load", {{"scenario", path}});
    }
    cap_id_ = -1;
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.loaded = path;
      snap_.vars.clear();
      snap_.history.clear();
    }
    event("loaded " + path);
  });
}

void Model::start()
{
  post([this] {
    cap_id_ = -1;
    cap_from_ = 0;
    captured_.clear();
    try { // every input and output from step 0; refused once running: then polling only
      cap_id_ = svc_.call("capture_start").at("id").get<int>();
    } catch (const ServiceError &) {
    }
    svc_.call("start");
    event("started");
  });
}

void Model::stop()
{
  post([this] {
    event("stop: " + str(svc_.call("stop"), "state"));
  });
}

void Model::unload()
{
  post([this] {
    const json s = svc_.call("unload").at("summary");
    if (str(s, "state") == "IDLE") {
      event("nothing to unload");
      return;
    }
    char line[160];
    std::snprintf(line, sizeof line, "unloaded: %.0f steps, %.0f overruns, step max %.0f us",
                  num(s, "steps"), num(s, "overruns"), num(s, "max_step_ns") / 1e3);
    event(line);
  });
}

} // namespace krumpir
