// A2L parsing, the XCP master against a fake ECU on localhost, and the controller model.
#include <doctest/doctest.h>

#include <asio.hpp>
#include <atomic>
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
/begin IF_DATA XCP /begin XCP_ON_UDP_IP 0x0104 5555 ADDRESS "127.0.0.1" /end XCP_ON_UDP_IP /end IF_DATA
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
/begin INSTANCE s_loop "" loop_t 0x3000 ECU_ADDRESS_EXTENSION 0 /end INSTANCE
/begin INSTANCE demo_cal_page "" demo_cal_page 0x4000 /end INSTANCE
/end MODULE
/end PROJECT
)";

// An XCP slave on a localhost UDP port, with a byte memory. max_cto 8, so long reads and writes
// take several commands; each answer comes after an event packet the master has to skip.
// silent: never answers.
class FakeEcu {
public:
  std::map<std::uint32_t, std::uint8_t> mem; // only touched with the io thread idle (see set/get)
  std::atomic<bool> silent{false};
  std::atomic<int> connects{0}, disconnects{0};

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

  void answer(const std::vector<std::uint8_t> &p, int ctr)
  {
    std::vector<std::uint8_t> r{0xFF};
    std::lock_guard<std::mutex> lock(mu_);
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
    std::vector<std::uint8_t> out{2, 0, 0, 0, 0xFD, 0x00}; // an event first
    out.insert(out.end(), {static_cast<std::uint8_t>(r.size()), 0, static_cast<std::uint8_t>(ctr),
                           static_cast<std::uint8_t>(ctr >> 8)});
    out.insert(out.end(), r.begin(), r.end());
    sock_.send_to(asio::buffer(out), peer_);
  }

  std::mutex mu_; // guards mem against the test thread
  asio::io_context io_;
  asio::ip::udp::socket sock_;
  asio::ip::udp::endpoint peer_;
  std::uint8_t buf_[512];
  std::uint32_t mta_ = 0;
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
    REQUIRE(eventually([&] {
      return m.view(
          [](const EcuSnapshot &s) { return s.connected && s.values.count("s_loop.temp") > 0; });
    }));
    m.view([&](const EcuSnapshot &s) {
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
  REQUIRE(list.size() == 2);
  CHECK(list[0].name == "motor");
  CHECK(list[1].name == "t12");
  for (const auto &c : list) {
    CHECK(c.desc.at("request").at("name") == c.name + "_cal_page.request");
    if (!fs::exists(c.a2l)) {
      continue; // gitignored: only there after an XCP build
    }
    const A2l a = load_a2l(c.a2l);
    CHECK(a.symbols.count(c.desc.at("status").at("running").get<std::string>()) == 1);
    CHECK_FALSE(a.at(c.desc.at("status").at("trip").get<std::string>()).names.empty());
  }
}
