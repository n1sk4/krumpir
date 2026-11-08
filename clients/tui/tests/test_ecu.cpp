// A2L parsing, the XCP master against a fake ECU on localhost, and the controller model.
#include <doctest/doctest.h>

#include <asio.hpp>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

#include "a2l.hpp"
#include "ecu_model.hpp"
#include "xcp.hpp"

using namespace krumpir;
namespace fs = std::filesystem;

namespace {

const char *kA2l = R"(
ASAP2_VERSION 1 71
/begin PROJECT p ""
/begin MODULE m ""
/begin A2ML  block "IF_DATA" struct { int; }; /* not parsed */ /end A2ML
/begin MOD_PAR ""
EPK "build_x" ADDR_EPK 0xFFFFFF00
/end MOD_PAR
/begin IF_DATA XCP
  /begin DAQ DYNAMIC 0 2 0 OPTIMISATION_TYPE_DEFAULT ADDRESS_EXTENSION_FREE IDENTIFICATION_FIELD_TYPE_RELATIVE_BYTE GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE 0xF8 OVERLOAD_INDICATION_PID
    /begin EVENT "tick" "tick" 1 DAQ 0xFF 1 6 0 CONSISTENCY DAQ /end EVENT
    /begin EVENT "cycle" "cycle" 0 DAQ 0xFF 0 0 0 CONSISTENCY DAQ /end EVENT
  /end DAQ
  /begin XCP_ON_UDP_IP 0x0104 5555 ADDRESS "127.0.0.1" /end XCP_ON_UDP_IP
/end IF_DATA
/begin RECORD_LAYOUT F32 FNC_VALUES 1 FLOAT32_IEEE ROW_DIR DIRECT /end RECORD_LAYOUT
/begin RECORD_LAYOUT U32 FNC_VALUES 1 ULONG ROW_DIR DIRECT /end RECORD_LAYOUT
/begin COMPU_VTAB s_trip.table "" TAB_VERB 3 0 "NONE" 1 "OVER_TEMP" 2 "SENSOR" /end COMPU_VTAB
/begin COMPU_METHOD s_trip "" TAB_VERB "%.0" "" COMPU_TAB_REF s_trip.table /end COMPU_METHOD
/begin TYPEDEF_CHARACTERISTIC setpoint "" VALUE F32 0 NO_COMPU_METHOD 100 450 PHYS_UNIT "degC" /end TYPEDEF_CHARACTERISTIC
/begin TYPEDEF_CHARACTERISTIC request "" VALUE U32 0 IDENTITY 0 4294967295 /end TYPEDEF_CHARACTERISTIC
/begin TYPEDEF_STRUCTURE demo_cal_page "" 8
  /begin STRUCTURE_COMPONENT setpoint setpoint 0 /end STRUCTURE_COMPONENT
  /begin STRUCTURE_COMPONENT request request 4 /end STRUCTURE_COMPONENT
/end TYPEDEF_STRUCTURE
/begin TYPEDEF_MEASUREMENT run_applied "" UBYTE IDENTITY 0 0 0 255 /end TYPEDEF_MEASUREMENT
/begin TYPEDEF_MEASUREMENT temp "" FLOAT32_IEEE NO_COMPU_METHOD 0 0 -1e32 1e32 /end TYPEDEF_MEASUREMENT
/begin TYPEDEF_STRUCTURE rte_t "" 8
  /begin STRUCTURE_COMPONENT run_applied run_applied 4 /end STRUCTURE_COMPONENT
/end TYPEDEF_STRUCTURE
/begin TYPEDEF_STRUCTURE loop_t "" 8
  /begin STRUCTURE_COMPONENT temp temp 0 /end STRUCTURE_COMPONENT
/end TYPEDEF_STRUCTURE
/begin MEASUREMENT s_trip "" ULONG s_trip 0 0 0 4294967295 ECU_ADDRESS 0x1000 /end MEASUREMENT
/begin MEASUREMENT arr "" SWORD NO_COMPU_METHOD 0 0 -10 10 ECU_ADDRESS 0x5000 MATRIX_DIM 3 /end MEASUREMENT
/begin CHARACTERISTIC gain "" VALUE 0x6000 F32 0 NO_COMPU_METHOD 0 10 /end CHARACTERISTIC
/begin INSTANCE s_rte "" rte_t 0x2000 ECU_ADDRESS_EXTENSION 0 /end INSTANCE
/begin INSTANCE s_loop "" loop_t 0x3000 ECU_ADDRESS_EXTENSION 0 /begin IF_DATA XCP /begin DAQ_EVENT VARIABLE /begin DEFAULT_EVENT_LIST EVENT 1 /end DEFAULT_EVENT_LIST /end DAQ_EVENT /end IF_DATA /end INSTANCE
/begin MEASUREMENT s_count "" ULONG IDENTITY 0 0 0 4294967295 ECU_ADDRESS 0x3100 /begin IF_DATA XCP /begin DAQ_EVENT VARIABLE /begin DEFAULT_EVENT_LIST EVENT 0 /end DEFAULT_EVENT_LIST /end DAQ_EVENT /end IF_DATA /end MEASUREMENT
/begin INSTANCE demo_cal_page "" demo_cal_page 0x4000 /end INSTANCE
/end MODULE
/end PROJECT
)";

// An XCP slave on a localhost UDP port, with a byte memory. max_cto 8, so long reads and writes
// take several commands; each answer comes after an event packet the master has to skip.
// silent: never answers. DAQ as XCPlite does it: dynamic lists, ODT + fill + DAQ word headers,
// 32 bit timestamps in us in the first ODT; trigger() sends one event's packets.
class FakeEcu {
public:
  std::map<std::uint32_t, std::uint8_t> mem; // only touched with the io thread idle (see set/get)
  std::atomic<bool> silent{false};
  std::atomic<int> connects{0}, disconnects{0};
  std::atomic<bool> daq_running{false};

  // Sends the DAQ packets of every list on `event` with the memory as it is now. skip: the
  // transport counter jumps by that many first, as after lost packets.
  void trigger(int event, std::uint32_t ts_us, int skip = 0)
  {
    asio::post(io_, [this, event, ts_us, skip] {
      std::lock_guard<std::mutex> lock(mu_);
      if (!daq_running) {
        return;
      }
      tx_ += static_cast<std::uint16_t>(skip);
      std::vector<std::uint8_t> out;
      for (std::size_t d = 0; d < lists_.size(); d++) {
        if (lists_[d].event != event) {
          continue;
        }
        for (std::size_t o = 0; o < lists_[d].odts.size(); o++) {
          std::vector<std::uint8_t> p{static_cast<std::uint8_t>(o), 0,
                                      static_cast<std::uint8_t>(d), 0};
          if (o == 0) {
            for (int k = 0; k < 4; k++) {
              p.push_back(static_cast<std::uint8_t>(ts_us >> (8 * k)));
            }
          }
          for (const auto &[addr, size] : lists_[d].odts[o]) {
            for (std::uint32_t k = 0; k < size; k++) {
              p.push_back(mem[addr + k]);
            }
          }
          frame(out, p);
        }
      }
      sock_.send_to(asio::buffer(out), peer_); // all of them in one datagram
    });
  }

  FakeEcu() : sock_(io_, asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 0))
  {
    put("build_x", 0xFFFFFF00);
    receive();
    thread_ = std::thread([this] { io_.run(); });
  }
  ~FakeEcu()
  {
    io_.stop();
    thread_.join();
  }
  std::string address() const
  {
    return "127.0.0.1:" + std::to_string(sock_.local_endpoint().port());
  }

  template <class T> void set(std::uint32_t addr, T v)
  {
    std::uint8_t b[sizeof v];
    std::memcpy(b, &v, sizeof v);
    std::lock_guard<std::mutex> lock(mu_);
    for (std::size_t k = 0; k < sizeof v; k++) {
      mem[addr + static_cast<std::uint32_t>(k)] = b[k];
    }
  }
  template <class T> T get(std::uint32_t addr)
  {
    std::uint8_t b[sizeof(T)];
    std::lock_guard<std::mutex> lock(mu_);
    for (std::size_t k = 0; k < sizeof b; k++) {
      b[k] = mem[addr + static_cast<std::uint32_t>(k)];
    }
    T v;
    std::memcpy(&v, b, sizeof v);
    return v;
  }

private:
  void put(const std::string &s, std::uint32_t addr)
  {
    for (std::size_t k = 0; k < s.size(); k++) {
      mem[addr + static_cast<std::uint32_t>(k)] = static_cast<std::uint8_t>(s[k]);
    }
  }

  void receive()
  {
    sock_.async_receive_from(asio::buffer(buf_), peer_, [this](std::error_code ec, std::size_t n) {
      if (ec) {
        return;
      }
      if (!silent && n >= 5) {
        answer(std::vector<std::uint8_t>(buf_ + 4, buf_ + n), buf_[2] | (buf_[3] << 8));
      }
      receive();
    });
  }

  static std::uint32_t u32(const std::vector<std::uint8_t> &p, std::size_t at)
  {
    return p[at] | (p[at + 1] << 8) | (p[at + 2] << 16) |
           (static_cast<std::uint32_t>(p[at + 3]) << 24);
  }

  // [len][ctr] and the packet, the counter running over everything sent
  void frame(std::vector<std::uint8_t> &out, const std::vector<std::uint8_t> &p)
  {
    out.insert(out.end(), {static_cast<std::uint8_t>(p.size()), 0, static_cast<std::uint8_t>(tx_),
                           static_cast<std::uint8_t>(tx_ >> 8)});
    out.insert(out.end(), p.begin(), p.end());
    tx_++;
  }

  static unsigned u16(const std::vector<std::uint8_t> &p, std::size_t at)
  {
    return p[at] | (p[at + 1] << 8);
  }

  struct List {
    int event = -1;
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> odts; // (addr, size)
  };

  // The DAQ commands; false: not one of them
  bool daq(const std::vector<std::uint8_t> &p, std::vector<std::uint8_t> &r)
  {
    switch (p[0]) {
    case 0xDA: // GET_DAQ_PROCESSOR_INFO: dynamic, timestamps; key: ODT, fill, DAQ word
      r.insert(r.end(), {0x11, 0, 0, 2, 0, 0, 0xC0});
      return true;
    case 0xD9: // GET_DAQ_RESOLUTION_INFO: entries up to 8 bytes; 4 byte timestamps of 1 us
      r.insert(r.end(), {1, 8, 1, 8, 0x34, 1, 0});
      return true;
    case 0xD6:
      lists_.clear();
      daq_running = false;
      return true;
    case 0xD5:
      lists_.assign(u16(p, 2), List{});
      return true;
    case 0xD4:
      lists_.at(u16(p, 2)).odts.resize(p[4]);
      return true;
    case 0xD3:
      lists_.at(u16(p, 2)).odts.at(p[4]).reserve(p[5]);
      return true;
    case 0xE2:
      ptr_ = {u16(p, 2), p[4]};
      return true;
    case 0xE1:
      lists_.at(ptr_.first).odts.at(ptr_.second).push_back({u32(p, 4), p[2]});
      return true;
    case 0xE0:
      lists_.at(u16(p, 2)).event = static_cast<int>(u16(p, 4));
      return true;
    case 0xDE:
      r.push_back(0); // first PID
      return true;
    case 0xDD:
      daq_running = p[1] == 1;
      return true;
    default:
      return false;
    }
  }

  void answer(const std::vector<std::uint8_t> &p, int)
  {
    std::vector<std::uint8_t> r{0xFF};
    std::lock_guard<std::mutex> lock(mu_);
    if (daq(p, r)) {
      std::vector<std::uint8_t> out;
      frame(out, r);
      sock_.send_to(asio::buffer(out), peer_);
      return;
    }
    switch (p[0]) {
    case 0xFF: // CONNECT: resource, comm mode (little endian), max_cto 8, max_dto, versions
      connects++;
      r.insert(r.end(), {0x00, 0x00, 8, 0x00, 0x01, 0x01, 0x01});
      break;
    case 0xFE:
      disconnects++;
      break;
    case 0xF4: { // SHORT_UPLOAD n _ ext addr
      const std::uint32_t a = u32(p, 4);
      for (std::size_t k = 0; k < p[1]; k++) {
        const auto it = mem.find(a + static_cast<std::uint32_t>(k));
        if (it == mem.end()) {
          r = {0xFE, 0x22};
          break;
        }
        r.push_back(it->second);
      }
      break;
    }
    case 0xED: // SHORT_DOWNLOAD n _ ext addr data
      for (std::size_t k = 0; k < p[1]; k++) {
        mem[u32(p, 4) + static_cast<std::uint32_t>(k)] = p[8 + k];
      }
      break;
    case 0xF6: // SET_MTA
      mta_ = u32(p, 4);
      break;
    case 0xF0: // DOWNLOAD n data
      for (std::size_t k = 0; k < p[1]; k++) {
        mem[mta_++] = p[2 + k];
      }
      break;
    default:
      r = {0xFE, 0x20};
    }
    std::vector<std::uint8_t> out;
    frame(out, {0xFD, 0x00}); // an event first
    frame(out, r);
    sock_.send_to(asio::buffer(out), peer_);
  }

  std::mutex mu_; // guards mem against the test thread
  asio::io_context io_;
  asio::ip::udp::socket sock_;
  asio::ip::udp::endpoint peer_;
  std::uint8_t buf_[512];
  std::uint32_t mta_ = 0;
  std::uint16_t tx_ = 0; // transport counter of what it sends
  std::vector<List> lists_;
  std::pair<unsigned, unsigned> ptr_{0, 0};
  std::thread thread_;
};

template <class F> bool eventually(F &&f)
{
  for (int i = 0; i < 300; i++) {
    if (f()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

} // namespace

TEST_CASE("a2l: symbols, struct members, units, limits, enums")
{
  const A2l a = parse_a2l(kA2l);
  CHECK(a.epk == "build_x");
  CHECK(a.epk_addr == 0xFFFFFF00u);
  CHECK(a.host == "127.0.0.1");
  CHECK(a.port == 5555);

  const Symbol &sp = a.at("demo_cal_page.setpoint");
  CHECK(sp.addr == 0x4000);
  CHECK(sp.fmt == 'f');
  CHECK(sp.writable);
  CHECK(sp.unit == "degC");
  CHECK(sp.min == 100);
  CHECK(sp.max == 450);
  CHECK(a.at("demo_cal_page.request").addr == 0x4004);
  CHECK(a.at("demo_cal_page.request").fmt == 'I');
  CHECK(a.at("s_rte.run_applied").addr == 0x2004);
  CHECK(a.at("s_rte.run_applied").fmt == 'B');
  CHECK_FALSE(a.at("s_loop.temp").writable);
  CHECK(a.at("s_trip").names.at(1) == "OVER_TEMP");
  CHECK(a.at("arr").count == 3);
  CHECK(a.at("arr").size() == 6);
  CHECK(a.at("gain").addr == 0x6000);
  CHECK(a.at("gain").writable);
  CHECK_THROWS_AS(a.at("nope"), A2lError);
  CHECK(a.events == std::map<int, std::string>{{0, "cycle"}, {1, "tick"}});
  CHECK(a.at("s_loop.temp").event == 1); // an instance's event goes to its members
  CHECK(a.at("s_count").event == 0);
  CHECK(a.at("s_trip").event == -1);
}

TEST_CASE("xcp: DAQ from a fake ECU")
{
  FakeEcu fake;
  const A2l a = parse_a2l(kA2l);
  fake.set<float>(0x3000, 1.5f);
  fake.set<std::uint32_t>(0x3100, 7);
  fake.set<float>(0x4000, 350.0f);
  Ecu ecu(std::chrono::milliseconds(300));
  ecu.connect(fake.address(), a);
  ecu.daq_start({a.at("s_loop.temp"), a.at("s_count")}); // a list per event
  CHECK(ecu.daq_running());
  CHECK(fake.daq_running);

  fake.trigger(1, 1000);
  fake.trigger(1, 2000);
  fake.trigger(0, 2500);
  CHECK(ecu.read(a.at("demo_cal_page.setpoint")) == std::vector<double>{350}); // between packets
  std::vector<DaqSample> got;
  REQUIRE(eventually([&] {
    for (const auto &s : ecu.daq_take(std::chrono::milliseconds(5))) {
      got.push_back(s);
    }
    return got.size() == 3;
  }));
  CHECK(got[0].signal == 0);
  CHECK(got[0].v == 1.5);
  CHECK(got[0].t == 0);
  CHECK(got[1].t == doctest::Approx(0.001));
  CHECK(got[2].signal == 1);
  CHECK(got[2].v == 7);
  CHECK(got[2].t == doctest::Approx(0.0015)); // the clock is shared by the lists
  CHECK(ecu.daq_lost() == 0);

  // A timestamp that wraps, and packets lost on the way
  fake.trigger(1, 0xFFFFFF00u, 2);
  fake.trigger(1, 0x100);
  got.clear();
  REQUIRE(eventually([&] {
    for (const auto &s : ecu.daq_take(std::chrono::milliseconds(5))) {
      got.push_back(s);
    }
    return got.size() == 2;
  }));
  CHECK(got[1].t - got[0].t == doctest::Approx(0x200 * 1e-6));
  CHECK(ecu.daq_lost() == 2);

  Symbol no_event = a.at("s_trip");
  CHECK_THROWS_AS(ecu.daq_start({no_event}), XcpError);
  ecu.daq_start({a.at("s_loop.temp")});
  ecu.daq_stop();
  CHECK_FALSE(ecu.daq_running());
  CHECK(eventually([&] { return !fake.daq_running; }));
}

TEST_CASE("xcp: encode and decode")
{
  Symbol f;
  f.fmt = 'f';
  CHECK(decode(f, encode(f, {2.5})) == std::vector<double>{2.5});
  Symbol h;
  h.fmt = 'h';
  h.count = 2;
  CHECK(decode(h, encode(h, {-3, 7.4})) == std::vector<double>{-3, 7});
  Symbol b;
  b.fmt = 'B';
  CHECK_THROWS_AS(encode(b, {256}), XcpError);
  CHECK_THROWS_AS(encode(b, {NAN}), XcpError);
  CHECK_THROWS_AS(encode(b, {1, 2}), XcpError);
}

TEST_CASE("xcp: connect, read and write a fake ECU")
{
  FakeEcu fake;
  const A2l a = parse_a2l(kA2l);
  fake.set<float>(0x4000, 350.0f);
  fake.set<std::int16_t>(0x5000, -1);
  fake.set<std::int16_t>(0x5002, 2);
  fake.set<std::int16_t>(0x5004, 9);
  {
    Ecu ecu(std::chrono::milliseconds(300));
    ecu.connect(fake.address(), a);
    CHECK(fake.connects == 1);
    CHECK(ecu.read(a.at("demo_cal_page.setpoint")) == std::vector<double>{350});
    CHECK(ecu.read(a.at("arr")) == std::vector<double>{-1, 2, 9});
    ecu.write(a.at("demo_cal_page.setpoint"), {300});
    CHECK(fake.get<float>(0x4000) == 300.0f);
    ecu.download(0x7000, 0, std::vector<std::uint8_t>(20, 0xAB)); // SET_MTA + DOWNLOAD
    CHECK(fake.get<std::uint8_t>(0x7013) == 0xAB);
    CHECK(ecu.upload(0x7000, 0, 20) == std::vector<std::uint8_t>(20, 0xAB)); // 3 uploads
    CHECK_THROWS_WITH_AS(ecu.read(a.at("s_trip")), "command 0xF4 refused: OUT_OF_RANGE",
                         XcpError);
    CHECK(ecu.is_open()); // a refusal keeps the link
  }
  CHECK(eventually([&] { return fake.disconnects == 1; }));

  A2l other = a;
  other.epk = "build_y";
  Ecu ecu(std::chrono::milliseconds(300));
  CHECK_THROWS_AS(ecu.connect(fake.address(), other), XcpError);
  CHECK_FALSE(ecu.is_open());

  fake.silent = true;
  CHECK_THROWS_AS(ecu.connect(fake.address(), a), LinkError);
  CHECK_FALSE(ecu.is_open());
}

TEST_CASE("ecu model: status, calibration, requests, watched signals")
{
  FakeEcu fake;
  fake.set<float>(0x4000, 350.0f);
  fake.set<std::uint32_t>(0x4004, 0x0501);
  fake.set<std::uint8_t>(0x2004, 1);
  fake.set<std::uint32_t>(0x1000, 1);
  fake.set<float>(0x3000, 25.5f);

  const fs::path root = fs::temp_directory_path() / "krumpir_test_ecu";
  fs::remove_all(root);
  const fs::path dir = root / "controllers" / "demo-controller";
  fs::create_directories(dir / "src" / "gen");
  fs::create_directories(dir / "docs");
  std::ofstream(dir / "docs" / "demo.a2l") << kA2l;
  std::ofstream(dir / "src" / "gen" / "ctrl_gen.json") << R"({
    "name": "demo", "a2l": "docs/demo.a2l",
    "params": [
      {"name": "sp", "help": "setpoint", "unit": "degC", "min": 100, "max": 450, "default": 350,
       "cal": "demo_cal_page.setpoint"},
      {"name": "ramp", "help": "ramp", "unit": null, "min": 0, "max": 100, "default": 0, "cal": null}],
    "request": {"name": "demo_cal_page.request", "start": 1, "stop": 2, "clear": 3},
    "status": {"running": "s_rte.run_applied", "trip": "s_trip"},
    "monitor": ["s_loop.temp", "nope"]})";

  std::atomic<int> changes{0};
  {
    EcuModel m([&] { changes++; }, std::chrono::milliseconds(10), std::chrono::milliseconds(300));
    m.scan(root.generic_string());
    m.open(dir.generic_string());
    REQUIRE(eventually(
        [&] { return m.view([](const EcuSnapshot &s) { return !s.selected.empty(); }); }));
    m.view([&](const EcuSnapshot &s) {
      CHECK(s.a2l_error == "");
      CHECK(s.epk == "build_x");
      CHECK(s.params.size() == 2);
      CHECK(s.params[0].unit == "degC");
      CHECK(s.address == "127.0.0.1:5555");
      return 0;
    });

    m.connect(fake.address());
    std::uint32_t ts = 0;
    REQUIRE(eventually([&] {
      fake.trigger(1, ts += 1000); // s_loop.temp has an event: it comes by DAQ
      return m.view(
          [](const EcuSnapshot &s) { return s.connected && s.values.count("s_loop.temp") > 0; });
    }));
    m.view([&](const EcuSnapshot &s) {
      CHECK(s.daq_signals == 1);
      CHECK(s.running);
      CHECK(s.trip == 1);
      CHECK(s.trip_name == "OVER_TEMP");
      CHECK(s.params[0].value == 350);
      CHECK(std::isnan(s.params[1].value));
      CHECK(s.watched == std::vector<std::string>{"s_loop.temp"}); // "nope" is not in the A2L
      CHECK(s.values.at("s_loop.temp") == 25.5);
      return 0;
    });

    m.write("sp", 300);
    m.write("sp", 999); // outside the limits: refused before it is sent
    m.write("ramp", 1); // console only
    m.request("start");
    m.request("clear");
    REQUIRE(eventually([&] { return fake.get<std::uint32_t>(0x4004) == 0x0703; }));
    CHECK(fake.get<float>(0x4000) == 300.0f);
    REQUIRE(eventually(
        [&] { return m.view([](const EcuSnapshot &s) { return s.params[0].value == 300; }); }));
    m.view([&](const EcuSnapshot &s) {
      std::string all;
      for (const auto &e : s.events) {
        all += e + "\n";
      }
      CHECK(all.find("tripped: OVER_TEMP") != std::string::npos);
      CHECK(all.find("sp: 999 is outside 100..450") != std::string::npos);
      CHECK(all.find("'ramp' has no calibration value") != std::string::npos);
      CHECK(all.find("start requested") != std::string::npos);
      return 0;
    });

    fake.set<std::uint32_t>(0x1000, 0);
    REQUIRE(eventually([&] { return m.view([](const EcuSnapshot &s) { return s.trip == 0; }); }));
    m.disconnect();
    REQUIRE(eventually([&] { return m.view([](const EcuSnapshot &s) { return !s.connected; }); }));
    CHECK(fake.disconnects == 1);
  }
  CHECK(changes > 0);
  fs::remove_all(root);
}

TEST_CASE("controllers of the repo")
{
  const auto list = find_controllers(KT_REPO_ROOT);
  REQUIRE(list.size() == 4); // each on the board and on the PC (host/)
  CHECK(list[0].name == "motor");
  CHECK(list[1].name == "motor pc");
  CHECK(list[1].a2l.find("docs/motor_host.a2l") != std::string::npos);
  CHECK(list[2].name == "t12");
  CHECK(list[3].name == "t12 pc");
  for (const auto &c : list) {
    const std::string name = c.desc.at("name");
    CHECK(c.desc.at("request").at("name") == name + "_cal_page.request");
    if (!fs::exists(c.a2l)) {
      continue; // gitignored: only there after an XCP build
    }
    const A2l a = load_a2l(c.a2l);
    CHECK(a.symbols.count(c.desc.at("status").at("running").get<std::string>()) == 1);
    CHECK_FALSE(a.at(c.desc.at("status").at("trip").get<std::string>()).names.empty());
  }
}

// With KT_ECU=<a2l> (a controller running with XCP at the A2L's address, e.g. a PC build with
// --xcp, controllers/README.md "XCP on the PC build") also against it: the EPK at connect, a calibration write read back,
// and DAQ on the 1 kHz tick.
TEST_CASE("xcp: a live controller (KT_ECU)")
{
  const char *path = std::getenv("KT_ECU");
  if (path == nullptr) {
    return;
  }
  const A2l a = load_a2l(path);
  Ecu ecu;
  ecu.connect(a.host + ":" + std::to_string(a.port), a); // throws on another build's EPK

  const Symbol *sp = nullptr;
  for (const auto &[name, s] : a.symbols) {
    if (name.size() > 18 && name.compare(name.size() - 18, 18, "_cal_page.setpoint") == 0) {
      sp = &s;
    }
  }
  REQUIRE(sp != nullptr);
  const double old = ecu.read(*sp)[0];
  ecu.write(*sp, {old + 1});
  CHECK(ecu.read(*sp)[0] == doctest::Approx(old + 1));
  ecu.write(*sp, {old});

  const Symbol &duty = a.at("s_loop.duty");
  REQUIRE(duty.event >= 0);
  ecu.daq_start({duty});
  std::size_t n = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (std::chrono::steady_clock::now() < end) {
    n += ecu.daq_take(std::chrono::milliseconds(50)).size();
  }
  ecu.daq_stop();
  CHECK(n > 300); // about 500 at 1 kHz
  CHECK(ecu.daq_lost() == 0);
}
