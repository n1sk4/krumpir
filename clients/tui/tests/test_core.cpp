// The service client and the model against a fake `krumpir serve` in this process: framing,
// errors, timeouts, and the model's polling (state, variables, history). Runs on every platform;
// the real service is covered by runner/tests/test_serve.py.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include "model.hpp"
#include "service.hpp"

using asio::ip::tcp;
using krumpir::json;
using namespace std::chrono_literals;

// Answers connections one at a time, line by line, with `reply(request)`; a null reply sends
// nothing. With `drop` set, the next request closes the connection instead (and clears it).
class FakeServer
{
public:
  std::atomic<int> accepted{0};
  std::atomic<bool> drop{false};

  explicit FakeServer(std::function<json(const json &)> reply)
      : acceptor_(io_, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)),
        thread_([this, reply] {
          for (;;) {
            std::error_code ec;
            tcp::socket sock(io_);
            acceptor_.accept(sock, ec);
            if (ec || done_) {
              break;
            }
            accepted++;
            asio::streambuf buf;
            while (!ec) {
              std::size_t n = asio::read_until(sock, buf, '\n', ec);
              if (ec || drop.exchange(false)) {
                break;
              }
              std::string line(asio::buffers_begin(buf.data()),
                               asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(n));
              buf.consume(n);
              json r = reply(json::parse(line));
              if (!r.is_null()) {
                asio::write(sock, asio::buffer(r.dump() + "\n"), ec);
              }
            }
          }
        })
  {
  }
  ~FakeServer()
  {
    done_ = true;
    std::error_code ec;
    tcp::socket wake(io_); // out of accept
    wake.connect(acceptor_.local_endpoint(), ec);
    thread_.join();
  }
  std::string address() const
  {
    return "127.0.0.1:" + std::to_string(acceptor_.local_endpoint().port());
  }

private:
  std::atomic<bool> done_{false};
  asio::io_context io_;
  tcp::acceptor acceptor_;
  std::thread thread_;
};

TEST_CASE("service: request, reply, refusal, closed connection")
{
  FakeServer srv([](const json &req) -> json {
    if (req["op"] == "info") {
      return {{"ok", true}, {"protocol", 1}, {"echo", req["x"]}};
    }
    return {{"ok", false}, {"error", "unknown op 'nope'"}};
  });
  krumpir::Service svc;
  svc.connect(srv.address());
  json r = svc.call("info", {{"x", 7}});
  CHECK(r["protocol"] == 1);
  CHECK(r["echo"] == 7);
  CHECK_THROWS_WITH_AS(svc.call("nope"), "unknown op 'nope'", krumpir::ServiceError);
  CHECK(svc.is_open()); // a refusal keeps the connection
  svc.close();
  CHECK_THROWS_AS(svc.call("info"), krumpir::LinkError);
}

TEST_CASE("service: a refusal's code; busy closes the link")
{
  FakeServer srv([](const json &req) -> json {
    if (req["op"] == "read") {
      return {{"ok", false}, {"code", "not_found"}, {"error", "'x' is not a variable"}};
    }
    if (req["op"] == "state") {
      return {{"ok", false}, {"error", "an older service"}};
    }
    return {{"ok", false}, {"code", "busy"}, {"error", "another client is connected"}};
  });
  krumpir::Service svc;
  svc.connect(srv.address());
  auto code = [&](const char *op) {
    try {
      svc.call(op);
    } catch (const krumpir::ServiceError &e) {
      return e.code;
    }
    return std::string("none");
  };
  CHECK(code("read") == "not_found");
  CHECK(code("state").empty());
  CHECK(svc.is_open());
  CHECK(code("info") == "busy");
  CHECK_FALSE(svc.is_open());
}

TEST_CASE("service: a reply that never comes times out and closes")
{
  FakeServer srv([](const json &) { return json(); });
  krumpir::Service svc(100ms);
  svc.connect(srv.address());
  CHECK_THROWS_AS(svc.call("state"), krumpir::LinkError);
  CHECK_FALSE(svc.is_open());
}

TEST_CASE("service: nobody listening")
{
  std::string address;
  {
    asio::io_context io;
    tcp::acceptor a(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    address = "127.0.0.1:" + std::to_string(a.local_endpoint().port());
  } // closed again: the port is free
  krumpir::Service svc(1s);
  CHECK_THROWS_AS(svc.connect(address), krumpir::LinkError);
}

// A session that loads, runs (t grows by 0.1 per state request), and reads x = 2 t. With
// `capture`, x is also recorded by a capture (every "step"), u is not.
static void watch(bool capture)
{
  std::string state = "IDLE";
  double t = 0;
  json rows = json::array();
  FakeServer srv([&](const json &req) -> json {
    const std::string op = req["op"];
    json ok = {{"ok", true}};
    if (op == "files") {
      ok["scenarios"] = {"scenarios/a.toml"};
      ok["fmus"] = {"fmus/a.fmu"};
    } else if (op == "state") {
      if (state == "RUNNING") {
        t += 0.1;
        rows.push_back({t - 0.05, -1.0}); // two capture rows per poll
        rows.push_back({t, -1.0});
      }
      ok["state"] = state;
      ok["fault"] = "";
      if (state != "IDLE") {
        ok["t"] = t;
        ok["step"] = 0.001;
      }
    } else if (op == "load") {
      CHECK(req["scenario"] == "scenarios/a.toml");
      state = "INITIALIZED";
    } else if (op == "start") {
      state = "RUNNING";
    } else if (op == "reset") {
      CHECK((state == "STOPPED" || state == "FAULT"));
      state = "INITIALIZED";
      t = 0;
      ok["summary"] = {{"steps", 42}};
    } else if (op == "capture_start" && capture) {
      ok["id"] = 0;
    } else if (op == "capture_fetch") {
      const std::size_t from = req["from"];
      ok["capture_state"] = "RUNNING";
      ok["names"] = {"t", "x"};
      ok["rows"] = json(rows.begin() + static_cast<std::ptrdiff_t>(from), rows.end());
    } else if (op == "variables") {
      ok["variables"] = {
          {{"name", "x"},
           {"type", "Float64"},
           {"causality", "output"},
           {"tunable", false},
           {"min", -1.5},
           {"max", nullptr}},
          {{"name", "p"}, {"type", "Float64"}, {"causality", "parameter"}, {"tunable", true}},
          {{"name", "k"}, {"type", "Float64"}, {"causality", "parameter"}, {"tunable", false}},
          {{"name", "u"}, {"type", "Float64"}, {"causality", "input"}, {"tunable", false}},
          {{"name", "n"}, {"type", "Int32"}, {"causality", "input"}, {"tunable", false}}};
    } else if (op == "read") {
      ok["values"] = json::object();
      for (const auto &n : req["names"]) {
        ok["values"][n.get<std::string>()] = n == "x" ? 2 * t : 1.0;
      }
    } else {
      return {{"ok", false}, {"error", "unexpected " + op}};
    }
    return ok;
  });

  std::atomic<int> changes{0};
  krumpir::Model m([&] { changes++; }, 10ms);
  auto until = [&](const std::function<bool(const krumpir::Snapshot &)> &cond) {
    for (int i = 0; i < 300; i++) {
      if (m.view(cond)) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  };

  m.connect(srv.address());
  REQUIRE(until([](const krumpir::Snapshot &v) { return v.connected && !v.fmus.empty(); }));
  m.load("scenarios/a.toml");
  m.start();
  REQUIRE(until([](const krumpir::Snapshot &v) {
    const auto it = v.history.find("x");
    return it != v.history.end() && it->second.size() >= 3;
  }));
  m.view([&](const krumpir::Snapshot &v) {
    REQUIRE(v.vars.size() == 3); // u, x, p: Float64 inputs, outputs, tunable parameters
    CHECK(v.vars[0].name == "u");
    CHECK(v.vars[1].name == "x");
    CHECK(v.vars[1].min == -1.5);
    CHECK(std::isnan(v.vars[1].max)); // null: no limit
    CHECK(std::isnan(v.vars[0].min)); // absent: no limit
    CHECK(v.vars[2].kind == "param");
    const auto &h = v.history.at("x");
    for (std::size_t i = 1; i < h.size(); i++) {
      CHECK(h[i].t > h[i - 1].t);                                    // one sample per new time
      CHECK(h[i].v == doctest::Approx(capture ? -1.0 : 2 * h[i].t)); // capture rows, or polled
    }
    CHECK(v.history.at("u").size() >= 1); // not in the capture: polled either way
    CHECK(v.loaded == "scenarios/a.toml");
    bool loaded = false, running = false;
    for (const auto &e : v.events) {
      loaded |= e.find("loaded scenarios/a.toml") != std::string::npos;
      running |= e.find("-> RUNNING") != std::string::npos;
    }
    CHECK(loaded);
    CHECK(running);
    return 0;
  });
  CHECK(changes > 0);
}

TEST_CASE("model: disconnect closes the link and clears what was shown")
{
  FakeServer srv([](const json &) -> json {
    return {{"ok", true},
            {"state", "IDLE"},
            {"fault", ""},
            {"scenarios", {"scenarios/a.toml"}},
            {"fmus", json::array()}};
  });
  krumpir::Model m([] {}, 10ms);
  auto until = [&](const std::function<bool(const krumpir::Snapshot &)> &cond) {
    for (int i = 0; i < 300; i++) {
      if (m.view(cond)) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  };
  m.connect(srv.address());
  REQUIRE(until([](const krumpir::Snapshot &v) { return v.connected && !v.scenarios.empty(); }));
  m.disconnect();
  REQUIRE(until([](const krumpir::Snapshot &v) { return !v.connected; }));
  m.view([&](const krumpir::Snapshot &v) {
    CHECK(v.scenarios.empty());
    CHECK(v.address == srv.address()); // kept, to connect again
    CHECK(v.events.back().find("disconnected") != std::string::npos);
    return 0;
  });
}

TEST_CASE("model: a service busy with another client says so")
{
  FakeServer srv([](const json &) -> json {
    return {{"ok", false}, {"code", "busy"}, {"error", "another client is connected"}};
  });
  krumpir::Model m([] {}, 10ms);
  m.connect(srv.address());
  bool told = false;
  for (int i = 0; i < 300 && !told; i++) {
    std::this_thread::sleep_for(10ms);
    told = m.view([](const krumpir::Snapshot &v) {
      return !v.events.empty() && v.events.back().find("cannot connect to") != std::string::npos &&
             v.events.back().find("another client is connected") != std::string::npos;
    });
  }
  CHECK(told);
  CHECK_FALSE(m.view([](const krumpir::Snapshot &v) { return v.connected; }));
}

TEST_CASE("model: a second start frees the first capture; a refusal of that is ignored")
{
  std::vector<json> seen;
  std::mutex mu;
  std::string state = "INITIALIZED";
  int next_id = 0;
  FakeServer srv([&](const json &req) -> json {
    std::lock_guard<std::mutex> lock(mu);
    seen.push_back(req);
    const std::string op = req["op"];
    json ok = {{"ok", true},
               {"fault", ""},
               {"scenarios", json::array()},
               {"fmus", json::array()},
               {"variables", json::array()},
               {"values", json::object()}};
    if (op == "start") {
      if (state != "INITIALIZED") {
        return {{"ok", false}, {"code", "bad_state"}, {"error", "cannot start in " + state}};
      }
      state = "RUNNING";
    } else if (op == "capture_start") {
      ok["id"] = next_id++;
    } else if (op == "capture_fetch") {
      ok["capture_state"] = "RUNNING";
      ok["names"] = {"t"};
      ok["rows"] = json::array();
    } else if (op == "capture_free") {
      return {{"ok", false}, {"error", "unknown op 'capture_free'"}}; // an older runner
    }
    ok["state"] = state;
    return ok;
  });
  auto starts = [&] {
    std::lock_guard<std::mutex> lock(mu);
    return std::count_if(seen.begin(), seen.end(),
                         [](const json &r) { return r["op"] == "start"; });
  };
  {
    krumpir::Model m([] {}, 10ms);
    m.connect(srv.address());
    m.start();
    m.start();
    for (int i = 0; i < 300 && starts() < 2; i++) {
      std::this_thread::sleep_for(10ms);
    }
  }
  std::lock_guard<std::mutex> lock(mu);
  std::vector<std::string> ops;
  for (const auto &r : seen) {
    const std::string op = r["op"];
    if (op == "capture_start" || op == "capture_free" || op == "start") {
      ops.push_back(op);
    }
    if (op == "capture_free") {
      CHECK(r["id"] == 0);
    }
  }
  CHECK(ops == std::vector<std::string>{"capture_start", "start", "capture_free", "capture_start",
                                        "start"});
}

TEST_CASE("presets: every kind builds the stimulus the service takes")
{
  using krumpir::preset;
  REQUIRE(krumpir::presets().size() == 5);
  for (std::size_t k = 0; k < krumpir::presets().size(); k++) {
    const auto &p = krumpir::presets()[k];
    CHECK(p.fields.size() == p.defaults.size());
    CHECK(preset("u", k, std::vector<double>(p.fields.size(), 1.0))["input"] == "u");
  }
  CHECK(preset("u", 0, {2}) == json{{"input", "u"}, {"kind", "constant"}, {"value", 2.0}});
  json step = preset("u", 1, {0.1, 0.5, 0.8}); // from 0.1, after 0.5 s to 0.8
  CHECK(step["segments"][0] == json{{"kind", "const"}, {"value", 0.1}, {"duration", 0.5}});
  CHECK(step["segments"][1] == json{{"kind", "const"}, {"value", 0.8}});
  CHECK(preset("u", 1, {0.1, 0, 0.8})["kind"] == "constant"); // no delay: at once
  CHECK(preset("u", 2, {0, 1, 2})["segments"][0]["duration"] == 2.0);
  CHECK(preset("u", 3, {0, 1, 5})["freq_hz"] == 5.0);
  CHECK(preset("u", 4, {0, 1, 0.1, 0.25})["segments"][0]["duty"] == 0.25);
  CHECK_THROWS(preset("u", 5, {}));
  CHECK_THROWS(preset("u", 1, {1})); // too few numbers
}

TEST_CASE("model: write and signal send what the service expects")
{
  std::vector<json> seen;
  std::mutex mu;
  FakeServer srv([&](const json &req) -> json {
    std::lock_guard<std::mutex> lock(mu);
    seen.push_back(req);
    json ok = {{"ok", true}, {"state", "IDLE"}, {"fault", ""}};
    ok["scenarios"] = json::array();
    ok["fmus"] = json::array();
    return ok;
  });
  auto got_signal = [&] {
    std::lock_guard<std::mutex> lock(mu);
    return std::any_of(seen.begin(), seen.end(), [](const json &r) { return r["op"] == "signal"; });
  };
  {
    krumpir::Model m([] {}, 10ms);
    m.connect(srv.address());
    m.write("duty", 0.25);
    m.signal(krumpir::preset("duty", 0, {0.5}));
    for (int i = 0; i < 300 && !got_signal(); i++) {
      std::this_thread::sleep_for(10ms);
    }
  }
  std::lock_guard<std::mutex> lock(mu);
  bool wrote = false, signalled = false;
  for (const auto &r : seen) {
    wrote |= r["op"] == "write" && r["values"] == json{{"duty", 0.25}};
    signalled |= r["op"] == "signal" && r["stimulus"]["kind"] == "constant";
  }
  CHECK(wrote);
  CHECK(signalled);
}

TEST_CASE("model: start on a stopped run resets it first")
{
  std::vector<std::string> ops;
  std::mutex mu;
  std::string state = "STOPPED";
  FakeServer srv([&](const json &req) -> json {
    std::lock_guard<std::mutex> lock(mu);
    const std::string op = req["op"];
    ops.push_back(op);
    json ok = {{"ok", true},
               {"fault", ""},
               {"scenarios", json::array()},
               {"fmus", json::array()},
               {"variables", json::array()},
               {"values", json::object()}};
    if (op == "reset") {
      if (state != "STOPPED") {
        return {{"ok", false}, {"error", "cannot reset in state " + state}};
      }
      state = "INITIALIZED";
      ok["summary"] = {{"steps", 7}};
    } else if (op == "start") {
      state = "RUNNING";
    } else if (op == "capture_start") {
      ok["id"] = 0;
    } else if (op == "capture_fetch") {
      ok["capture_state"] = "RUNNING";
      ok["names"] = {"t"};
      ok["rows"] = json::array();
    }
    ok["state"] = state;
    return ok;
  });
  auto has = [&](const char *op) {
    std::lock_guard<std::mutex> lock(mu);
    return std::find(ops.begin(), ops.end(), op) != ops.end();
  };
  {
    krumpir::Model m([] {}, 10ms);
    m.connect(srv.address());
    m.start();
    for (int i = 0; i < 300 && !has("start"); i++) {
      std::this_thread::sleep_for(10ms);
    }
    CHECK(m.view([](const krumpir::Snapshot &v) {
      return std::any_of(v.events.begin(), v.events.end(), [](const std::string &e) {
        return e.find("reset: the run of 7 steps") != std::string::npos;
      });
    }));
  }
  std::lock_guard<std::mutex> lock(mu);
  const auto reset = std::find(ops.begin(), ops.end(), "reset");
  const auto start = std::find(ops.begin(), ops.end(), "start");
  REQUIRE(reset != ops.end());
  CHECK(reset < start);
}

TEST_CASE("model: connect, load, run, watch by polling") { watch(false); }
TEST_CASE("model: connect, load, run, watch by capture") { watch(true); }

// A running session with a capture (x, rows of -1; polled x = 2 t) whose connection is dropped.
// After the drop, `busy` attempts are refused as busy; `restarted`: the capture is gone, as on a
// runner that was started again.
static void lost_link(int busy, bool restarted)
{
  std::mutex mu;
  std::vector<json> seen;
  std::string state = "INITIALIZED";
  double t = 0;
  json rows = json::array();
  std::size_t fetched = 0, seen_at_drop = 0, from_after = 0, fetched_before = 0;
  int connections = 0; // each starts with `files`
  bool dropped = false, fetched_after = false;
  const int refusals = busy;
  FakeServer srv([&](const json &req) -> json {
    std::lock_guard<std::mutex> lock(mu);
    seen.push_back(req);
    const std::string op = req["op"];
    json ok = {{"ok", true}, {"fault", ""}, {"scenarios", json::array()}, {"fmus", json::array()}};
    if (op == "files") {
      dropped = ++connections > 1;
    }
    if (op == "files" && dropped && busy > 0) {
      busy--;
      return {{"ok", false}, {"code", "busy"}, {"error", "another client is connected"}};
    } else if (op == "state") {
      if (state == "RUNNING") {
        t += 0.1;
        rows.push_back({t, -1.0});
      }
      ok["state"] = state;
      ok["t"] = t;
      ok["step"] = 0.001;
    } else if (op == "start") {
      state = "RUNNING";
    } else if (op == "capture_start") {
      ok["id"] = 0;
    } else if (op == "capture_fetch") {
      if (dropped && restarted) {
        return {{"ok", false}, {"code", "not_found"}, {"error", "no capture 0"}};
      }
      const std::size_t from = req["from"];
      if (dropped && !fetched_after) {
        fetched_after = true;
        from_after = from;
        fetched_before = fetched;
      }
      ok["capture_state"] = "RUNNING";
      ok["names"] = {"t", "x"};
      ok["rows"] = json(rows.begin() + static_cast<std::ptrdiff_t>(from), rows.end());
      fetched = rows.size();
    } else if (op == "variables") {
      ok["variables"] = {{{"name", "x"}, {"type", "Float64"}, {"causality", "output"}},
                         {{"name", "u"}, {"type", "Float64"}, {"causality", "input"}}};
    } else if (op == "read") {
      ok["values"] = {{"x", 2 * t}, {"u", 0.5}};
    }
    return ok;
  });

  krumpir::Model m([] {}, 10ms, 50ms);
  auto until = [&](const std::function<bool(const krumpir::Snapshot &)> &cond) {
    for (int i = 0; i < 300; i++) {
      if (m.view(cond)) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  };
  auto x_size = [](const krumpir::Snapshot &v) {
    const auto it = v.history.find("x");
    return it == v.history.end() ? std::size_t(0) : it->second.size();
  };
  auto has = [](const krumpir::Snapshot &v, const char *what) {
    return std::count_if(v.events.begin(), v.events.end(), [what](const std::string &e) {
      return e.find(what) != std::string::npos;
    });
  };

  m.connect(srv.address());
  REQUIRE(until([](const krumpir::Snapshot &v) { return v.connected; }));
  m.write("u", 0.5); // a setpoint: replayed after a reset, never after a reconnect
  m.start();
  REQUIRE(until([&](const krumpir::Snapshot &v) { return x_size(v) >= 4; }));
  std::size_t before;
  const double first =
      m.view([](const krumpir::Snapshot &v) { return v.history.at("x").front().t; });
  {
    std::lock_guard<std::mutex> lock(mu);
    before = m.view(x_size);
    seen_at_drop = seen.size();
    srv.drop = true;
  }
  REQUIRE(until([&](const krumpir::Snapshot &v) { return has(v, "reconnected to") == 1; }));
  REQUIRE(until([&](const krumpir::Snapshot &v) { return x_size(v) > before + 2; }));
  m.view([&](const krumpir::Snapshot &v) {
    CHECK(v.connected);
    CHECK_FALSE(v.reconnecting);
    CHECK(v.state == "RUNNING");
    CHECK(has(v, "disconnected: ") == 1);
    CHECK(has(v, ", reconnecting") == 1);
    CHECK(has(v, "cannot connect") == 0);
    const auto &h = v.history.at("x");
    CHECK(h.front().t == first); // the history is kept
    for (std::size_t i = 1; i < h.size(); i++) {
      CHECK(h[i].t > h[i - 1].t);
    }
    CHECK(h.back().v == doctest::Approx(restarted ? 2 * h.back().t : -1.0));
    CHECK(has(v, "capture lost") == (restarted ? 1 : 0));
    return 0;
  });
  CHECK(srv.accepted >= 2 + refusals);
  std::lock_guard<std::mutex> lock(mu);
  CHECK(busy == 0);
  if (!restarted) {
    CHECK(fetched_after);
    CHECK(from_after == fetched_before); // where it left off: no rows twice, none missed
  }
  for (std::size_t i = seen_at_drop; i < seen.size(); i++) {
    const std::string op = seen[i]["op"];
    CHECK(op != "start");
    CHECK(op != "write");
    CHECK(op != "signal");
    CHECK(op != "capture_start");
    CHECK(op != "load");
  }
}

TEST_CASE("model: a lost link is tried again; the capture goes on, nothing is sent again")
{
  lost_link(0, false);
}
TEST_CASE("model: a reconnect refused as busy is tried again") { lost_link(3, false); }
TEST_CASE("model: after a reconnect a capture the runner forgot is dropped, polling goes on")
{
  lost_link(0, true);
}

TEST_CASE("model: after Disconnect a lost link is not tried again")
{
  FakeServer srv([](const json &) -> json {
    return {{"ok", true},
            {"state", "IDLE"},
            {"fault", ""},
            {"scenarios", json::array()},
            {"fmus", json::array()}};
  });
  krumpir::Model m([] {}, 10ms, 20ms);
  m.connect(srv.address());
  for (int i = 0; i < 300 && !m.view([](const krumpir::Snapshot &v) { return v.connected; }); i++) {
    std::this_thread::sleep_for(10ms);
  }
  m.disconnect();
  std::this_thread::sleep_for(300ms);
  CHECK(srv.accepted == 1);
  m.view([](const krumpir::Snapshot &v) {
    CHECK_FALSE(v.connected);
    CHECK_FALSE(v.reconnecting);
    return 0;
  });
}

TEST_CASE("csv: series merged by t, empty cells, quoted names, exact numbers")
{
  using krumpir::Sample;
  std::ostringstream out;
  const std::vector<std::vector<Sample>> series{
      {{0, 1}, {0.5, NAN}, {1, 0.1}},
      {{0.25, 2}, {1, INFINITY}, {2, -3}},
  };
  CHECK(krumpir::write_csv(out, {"a", "b,\"x\""}, series) == 5);
  CHECK(out.str() == "t,a,\"b,\"\"x\"\"\"\n"
                     "0,1,\n"
                     "0.25,,2\n"
                     "0.5,,\n"
                     "1,0.10000000000000001,\n"
                     "2,,-3\n");

  std::ostringstream one;
  const double v = 1.0 / 3, t = 0.1 + 0.2;
  CHECK(krumpir::write_csv(one, {"v"}, {{{t, v}}}) == 1);
  const std::string row = one.str().substr(one.str().find('\n') + 1);
  char *end;
  CHECK(std::strtod(row.c_str(), &end) == t);
  CHECK(std::strtod(end + 1, nullptr) == v);

  std::ostringstream none;
  CHECK(krumpir::write_csv(none, {}, {}) == 0);
  CHECK(none.str() == "t\n");
}

TEST_CASE("csv: the window of a history to a file, never written over")
{
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "krumpir_test_csv";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::mutex mu;
  std::map<std::string, std::deque<krumpir::Sample>> h;
  for (int i = 0; i < 10; i++) { // exact in binary: the times compare equal
    h["x"].push_back({i * 0.25, double(i)});
    if (i % 2 == 0) {
      h["y"].push_back({i * 0.25 + 0.125, -double(i)});
    }
  }
  const auto file = [](const std::string &event) {
    const auto at = event.rfind(" to ");
    return at == std::string::npos ? std::string() : event.substr(at + 4);
  };
  const auto read = [](const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
  };

  const std::string e1 = krumpir::write_csv_file(mu, h, {"x", "y"}, 0.5, 1, "runner", dir.string());
  CHECK(e1.rfind("exported 4 rows of 2 signals, t 0.5 .. 1 s, to ", 0) == 0);
  const std::string p1 = file(e1);
  CHECK(fs::path(p1).is_absolute());
  CHECK(fs::path(p1).filename().string().rfind("krumpir-runner-", 0) == 0);
  const std::string want = "t,x,y\n0.5,2,\n0.625,,-2\n0.75,3,\n1,4,\n";
  CHECK(read(p1) == want);
  const std::string e2 = krumpir::write_csv_file(mu, h, {"x"}, NAN, NAN, "runner", dir.string());
  CHECK(e2.rfind("exported 10 rows of 1 signals, t 0 .. 2.25 s, to ", 0) == 0);
  const std::string p2 = file(e2);
  const std::string p3 =
      file(krumpir::write_csv_file(mu, h, {"x"}, NAN, NAN, "runner", dir.string()));
  CHECK(p2 != p1);
  CHECK(p3 != p1);
  CHECK(p3 != p2);
  // Two of the three share a second: the later one got the next free name
  const auto suffixed = [](const std::string &p) { return krumpir::ends_with(p, "-2.csv"); };
  CHECK((suffixed(p2) || suffixed(p3)));
  CHECK(read(p1) == want); // the first file is as it was

  CHECK(krumpir::write_csv_file(mu, h, {"x"}, 5, 6, "runner", dir.string()) == "nothing to export");
  CHECK(krumpir::write_csv_file(mu, h, {"z"}, NAN, NAN, "runner", dir.string()) ==
        "nothing to export");
  const std::string bad =
      krumpir::write_csv_file(mu, h, {"x"}, NAN, NAN, "runner", (dir / "missing").string());
  CHECK(bad.rfind("export failed: ", 0) == 0);
  int n = 0;
  for (const auto &f : fs::directory_iterator(dir)) {
    n += f.path().extension() == ".csv";
  }
  CHECK(n == 3);
  fs::remove_all(dir);
}

TEST_CASE("model: export needs no link; nothing plotted writes nothing")
{
  krumpir::Model m([] {}, 10ms);
  m.export_csv({"x"}, NAN, NAN);
  bool told = false;
  for (int i = 0; i < 300 && !told; i++) {
    std::this_thread::sleep_for(10ms);
    told = m.view([](const krumpir::Snapshot &v) {
      return !v.events.empty() && v.events.back().find("nothing to export") != std::string::npos;
    });
  }
  CHECK(told);
}
