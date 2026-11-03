#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace krumpir {

struct A2lError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// A measurement or calibration value of the ECU.
struct Symbol {
  std::uint32_t addr = 0;
  std::uint8_t ext = 0;
  char fmt = 'B';         // B b H h I i Q q (unsigned/signed 8..64 bit), f (float32), d (float64)
  std::size_t count = 1;  // array length
  bool writable = false;  // a characteristic (calibration value)
  std::string unit;
  double min = 0, max = 0;            // the A2L's limits (both 0: none given)
  std::map<long long, std::string> names; // enum values (COMPU_VTAB), empty when none

  std::size_t elem_size() const;
  std::size_t size() const { return elem_size() * count; }
};

// The parts of an A2L file an XCP master needs, as xcpclient writes them for XCPlite: the
// symbols (struct members joined by dots: s_loop.temp, t12_cal_page.setpoint), the EPK and the
// UDP address. Same subset as tools/krumpir/krumpir/xcp.py, plus units, limits and enum names.
struct A2l {
  std::map<std::string, Symbol> symbols;
  std::string epk;
  std::uint32_t epk_addr = 0;
  std::string host;
  unsigned port = 5555;

  const Symbol &at(const std::string &name) const; // throws A2lError
};

A2l parse_a2l(const std::string &text);
A2l load_a2l(const std::string &path); // throws A2lError when unreadable

} // namespace krumpir
