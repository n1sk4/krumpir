#include "ecu_model.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace krumpir {

namespace fs = std::filesystem;

namespace {

std::string str(const json &j, const char *key)
{
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

double num(const json &j, const char *key)
{
  const auto it = j.find(key);
  return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

} // namespace

std::vector<Controller> find_controllers(const std::string &root)
{
  std::vector<Controller> out;
  std::error_code ec;
  for (const auto &d : fs::directory_iterator(fs::path(root) / "controllers", ec)) {
    const fs::path file = d.path() / "src" / "gen" / "ctrl_gen.json";
    std::ifstream f(file);
    if (!f) {
      continue;
    }
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
      continue;
    }
    out.push_back({d.path().generic_string(), str(j, "name"),
                   (d.path() / str(j, "a2l")).generic_string(), std::move(j)});
  }
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.dir < b.dir; });
  return out;
}

EcuModel::EcuModel(std::function<void()> on_change, std::chrono::milliseconds period,
                   std::chrono::milliseconds timeout)
    : ecu_(timeout), on_change_(std::move(on_change)), period_(period),
      thread_([this] { worker(); })
{
}

EcuModel::~EcuModel()
{
  {
    std::lock_guard<std::mutex> lock(qmu_);
    quit_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void EcuModel::post(std::function<void()> job)
{
  {
    std::lock_guard<std::mutex> lock(qmu_);
    jobs_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void EcuModel::event(const std::string &text)
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

// A refused command or a bad value leaves the link as it is; a lost link marks it disconnected.
void EcuModel::guarded(const std::function<void()> &job)
{
  try {
    job();
  } catch (const XcpError &e) {
    event(std::string("error: ") + e.what());
  } catch (const A2lError &e) {
    event(std::string("error: ") + e.what());
  } catch (const json::exception &e) {
    event(std::string("bad controller description: ") + e.what());
  } catch (const LinkError &e) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.connected = false;
    }
    event(std::string("disconnected: ") + e.what());
  }
}

void EcuModel::worker()
{
  std::unique_lock<std::mutex> lock(qmu_);
  while (!quit_) {
    if (ecu_.daq_running()) { // waits on the socket instead: commands wait up to one period
      lock.unlock();
      guarded([this] { take_daq(); });
      lock.lock();
    } else {
      cv_.wait_for(lock, period_, [this] { return quit_ || !jobs_.empty(); });
    }
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
    if (ecu_.is_open()) {
      guarded([this] { poll(); });
    }
    on_change_();
    lock.lock();
  }
  lock.unlock();
  ecu_.close();
}

// Status, calibration values, then the watched signals: one read each.
void EcuModel::poll()
{
  const json status = desc_.value("status", json::object());
  std::vector<std::string> watched, cals;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &w : snap_.watched) { // those DAQ measures come in by take_daq
      if (std::find(daq_names_.begin(), daq_names_.end(), w) == daq_names_.end()) {
        watched.push_back(w);
      }
    }
    for (const auto &p : snap_.params) {
      cals.push_back(p.cal);
    }
  }
  auto read1 = [this](const std::string &name) {
    const auto it = a2l_.symbols.find(name);
    return it == a2l_.symbols.end() ? NAN : ecu_.read(it->second).at(0);
  };

  const auto t_start = std::chrono::steady_clock::now();
  const double running = read1(str(status, "running"));
  const double rtt =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count();
  const std::string trip_var = str(status, "trip");
  const double trip = read1(trip_var);
  std::vector<double> cal_values, values;
  for (const auto &c : cals) {
    cal_values.push_back(c.empty() ? NAN : read1(c));
  }
  for (const auto &w : watched) {
    values.push_back(read1(w));
  }
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();

  const long long tr = std::isfinite(trip) ? static_cast<long long>(trip) : 0;
  std::string trip_name;
  if (tr != 0) {
    const auto sym = a2l_.symbols.find(trip_var);
    const auto *names = sym != a2l_.symbols.end() ? &sym->second.names : nullptr;
    trip_name = names && names->count(tr) ? names->at(tr) : "trip " + std::to_string(tr);
  }
  bool changed;
  {
    std::lock_guard<std::mutex> lock(mu_);
    changed = tr != snap_.trip;
    snap_.rtt_ms = rtt;
    snap_.running = running > 0;
    snap_.trip = tr;
    snap_.trip_name = trip_name;
    for (std::size_t k = 0; k < snap_.params.size() && k < cal_values.size(); k++) {
      snap_.params[k].value = cal_values[k];
    }
    for (std::size_t k = 0; k < watched.size(); k++) {
      snap_.values[watched[k]] = values[k];
      auto &h = snap_.history[watched[k]];
      h.push_back({t, values[k]});
      if (h.size() > kHistory) {
        h.pop_front();
      }
    }
  }
  if (changed) {
    event(tr != 0 ? "tripped: " + trip_name : "trip cleared");
  }
}

void EcuModel::restart_daq()
{
  std::vector<std::string> names;
  std::vector<Symbol> signals;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &w : snap_.watched) {
      const auto it = a2l_.symbols.find(w);
      if (it != a2l_.symbols.end() && it->second.event >= 0 && it->second.count == 1) {
        names.push_back(w);
        signals.push_back(it->second);
      }
    }
  }
  ecu_.daq_stop();
  daq_names_.clear();
  if (!signals.empty()) {
    try {
      ecu_.daq_start(signals);
      daq_names_ = names;
      daq_t0_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    } catch (const XcpError &e) {
      event(std::string("DAQ refused, polling instead: ") + e.what());
    }
  }
  std::lock_guard<std::mutex> lock(mu_);
  snap_.daq_signals = static_cast<int>(daq_names_.size());
  snap_.daq_rate = 0;
  snap_.daq_lost = 0;
}

void EcuModel::take_daq()
{
  const auto start = std::chrono::steady_clock::now();
  const auto samples = ecu_.daq_take(period_);
  const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto &s : samples) {
    const std::string &name = daq_names_.at(s.signal);
    snap_.values[name] = s.v;
    auto &h = snap_.history[name];
    h.push_back({daq_t0_ + s.t, s.v});
    if (h.size() > kHistory) {
      h.pop_front();
    }
  }
  if (dt > 0 && !daq_names_.empty()) {
    const double rate = static_cast<double>(samples.size()) / daq_names_.size() / dt;
    snap_.daq_rate += 0.2 * (rate - snap_.daq_rate); // smoothed
  }
  snap_.daq_lost = ecu_.daq_lost();
}

void EcuModel::scan(std::string root)
{
  post([this, root] {
    auto list = find_controllers(root);
    const std::size_t n = list.size();
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.controllers = std::move(list);
    }
    event("found " + std::to_string(n) + " controller(s) in " + root);
  });
}

void EcuModel::open(std::string dir)
{
  post([this, dir] {
    Controller c;
    {
      std::lock_guard<std::mutex> lock(mu_);
      const auto it = std::find_if(snap_.controllers.begin(), snap_.controllers.end(),
                                   [&](const Controller &x) { return x.dir == dir; });
      if (it == snap_.controllers.end()) {
        throw A2lError("no controller " + dir);
      }
      c = *it;
    }
    ecu_.close();
    daq_names_.clear();
    desc_ = c.desc;
    a2l_ = A2l{};
    std::string error;
    try {
      a2l_ = load_a2l(c.a2l);
    } catch (const A2lError &e) {
      error = std::string(e.what()) +
              " (build with -e esp32dev-xcp and generate it, see the controller's README)";
    }
    std::vector<std::string> names;
    for (const auto &[name, s] : a2l_.symbols) {
      if (s.count == 1) {
        names.push_back(name);
      }
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto events = std::move(snap_.events);
      auto controllers = std::move(snap_.controllers);
      snap_ = EcuSnapshot{};
      snap_.controllers = std::move(controllers);
      snap_.events = std::move(events);
      snap_.selected = dir;
      for (const auto &p : c.desc.value("params", json::array())) {
        snap_.params.push_back({str(p, "name"), str(p, "cal"), str(p, "unit"), str(p, "help"),
                                num(p, "min"), num(p, "max"), num(p, "default")});
      }
      snap_.monitor = c.desc.value("monitor", std::vector<std::string>{});
      snap_.a2l_error = error;
      snap_.epk = a2l_.epk;
      snap_.measurements = std::move(names);
      snap_.address = a2l_.host.empty() ? "" : a2l_.host + ":" + std::to_string(a2l_.port);
    }
    event(error.empty() ? "opened " + c.name : "opened " + c.name + ": " + error);
  });
}

void EcuModel::connect(std::string address)
{
  post([this, address] {
    if (a2l_.symbols.empty()) {
      throw A2lError("no A2L loaded");
    }
    const std::string to = address.empty() ? a2l_.host + ":" + std::to_string(a2l_.port) : address;
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.address = to;
      snap_.connected = false;
      snap_.history.clear();
    }
    ecu_.connect(to, a2l_);
    t0_ = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.connected = true;
      if (snap_.watched.empty()) {
        for (const auto &m : snap_.monitor) {
          if (a2l_.symbols.count(m)) {
            snap_.watched.push_back(m);
          }
        }
      }
    }
    event("connected to " + to + (a2l_.epk.empty() ? "" : ", EPK " + a2l_.epk));
    restart_daq();
  });
}

void EcuModel::disconnect()
{
  post([this] {
    ecu_.close();
    daq_names_.clear();
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.connected = false;
      snap_.daq_signals = 0;
    }
    event("disconnected");
  });
}

void EcuModel::write(std::string param, double value)
{
  post([this, param, value] {
    CalParam p;
    {
      std::lock_guard<std::mutex> lock(mu_);
      const auto it = std::find_if(snap_.params.begin(), snap_.params.end(),
                                   [&](const CalParam &x) { return x.name == param; });
      if (it == snap_.params.end()) {
        throw XcpError("no parameter '" + param + "'");
      }
      p = *it;
    }
    if (p.cal.empty()) {
      throw XcpError("'" + param + "' has no calibration value (console only)");
    }
    if (!(value >= p.min && value <= p.max)) {
      char why[160];
      std::snprintf(why, sizeof why, "%s: %g is outside %g..%g", param.c_str(), value, p.min,
                    p.max);
      throw XcpError(why);
    }
    ecu_.write(a2l_.at(p.cal), {value});
    char line[160];
    std::snprintf(line, sizeof line, "%s = %g", param.c_str(), value);
    event(line);
  });
}

void EcuModel::request(std::string command)
{
  post([this, command] {
    const json req = desc_.value("request", json());
    if (!req.is_object() || !req.contains(command) || !req[command].is_number_unsigned()) {
      throw XcpError("this controller takes no '" + command + "' request");
    }
    const Symbol &s = a2l_.at(str(req, "name"));
    // The next sequence number after the page's, so the same command can follow itself.
    const auto cur = static_cast<std::uint32_t>(ecu_.read(s).at(0));
    const std::uint32_t next =
        ((((cur >> 8) + 1) << 8) | (req[command].get<std::uint32_t>() & 0xFFu)) & 0xFFFFFFFFu;
    ecu_.write(s, {static_cast<double>(next)});
    event(command + " requested");
  });
}

void EcuModel::watch(std::vector<std::string> names)
{
  post([this, names] {
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_.watched = names;
      for (auto it = snap_.values.begin(); it != snap_.values.end();) {
        it = std::find(names.begin(), names.end(), it->first) == names.end()
                 ? snap_.values.erase(it)
                 : std::next(it);
      }
    }
    if (ecu_.is_open()) {
      restart_daq();
    }
  });
}

} // namespace krumpir
