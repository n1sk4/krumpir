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

const std::vector<Preset> &presets()
{
  static const std::vector<Preset> p{
      {"constant", {"value"}, {""}},
      {"step", {"from", "after s", "to"}, {"", "1", ""}},
      {"ramp", {"from", "to", "over s"}, {"", "", "1"}},
      {"sine", {"offset", "amplitude", "freq Hz"}, {"", "1", "1"}},
      {"pulse", {"low", "high", "period s", "duty 0..1"}, {"", "1", "1", "0.5"}},
  };
  return p;
}

json preset(const std::string &input, std::size_t kind, const std::vector<double> &f)
{
  switch (kind) {
  case 0:
    return {{"input", input}, {"kind", "constant"}, {"value", f.at(0)}};
  case 1: // the signal starts now, so the step is `after` seconds from now
    if (!(f.at(1) > 0)) {
      return {{"input", input}, {"kind", "constant"}, {"value", f.at(2)}};
    }
    return {{"input", input},
            {"segments", {{{"kind", "const"}, {"value", f.at(0)}, {"duration", f.at(1)}},
                          {{"kind", "const"}, {"value", f.at(2)}}}}};
  case 2: // then holds `to`
    return {{"input", input},
            {"segments", {{{"kind", "ramp"}, {"from", f.at(0)}, {"to", f.at(1)},
                           {"duration", f.at(2)}}}}};
  case 3:
    return {{"input", input},
            {"kind", "sine"},
            {"offset", f.at(0)},
            {"amplitude", f.at(1)},
            {"freq_hz", f.at(2)}};
  case 4:
    return {{"input", input},
            {"segments",
             {{{"kind", "pulse"}, {"low", f.at(0)}, {"high", f.at(1)}, {"period", f.at(2)},
               {"duty", f.at(3)}}}}};
  default:
    throw std::out_of_range("no preset " + std::to_string(kind));
  }
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
    snap_.late = num(st, "late");
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
        const auto limit = [&v](const char *key) {
          const auto it = v.find(key);
          return it != v.end() && it->is_number() ? it->get<double>() : double(NAN);
        };
        Variable var{str(v, "name"), kind};
        var.min = limit("min"); // the service does not send these yet
        var.max = limit("max");
        vars.push_back(var);
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

void Model::disconnect()
{
  post([this] {
    svc_.close();
    cap_id_ = -1;
    cap_from_ = 0;
    captured_.clear();
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto events = std::move(snap_.events);
      std::string address = std::move(snap_.address);
      snap_ = Snapshot{};
      snap_.address = std::move(address);
      snap_.events = std::move(events);
    }
    event("disconnected");
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

void Model::describe(std::string path)
{
  post([this, path] {
    json reply;
    try {
      reply = svc_.call("describe", {{ends_with(path, ".fmu") ? "fmu" : "scenario", path}});
    } catch (const ServiceError &e) {
      reply = {{"text", ""}, {"problem", e.what()}};
    }
    std::lock_guard<std::mutex> lock(mu_);
    snap_.described_path = path;
    snap_.described = std::move(reply);
    snap_.note.clear();
  });
}

void Model::note(const std::string &text, bool ok)
{
  {
    std::lock_guard<std::mutex> lock(mu_);
    snap_.note = text;
    snap_.note_ok = ok;
  }
  event(text);
}

// args: what `load` takes. A refusal is the form's note as well as an event.
void Model::do_load(const std::string &label, const json &args)
{
  setpoints_.clear(); // another scenario: start clean
  if (str(svc_.call("state"), "state") != "IDLE") {
    svc_.call("unload");
    event("unloaded the previous run");
  }
  try {
    svc_.call("load", args);
  } catch (const ServiceError &e) {
    note(std::string("not loaded: ") + e.what(), false);
    return;
  }
  cap_id_ = -1;
  {
    std::lock_guard<std::mutex> lock(mu_);
    snap_.loaded = label;
    snap_.vars.clear();
    snap_.history.clear();
  }
  note("loaded " + label, true);
}

void Model::load(std::string path)
{
  post([this, path] {
    if (ends_with(path, ".fmu")) {
      do_load(path, {{"toml", str(svc_.call("init", {{"fmu", path}}), "toml")}});
    } else {
      do_load(path, {{"scenario", path}});
    }
  });
}

void Model::load_text(std::string label, std::string toml)
{
  post([this, label, toml] { do_load(label + " (edited)", {{"toml", toml}}); });
}

void Model::check(std::string toml)
{
  post([this, toml] {
    try {
      svc_.call("check", {{"toml", toml}});
      note("check: ok, the runner takes it", true);
    } catch (const ServiceError &e) {
      note(std::string("check: ") + e.what(), false);
    }
  });
}

void Model::save(std::string path, std::string toml, bool overwrite)
{
  post([this, path, toml, overwrite] {
    try {
      svc_.call("save", {{"path", path}, {"toml", toml}, {"overwrite", overwrite}});
    } catch (const ServiceError &e) {
      note(std::string("not saved: ") + e.what(), false);
      return;
    }
    note("saved " + path, true);
    const json files = svc_.call("files");
    std::lock_guard<std::mutex> lock(mu_);
    snap_.scenarios = files.at("scenarios").get<std::vector<std::string>>();
  });
}

void Model::start()
{
  post([this] {
    const std::string st = str(svc_.call("state"), "state");
    if (st == "STOPPED" || st == "FAULT") {
      do_reset();
    }
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

void Model::write(std::string name, double value)
{
  post([this, name, value] {
    svc_.call("write", {{"values", {{name, value}}}});
    setpoints_[name] = {false, value};
    char line[160];
    std::snprintf(line, sizeof line, "%s = %.6g", name.c_str(), value);
    event(line);
  });
}

void Model::signal(json stimulus)
{
  post([this, stimulus] {
    svc_.call("signal", {{"stimulus", stimulus}});
    setpoints_[stimulus["input"].get<std::string>()] = {true, stimulus};
    json what = stimulus;
    what.erase("input");
    event(stimulus["input"].get<std::string>() + ": " + what.dump());
  });
}

void Model::do_reset()
{
  const json s = svc_.call("reset").at("summary");
  cap_id_ = -1;
  {
    std::lock_guard<std::mutex> lock(mu_);
    snap_.history.clear();
  }
  char line[160];
  std::snprintf(line, sizeof line, "reset: the run of %.0f steps ended, again from t = 0",
                num(s, "steps"));
  event(line);
  for (const auto &[name, sp] : setpoints_) { // keep what was set: the run starts with it again
    try {
      if (sp.first) {
        svc_.call("signal", {{"stimulus", sp.second}});
      } else {
        svc_.call("write", {{"values", {{name, sp.second}}}});
      }
    } catch (const ServiceError &e) {
      event("not kept: " + name + ": " + e.what());
    }
  }
}

void Model::reset()
{
  post([this] { do_reset(); });
}

void Model::unload()
{
  post([this] {
    const json s = svc_.call("unload").at("summary");
    setpoints_.clear();
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
