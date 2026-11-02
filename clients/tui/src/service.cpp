#include "service.hpp"

#include <asio.hpp>

namespace krumpir {

using asio::ip::tcp;

struct Service::Impl {
  asio::io_context io;
  tcp::socket sock{io};
  asio::streambuf buf;
  std::chrono::milliseconds timeout{};

  // Runs the pending operation. False when it did not finish in time: the socket is then closed
  // (which cancels the operation).
  bool run()
  {
    io.restart();
    io.run_for(timeout);
    if (io.stopped()) {
      return true;
    }
    std::error_code ignored;
    sock.close(ignored);
    io.run();
    return false;
  }

  void drop()
  {
    std::error_code ignored;
    sock.close(ignored);
    buf.consume(buf.size());
  }

  [[noreturn]] void fail(const std::string &why)
  {
    drop();
    throw LinkError(why);
  }
};

Service::Service(std::chrono::milliseconds timeout) : impl_(std::make_unique<Impl>())
{
  impl_->timeout = timeout;
}

Service::~Service() = default;

bool Service::is_open() const { return impl_->sock.is_open(); }

void Service::close() { impl_->drop(); }

void Service::connect(const std::string &address)
{
  close();
  const auto colon = address.rfind(':');
  const std::string host = address.substr(0, colon);
  const std::string port = colon == std::string::npos ? "7700" : address.substr(colon + 1);
  std::error_code ec;
  tcp::resolver resolver(impl_->io);
  const auto endpoints = resolver.resolve(host, port, ec);
  if (ec) {
    throw LinkError(address + ": " + ec.message());
  }
  asio::async_connect(impl_->sock, endpoints,
                      [&](std::error_code e, const tcp::endpoint &) { ec = e; });
  if (!impl_->run()) {
    impl_->fail(address + ": timed out");
  }
  if (ec) {
    impl_->fail(address + ": " + ec.message());
  }
  impl_->sock.set_option(tcp::no_delay(true));
}

json Service::call(const std::string &op, json args)
{
  if (!is_open()) {
    throw LinkError("not connected");
  }
  args["op"] = op;
  const std::string line = args.dump() + "\n";
  std::error_code ec;
  asio::async_write(impl_->sock, asio::buffer(line), [&](std::error_code e, std::size_t) { ec = e; });
  if (!impl_->run()) {
    impl_->fail(op + ": timed out");
  }
  if (ec) {
    impl_->fail(op + ": " + ec.message());
  }

  std::size_t n = 0;
  asio::async_read_until(impl_->sock, impl_->buf, '\n', [&](std::error_code e, std::size_t k) {
    ec = e;
    n = k;
  });
  if (!impl_->run()) {
    impl_->fail(op + ": timed out");
  }
  if (ec) {
    impl_->fail(ec == asio::error::eof ? "the service closed the connection"
                                       : op + ": " + ec.message());
  }
  const auto begin = asio::buffers_begin(impl_->buf.data());
  const std::string text(begin, begin + static_cast<std::ptrdiff_t>(n));
  impl_->buf.consume(n);

  json reply = json::parse(text, nullptr, false);
  if (reply.is_discarded() || !reply.is_object()) {
    impl_->fail(op + ": not a JSON reply: " + text.substr(0, 80));
  }
  if (!reply.value("ok", false)) {
    throw ServiceError(reply.value("error", op + " failed"));
  }
  return reply;
}

} // namespace krumpir
