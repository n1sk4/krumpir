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
  std::size_t max_cto = 8;
  std::string address;

  void drop()
  {
    std::error_code ignored;
    sock.close(ignored);
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
  std::uint8_t buf[2048];
  for (;;) { // skips events, service requests and DAQ packets until the answer
    std::size_t got = 0;
    bool done = false;
    m.sock.async_receive(asio::buffer(buf), [&](std::error_code e, std::size_t n) {
      ec = e;
      got = n;
      done = true;
    });
    m.io.restart();
    m.io.run_until(deadline);
    if (!done) {
      m.drop(); // cancels the receive
      m.io.restart();
      m.io.run();
      char what[64];
      std::snprintf(what, sizeof what, ": no answer (command 0x%02X)", packet[0]);
      throw LinkError(m.address + what);
    }
    if (ec) {
      m.drop();
      throw LinkError(m.address + ": " + ec.message());
    }
    for (std::size_t pos = 0; pos + 4 <= got;) { // one datagram may carry several packets
      const std::size_t n = static_cast<std::size_t>(buf[pos] | (buf[pos + 1] << 8));
      const std::uint8_t *pkt = buf + pos + 4;
      const std::size_t len = std::min(n, got - pos - 4);
      pos += 4 + n;
      if (len >= 1 && pkt[0] == 0xFF) {
        return {pkt, pkt + len};
      }
      if (len >= 1 && pkt[0] == 0xFE) {
        const int code = len > 1 ? pkt[1] : 0;
        const auto it = kErrors.find(code);
        char what[64];
        std::snprintf(what, sizeof what, "command 0x%02X refused: ", packet[0]);
        throw XcpError(what + (it != kErrors.end() ? std::string(it->second)
                                                   : "error " + std::to_string(code)));
      }
    }
  }
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
