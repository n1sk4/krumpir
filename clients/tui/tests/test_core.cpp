// The service client and the model against a fake `krumpir serve` in this process: framing,
// errors, timeouts, and the model's polling (state, variables, history). Runs on every platform;
// the real service is covered by runner/tests/test_serve.py.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "model.hpp"
#include "service.hpp"

using krumpir::json;
using asio::ip::tcp;
using namespace std::chrono_literals;

// Answers one connection, line by line, with `reply(request)`; a null reply sends nothing.
class FakeServer {
public:
  explicit FakeServer(std::function<json(const json &)> reply)
      : acceptor_(io_, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)),
        thread_([this, reply] {
          std::error_code ec;
          tcp::socket sock(io_);
          acceptor_.accept(sock, ec);
          asio::streambuf buf;
          while (!ec) {
            std::size_t n = asio::read_until(sock, buf, '\n', ec);
            if (ec) {
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
        })
  {
  }
  ~FakeServer() { thread_.join(); }
  std::string address() const
  {
    return "127.0.0.1:" + std::to_string(acceptor_.local_endpoint().port());
  }

private:
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
    } else if (op == "capture_start" && capture) {
      ok["id"] = 0;
    } else if (op == "capture_fetch") {
      const std::size_t from = req["from"];
      ok["capture_state"] = "RUNNING";
      ok["names"] = {"t", "x"};
      ok["rows"] = json(rows.begin() + static_cast<std::ptrdiff_t>(from), rows.end());
    } else if (op == "variables") {
      ok["variables"] = {
          {{"name", "x"}, {"type", "Float64"}, {"causality", "output"}, {"tunable", false}},
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
    CHECK(v.vars[2].kind == "param");
    const auto &h = v.history.at("x");
    for (std::size_t i = 1; i < h.size(); i++) {
      CHECK(h[i].t > h[i - 1].t); // one sample per new time
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

TEST_CASE("model: connect, load, run, watch by polling") { watch(false); }
TEST_CASE("model: connect, load, run, watch by capture") { watch(true); }
