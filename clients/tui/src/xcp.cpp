#include "xcp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <type_traits>

#include <asio.hpp>

namespace krumpir {

using asio::ip::udp;

namespace {

const std::map<int, const char *> kErrors{
    {0x10, "CMD_BUSY"},        {0x11, "DAQ_ACTIVE"},        {0x12, "PGM_ACTIVE"},
    {0x20, "CMD_UNKNOWN"},     {0x21, "CMD_SYNTAX"},        {0x22, "OUT_OF_RANGE"},
    {0x23, "WRITE_PROTECTED"}, {0x24, "ACCESS_DENIED"},     {0x25, "ACCESS_LOCKED"},
    {0x26, "PAGE_NOT_VALID"},  {0x27, "MODE_NOT_VALID"},    {0x28, "SEGMENT_NOT_VALID"},
    {0x29, "SEQUENCE"},        {0x2A, "DAQ_CONFIG"},        {0x30, "MEMORY_OVERFLOW"},
    {0x31, "GENERIC"},         {0x32, "VERIFY"}};

void put32(std::vector<std::uint8_t> &p, std::uint32_t v)
{
  for (int k = 0; k < 4; k++) {
    p.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
  }
}

template <class T> double get(const std::uint8_t *p)
{
  T v;
  std::memcpy(&v, p, sizeof v); // ponytail: the host is little endian (x86, arm64), as the ECU
  return static_cast<double>(v);
}

template <class T> void put(std::vector<std::uint8_t> &out, double v)
{
  if constexpr (std::is_integral_v<T>) {
    if (!std::isfinite(v)) {
      throw XcpError("not a number");
    }
    const double r = std::round(v);
    if (r < static_cast<double>(std::numeric_limits<T>::min()) ||
        r > static_cast<double>(std::numeric_limits<T>::max())) {
      throw XcpError("out of the type's range");
    }
    v = r;
  }
  const T x = static_cast<T>(v);
  std::uint8_t b[sizeof x];
  std::memcpy(b, &x, sizeof x);
  out.insert(out.end(), b, b + sizeof x);
}

} // namespace

std::vector<double> decode(const Symbol &s, const std::vector<std::uint8_t> &bytes)
{
  std::vector<double> out;
  const std::size_t n = s.elem_size();
  for (std::size_t k = 0; k + n <= bytes.size() && out.size() < s.count; k += n) {
    const std::uint8_t *p = bytes.data() + k;
    switch (s.fmt) {
    case 'B': out.push_back(get<std::uint8_t>(p)); break;
    case 'b': out.push_back(get<std::int8_t>(p)); break;
    case 'H': out.push_back(get<std::uint16_t>(p)); break;
    case 'h': out.push_back(get<std::int16_t>(p)); break;
    case 'I': out.push_back(get<std::uint32_t>(p)); break;
    case 'i': out.push_back(get<std::int32_t>(p)); break;
    case 'Q': out.push_back(get<std::uint64_t>(p)); break;
    case 'q': out.push_back(get<std::int64_t>(p)); break;
    case 'f': out.push_back(get<float>(p)); break;
    default: out.push_back(get<double>(p)); break;
    }
  }
  return out;
}

std::vector<std::uint8_t> encode(const Symbol &s, const std::vector<double> &v)
{
  if (v.size() != s.count) {
    throw XcpError("takes " + std::to_string(s.count) + " value(s)");
  }
  std::vector<std::uint8_t> out;
  for (const double x : v) {
    switch (s.fmt) {
    case 'B': put<std::uint8_t>(out, x); break;
    case 'b': put<std::int8_t>(out, x); break;
    case 'H': put<std::uint16_t>(out, x); break;
    case 'h': put<std::int16_t>(out, x); break;
    case 'I': put<std::uint32_t>(out, x); break;
    case 'i': put<std::int32_t>(out, x); break;
    case 'Q': put<std::uint64_t>(out, x); break;
    case 'q': put<std::int64_t>(out, x); break;
    case 'f': put<float>(out, x); break;
    default: put<double>(out, x); break;
    }
  }
  return out;
}

struct Ecu::Impl {
  asio::io_context io;
  udp::socket sock{io};
  std::chrono::milliseconds timeout{};
  std::uint16_t ctr = 0;
  std::size_t max_cto = 8, max_dto = 8;
  std::string address;

  // DAQ: the lists as configured, how the ECU frames its packets, what came in
  struct List {
    std::vector<std::vector<std::size_t>> odts; // signals per ODT
    std::uint8_t first_pid = 0;
    bool seen = false;
    std::uint64_t raw = 0; // last timestamp as sent
    double t = 0;          // its time since the first packet of any list (s)
  };
  std::vector<Symbol> signals;
  std::vector<List> lists;
  bool daq = false, have_t0 = false;
  std::uint64_t t0 = 0; // the first timestamp of the measurement
  std::size_t header = 4, ts_size = 4;
  double ts_unit = 1e-6; // s per timestamp tick
  std::vector<DaqSample> samples;
  int last_ctr = -1;
  std::uint64_t lost = 0;

  void drop()
  {
    std::error_code ignored;
    sock.close(ignored);
    daq = false;
  }

  // Waits for one datagram until the deadline: its size, or 0 when none came. A socket error
  // drops the link and throws.
  std::size_t receive(std::uint8_t *buf, std::size_t cap,
                      std::chrono::steady_clock::time_point deadline)
  {
    std::size_t got = 0;
    bool done = false;
    std::error_code ec;
    sock.async_receive(asio::buffer(buf, cap), [&](std::error_code e, std::size_t n) {
      ec = e;
      got = n;
      done = true;
    });
    io.restart();
    io.run_until(deadline);
    if (!done) {
      std::error_code ignored;
      sock.cancel(ignored);
      io.restart();
      io.run(); // the cancelled handler
      return 0;
    }
    if (ec) {
      drop();
      throw LinkError(address + ": " + ec.message());
    }
    return got;
  }

  // One DAQ packet (DTO) into samples.
  void dto(const std::uint8_t *p, std::size_t len)
  {
    if (len < header) {
      return;
    }
    std::size_t list = 0, odt = p[0];
    if (header == 1) { // absolute ODT number: find its list
      for (list = 0; list < lists.size(); list++) {
        if (p[0] >= lists[list].first_pid &&
            p[0] < lists[list].first_pid + lists[list].odts.size()) {
          break;
        }
      }
      odt = list < lists.size() ? p[0] - lists[list].first_pid : 0;
    } else {
      list = header == 2 ? p[1] : header == 3 ? (p[1] | (p[2] << 8)) : (p[2] | (p[3] << 8));
    }
    if (list >= lists.size() || odt >= lists[list].odts.size()) {
      return;
    }
    List &l = lists[list];
    std::size_t pos = header;
    if (odt == 0 && ts_size > 0) { // the first ODT carries the timestamp
      if (len < pos + ts_size) {
        return;
      }
      std::uint64_t raw = 0;
      std::memcpy(&raw, p + pos, ts_size);
      pos += ts_size;
      const std::uint64_t mask = ts_size >= 8 ? ~0ULL : (1ULL << (8 * ts_size)) - 1;
      if (!have_t0) {
        have_t0 = true;
        t0 = raw;
      }
      // Ticks since the last packet of this list (since t0 for its first), modulo the width
      const std::uint64_t since = (l.seen ? raw - l.raw : raw - t0) & mask;
      l.t = (l.seen ? l.t : 0) + static_cast<double>(since) * ts_unit;
      l.raw = raw;
      l.seen = true;
    }
    for (const std::size_t k : l.odts[odt]) {
      const Symbol &s = signals[k];
      if (pos + s.size() > len) {
        return;
      }
      const auto v = decode(s, std::vector<std::uint8_t>(p + pos, p + pos + s.size()));
      samples.push_back({k, l.t, v.empty() ? NAN : v[0]});
      pos += s.size();
    }
  }

  // The packets of one datagram: DAQ ones are decoded; the answer, if one is in it, is returned.
  bool packets(const std::uint8_t *buf, std::size_t got, std::vector<std::uint8_t> *answer)
  {
    bool found = false;
    for (std::size_t pos = 0; pos + 4 <= got;) { // one datagram may carry several packets
      const std::size_t n = static_cast<std::size_t>(buf[pos] | (buf[pos + 1] << 8));
      const int c = buf[pos + 2] | (buf[pos + 3] << 8);
      const std::uint8_t *pkt = buf + pos + 4;
      const std::size_t len = std::min(n, got - pos - 4);
      pos += 4 + n;
      const bool answer_packet = len >= 1 && (pkt[0] == 0xFF || pkt[0] == 0xFE);
      // The ECU counts every packet it sends: a gap is a lost one (an answer with 0: a server
      // that leaves answers out of the count)
      if (daq && !(answer_packet && c == 0)) {
        if (last_ctr >= 0) {
          lost += static_cast<std::uint16_t>(c - last_ctr - 1);
        }
        last_ctr = c;
      }
      if (len >= 1 && (pkt[0] == 0xFF || pkt[0] == 0xFE) && answer != nullptr && !found) {
        answer->assign(pkt, pkt + len);
        found = true;
      } else if (len >= 1 && pkt[0] < 0xFC && daq) {
        dto(pkt, len);
      } // events (0xFD) and service requests (0xFC) are skipped
    }
    return found;
  }
};

Ecu::Ecu(std::chrono::milliseconds timeout) : impl_(std::make_unique<Impl>())
{
  impl_->timeout = timeout;
}

Ecu::~Ecu() { close(); }

bool Ecu::is_open() const { return impl_->sock.is_open(); }

void Ecu::close()
{
  if (!is_open()) {
    return;
  }
  try {
    cmd({0xFE}); // DISCONNECT, so the next master can connect
  } catch (const std::exception &) {
  }
  impl_->drop();
}

std::vector<std::uint8_t> Ecu::cmd(std::vector<std::uint8_t> packet)
{
  if (!is_open()) {
    throw LinkError("not connected");
  }
  Impl &m = *impl_;
  m.ctr = static_cast<std::uint16_t>(m.ctr + 1);
  std::vector<std::uint8_t> frame{static_cast<std::uint8_t>(packet.size()),
                                  static_cast<std::uint8_t>(packet.size() >> 8),
                                  static_cast<std::uint8_t>(m.ctr),
                                  static_cast<std::uint8_t>(m.ctr >> 8)};
  frame.insert(frame.end(), packet.begin(), packet.end());
  std::error_code ec;
  m.sock.send(asio::buffer(frame), 0, ec);
  if (ec) {
    m.drop();
    throw LinkError(m.address + ": " + ec.message());
  }
  const auto deadline = std::chrono::steady_clock::now() + m.timeout;
  std::uint8_t buf[65536];
  std::vector<std::uint8_t> r;
  for (;;) { // DAQ packets on the way are decoded; events and service requests skipped
    const std::size_t got = m.receive(buf, sizeof buf, deadline);
    if (got == 0) {
      m.drop();
      char what[64];
      std::snprintf(what, sizeof what, ": no answer (command 0x%02X)", packet[0]);
      throw LinkError(m.address + what);
    }
    if (!m.packets(buf, got, &r)) {
      continue;
    }
    if (r[0] == 0xFF) {
      return r;
    }
    const int code = r.size() > 1 ? r[1] : 0;
    const auto it = kErrors.find(code);
    char what[64];
    std::snprintf(what, sizeof what, "command 0x%02X refused: ", packet[0]);
    throw XcpError(what + (it != kErrors.end() ? std::string(it->second)
                                               : "error " + std::to_string(code)));
  }
}

namespace {

void put16(std::vector<std::uint8_t> &p, unsigned v)
{
  p.push_back(static_cast<std::uint8_t>(v));
  p.push_back(static_cast<std::uint8_t>(v >> 8));
}

} // namespace

void Ecu::daq_start(const std::vector<Symbol> &signals)
{
  daq_stop();
  Impl &m = *impl_;
  const auto info = cmd({0xDA}); // GET_DAQ_PROCESSOR_INFO
  const auto res = cmd({0xD9});  // GET_DAQ_RESOLUTION_INFO
  if (info.size() < 8 || res.size() < 8) {
    throw XcpError("short DAQ info answer");
  }
  if (!(info[1] & 0x01)) {
    throw XcpError("the ECU has no dynamic DAQ lists");
  }
  static const std::size_t headers[] = {1, 2, 3, 4}; // identification field types
  m.header = headers[info[7] >> 6];
  const std::size_t max_entry = res[2];
  m.ts_size = (info[1] & 0x10) ? (res[5] & 0x07) : 0; // 1, 2 or 4 bytes; 0: no timestamps
  m.ts_unit = (res[6] | (res[7] << 8)) * std::pow(10.0, ((res[5] >> 4) & 0x0F) - 9.0);

  // One list per event; each list's signals in ODTs that fit a DAQ packet
  std::map<int, Impl::List> by_event;
  std::map<int, std::size_t> fill; // bytes in the event's last ODT
  for (std::size_t k = 0; k < signals.size(); k++) {
    const Symbol &s = signals[k];
    if (s.event < 0 || s.size() > max_entry || s.size() + m.header + m.ts_size > m.max_dto) {
      throw XcpError("a signal cannot be measured by DAQ (no event, or too large)");
    }
    auto &l = by_event[s.event];
    const std::size_t room = m.max_dto - m.header - (l.odts.size() <= 1 ? m.ts_size : 0);
    if (l.odts.empty() || fill[s.event] + s.size() > room) {
      if (l.odts.size() == 255) {
        throw XcpError("too many signals for one DAQ list");
      }
      l.odts.emplace_back();
      fill[s.event] = 0;
    }
    l.odts.back().push_back(k);
    fill[s.event] += s.size();
  }

  cmd({0xD6}); // FREE_DAQ
  std::vector<std::uint8_t> p{0xD5, 0}; // ALLOC_DAQ
  put16(p, static_cast<unsigned>(by_event.size()));
  cmd(p);
  unsigned d = 0;
  for (const auto &[ev, l] : by_event) { // ALLOC_ODT for every list, then the entries
    p = {0xD4, 0};
    put16(p, d++);
    p.push_back(static_cast<std::uint8_t>(l.odts.size()));
    cmd(p);
  }
  d = 0;
  for (const auto &[ev, l] : by_event) {
    for (std::size_t o = 0; o < l.odts.size(); o++) {
      p = {0xD3, 0}; // ALLOC_ODT_ENTRY
      put16(p, d);
      p.push_back(static_cast<std::uint8_t>(o));
      p.push_back(static_cast<std::uint8_t>(l.odts[o].size()));
      cmd(p);
    }
    d++;
  }
  d = 0;
  for (const auto &[ev, l] : by_event) {
    for (std::size_t o = 0; o < l.odts.size(); o++) {
      p = {0xE2, 0}; // SET_DAQ_PTR
      put16(p, d);
      p.push_back(static_cast<std::uint8_t>(o));
      p.push_back(0);
      cmd(p);
      for (const std::size_t k : l.odts[o]) {
        const Symbol &s = signals[k];
        p = {0xE1, 0xFF, static_cast<std::uint8_t>(s.size()), s.ext}; // WRITE_DAQ
        put32(p, s.addr);
        cmd(p);
      }
    }
    p = {0xE0, 0x10}; // SET_DAQ_LIST_MODE: with timestamps
    put16(p, d);
    put16(p, static_cast<unsigned>(ev));
    p.push_back(1); // prescaler
    p.push_back(0); // priority
    cmd(p);
    d++;
  }
  m.signals = signals;
  m.lists.clear();
  d = 0;
  for (auto &[ev, l] : by_event) {
    p = {0xDE, 2}; // START_STOP_DAQ_LIST: select
    put16(p, d++);
    const auto r = cmd(p);
    l.first_pid = r.size() > 1 ? r[1] : 0;
    m.lists.push_back(std::move(l));
  }
  m.samples.clear();
  m.have_t0 = false;
  m.last_ctr = -1;
  m.lost = 0;
  m.daq = true; // packets may come before the answer to the start
  try {
    cmd({0xDD, 1}); // START_STOP_SYNCH: start the selected lists
  } catch (const XcpError &) {
    m.daq = false;
    throw;
  }
}

void Ecu::daq_stop()
{
  Impl &m = *impl_;
  if (!m.daq) {
    return;
  }
  m.daq = false;
  m.samples.clear();
  try {
    cmd({0xDD, 0}); // START_STOP_SYNCH: stop all
    cmd({0xD6});    // FREE_DAQ
  } catch (const XcpError &) {
  }
}

bool Ecu::daq_running() const { return impl_->daq; }

std::uint64_t Ecu::daq_lost() const { return impl_->lost; }

std::vector<DaqSample> Ecu::daq_take(std::chrono::milliseconds wait)
{
  Impl &m = *impl_;
  if (m.daq && is_open()) {
    const auto deadline = std::chrono::steady_clock::now() + wait;
    std::uint8_t buf[65536];
    for (std::size_t got; (got = m.receive(buf, sizeof buf, deadline)) > 0;) {
      m.packets(buf, got, nullptr);
    }
  }
  return std::move(m.samples);
}

void Ecu::connect(const std::string &address, const A2l &a2l)
{
  close();
  Impl &m = *impl_;
  const auto colon = address.rfind(':');
  const std::string host = address.substr(0, colon);
  const std::string port = colon == std::string::npos ? "5555" : address.substr(colon + 1);
  m.address = address;
  std::error_code ec;
  udp::resolver resolver(m.io);
  const auto eps = resolver.resolve(udp::v4(), host, port, ec);
  if (ec || eps.empty()) {
    throw LinkError(address + ": " + (ec ? ec.message() : std::string("no address")));
  }
  m.sock.open(udp::v4(), ec);
  if (!ec) {
    std::error_code ignored; // room for DAQ bursts while a command is on its way
    m.sock.set_option(asio::socket_base::receive_buffer_size(1 << 20), ignored);
    m.sock.connect(*eps.begin(), ec);
  }
  if (ec) {
    m.drop();
    throw LinkError(address + ": " + ec.message());
  }
  const auto r = cmd({0xFF, 0x00}); // CONNECT, normal mode
  if (r.size() < 4 || (r[2] & 0x01)) {
    m.drop();
    throw XcpError(r.size() < 4 ? "short CONNECT answer"
                                : "the ECU is big endian; only little endian is supported");
  }
  m.max_cto = r[3] >= 8 ? r[3] : 8;
  m.max_dto = r.size() >= 6 ? static_cast<std::size_t>(r[4] | (r[5] << 8)) : 8;
  if (!a2l.epk.empty()) {
    const auto b = upload(a2l.epk_addr, 0, a2l.epk.size());
    const std::string epk(b.begin(), b.end());
    if (epk != a2l.epk) {
      close();
      throw XcpError("EPK mismatch: the ECU runs '" + epk + "', the A2L is for '" + a2l.epk +
                     "' (regenerate the A2L for this build)");
    }
  }
}

std::vector<std::uint8_t> Ecu::upload(std::uint32_t addr, std::uint8_t ext, std::size_t n)
{
  std::vector<std::uint8_t> out;
  while (out.size() < n) {
    const std::size_t k = std::min(n - out.size(), impl_->max_cto - 1);
    std::vector<std::uint8_t> p{0xF4, static_cast<std::uint8_t>(k), 0, ext}; // SHORT_UPLOAD
    put32(p, addr + static_cast<std::uint32_t>(out.size()));
    const auto r = cmd(p);
    if (r.size() < 1 + k) {
      throw XcpError("short SHORT_UPLOAD answer");
    }
    out.insert(out.end(), r.begin() + 1, r.begin() + 1 + static_cast<std::ptrdiff_t>(k));
  }
  return out;
}

void Ecu::download(std::uint32_t addr, std::uint8_t ext, const std::vector<std::uint8_t> &data)
{
  const std::size_t cto = impl_->max_cto;
  if (data.size() <= cto - 8) {
    std::vector<std::uint8_t> p{0xED, static_cast<std::uint8_t>(data.size()), 0, ext};
    put32(p, addr);
    p.insert(p.end(), data.begin(), data.end());
    cmd(p); // SHORT_DOWNLOAD
    return;
  }
  std::vector<std::uint8_t> p{0xF6, 0, 0, ext};
  put32(p, addr);
  cmd(p); // SET_MTA
  for (std::size_t k = 0; k < data.size(); k += cto - 2) {
    const std::size_t n = std::min(cto - 2, data.size() - k);
    std::vector<std::uint8_t> d{0xF0, static_cast<std::uint8_t>(n)}; // DOWNLOAD
    d.insert(d.end(), data.begin() + static_cast<std::ptrdiff_t>(k),
             data.begin() + static_cast<std::ptrdiff_t>(k + n));
    cmd(d);
  }
}

std::vector<double> Ecu::read(const Symbol &s) { return decode(s, upload(s.addr, s.ext, s.size())); }

void Ecu::write(const Symbol &s, const std::vector<double> &v)
{
  download(s.addr, s.ext, encode(s, v));
}

} // namespace krumpir
