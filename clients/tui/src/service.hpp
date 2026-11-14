#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace krumpir
{

using json = nlohmann::json;

// The service refused a request (its reply had "ok": false). The connection is fine, except
// for "busy": another client is connected, and the service has closed it. code: the kind of
// refusal (bad_state, not_found, busy, ...), "" from a service that sends none.
struct ServiceError : std::runtime_error {
  explicit ServiceError(const std::string &what, std::string code = {})
      : std::runtime_error(what), code(std::move(code))
  {
  }
  std::string code;
};

// The connection failed or timed out. It has been closed.
struct LinkError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// A blocking client of `krumpir serve`: one JSON object per line each way, one request at a
// time (runner/docs/service-protocol.md). Every call is bounded by the timeout.
class Service
{
public:
  explicit Service(std::chrono::milliseconds timeout = std::chrono::seconds(5));
  ~Service();
  Service(const Service &) = delete;
  Service &operator=(const Service &) = delete;

  // "host:port", or "host" for the default port 7700.
  void connect(const std::string &address);
  void close();
  bool is_open() const;

  // Sends {"op": op, ...args} and returns the reply. Throws ServiceError or LinkError.
  json call(const std::string &op, json args = json::object());

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace krumpir
