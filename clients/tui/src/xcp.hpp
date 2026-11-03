#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "a2l.hpp"
#include "service.hpp" // LinkError

namespace krumpir {

// The ECU refused a command, or does not match its A2L. The link is fine.
struct XcpError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// One value of a DAQ measurement: `signal` is its index in daq_start's list, t the ECU's time (s,
// from the first DAQ packet).
struct DaqSample {
  std::size_t signal;
  double t, v;
};

// A blocking XCP master on UDP, as tools/krumpir/krumpir/xcp.py: CONNECT with an EPK check
// against the A2L, reads by SHORT_UPLOAD, writes by SHORT_DOWNLOAD (SET_MTA + DOWNLOAD for long
// values), DISCONNECT on close so the next master can connect. A missing answer throws LinkError
// and closes. Little-endian ECUs only.
// DAQ: dynamic DAQ lists, one per event, with the ECU's timestamps; the packets are decoded as
// they come in, also while a command waits for its answer.
class Ecu {
public:
  explicit Ecu(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
  ~Ecu(); // disconnects
  Ecu(const Ecu &) = delete;
  Ecu &operator=(const Ecu &) = delete;

  // "host:port", or "host" for port 5555. Checks the EPK when the A2L has one.
  void connect(const std::string &address, const A2l &a2l);
  void close();
  bool is_open() const;

  std::vector<double> read(const Symbol &s);  // count values
  void write(const Symbol &s, const std::vector<double> &v); // count values; ints are rounded

  std::vector<std::uint8_t> upload(std::uint32_t addr, std::uint8_t ext, std::size_t n);
  void download(std::uint32_t addr, std::uint8_t ext, const std::vector<std::uint8_t> &data);

  // Measures each signal (a scalar with an event) on its event, replacing a running measurement.
  // A refusal throws XcpError and leaves DAQ stopped.
  void daq_start(const std::vector<Symbol> &signals);
  void daq_stop();
  bool daq_running() const;
  // Waits up to `wait` for DAQ packets and returns the samples that came in since the last call.
  std::vector<DaqSample> daq_take(std::chrono::milliseconds wait);
  std::uint64_t daq_lost() const; // packets missing (transport counter gaps) since daq_start

private:
  std::vector<std::uint8_t> cmd(std::vector<std::uint8_t> packet);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Bytes <-> values of a symbol's type (little endian).
std::vector<double> decode(const Symbol &s, const std::vector<std::uint8_t> &bytes);
std::vector<std::uint8_t> encode(const Symbol &s, const std::vector<double> &v);

} // namespace krumpir
