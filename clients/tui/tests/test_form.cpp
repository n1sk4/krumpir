// The load form: fields from a describe reply, its checks, and the scenario text it writes.
// With KT_RUNNER=host:port (a `krumpir serve` in runner/) it also round-trips every scenario of
// that runner: describe -> form -> text -> describe gives the same scenario.
#include <cstdlib>

#include <doctest/doctest.h>

#include "form.hpp"
#include "service.hpp"

using namespace krumpir;

namespace {

const json kReply = json::parse(R"({
  "ok": true, "text": "...",
  "scenario": {
    "model": {"fmu": "fmus/dcmotor.fmu", "step": 0.001, "duration": null, "params": {"Vbus": 6}},
    "rt": {"wait": "sleep", "max_overruns": 1000000, "cpu": null, "priority": null},
    "io": {"backend": "null", "inputs": ["duty"], "outputs": ["speed_rpm"],
           "stimulus": [{"input": "duty", "loops": 1,
                         "segments": [{"kind": "const", "value": 0.5, "duration": 1}]}]},
    "log": {"file": "runs/x.csv"}},
  "variables": [
    {"name": "Vbus", "type": "Float64", "causality": "parameter", "unit": "V", "start": "12",
     "description": "bus voltage"},
    {"name": "R", "type": "Float64", "causality": "parameter", "unit": "Ohm", "start": "2.5",
     "description": ""},
    {"name": "mode", "type": "Int32", "causality": "parameter", "unit": "", "start": "1",
     "description": ""},
    {"name": "duty", "type": "Float64", "causality": "input", "unit": "1", "start": "0",
     "description": ""},
    {"name": "load_torque", "type": "Float64", "causality": "input", "unit": "N.m", "start": "0",
     "description": ""},
    {"name": "speed_rpm", "type": "Float64", "causality": "output", "unit": "rpm", "start": "",
     "description": ""},
    {"name": "current", "type": "Float64", "causality": "output", "unit": "A", "start": "",
     "description": ""}]
})");

void set(Form &f, const std::string &key, const std::string &value)
{
  Field *x = f.find(key);
  REQUIRE(x != nullptr);
  x->value = value;
  f.check();
}

std::string error(const Form &f, const std::string &key)
{
  const Field *x = f.find(key);
  REQUIRE(x != nullptr);
  return x->error;
}

} // namespace

TEST_CASE("form: fields from a describe reply")
{
  Form f = Form::from_describe(kReply, {"fmus/dcmotor.fmu", "fmus/t12tip.fmu"});
  CHECK(f.problem.empty());
  CHECK(f.ok());
  CHECK_FALSE(f.dirty());
  CHECK(f.find("model.fmu")->choices.size() == 2);
  CHECK(f.find("model.until_stopped")->value == "true");
  CHECK_FALSE(f.find("model.duration")->shown);
  CHECK(f.find("param:Vbus")->value == "6");
  CHECK(f.find("param:Vbus")->unit == "V");
  CHECK(f.find("param:R")->from_fmu);
  CHECK(f.find("param:R")->value == "2.5");
  CHECK(f.find("param:mode")->kind == Field::Fixed);
  CHECK(f.find("in:duty")->value == "true");
  CHECK(f.find("in:load_torque")->value == "false");
  CHECK(f.find("stim:duty")->value == "segments");
  CHECK_FALSE(f.find("stim:load_torque")->shown); // its input is not used
  CHECK_FALSE(f.find("rt.cpu")->shown);
  CHECK(f.find("stim:duty:value")->unit == "1");

  // Unchanged: the same scenario back; FMU parameters at their start values are left out
  CHECK(f.to_toml() == "[model]\nfmu = \"fmus/dcmotor.fmu\"\nstep = 0.001\n"
                       "\n[model.params]\nVbus = 6\n"
                       "\n[rt]\nwait = \"sleep\"\nmax_overruns = 1000000\n"
                       "\n[io]\nbackend = \"null\"\ninputs = [\"duty\"]\noutputs = [\"speed_rpm\"]\n"
                       "stimulus = [\n  { input = \"duty\", loops = 1, segments = [{ duration = 1, "
                       "kind = \"const\", value = 0.5 }] },\n]\n"
                       "\n[log]\nfile = \"runs/x.csv\"\n");
}

TEST_CASE("form: edits and what they write")
{
  Form f = Form::from_describe(kReply, {"fmus/dcmotor.fmu"});
  set(f, "param:R", ".5");
  set(f, "model.until_stopped", "false");
  set(f, "model.duration", "2");
  set(f, "rt.fifo", "true");
  set(f, "in:load_torque", "true");
  set(f, "stim:load_torque", "step");
  set(f, "stim:load_torque:t", "0.5");
  set(f, "stim:duty", "constant");
  set(f, "stim:duty:value", "0.25");
  set(f, "out:speed_rpm", "false");
  CHECK(f.ok());
  CHECK(f.dirty());
  const std::string t = f.to_toml();
  CHECK(t.find("duration = 2\n") != std::string::npos);
  CHECK(t.find("R = 0.5\n") != std::string::npos); // written as TOML takes it
  CHECK(t.find("priority = 80\n") != std::string::npos);
  CHECK(t.find("inputs = [\"duty\", \"load_torque\"]") != std::string::npos);
  CHECK(t.find("outputs = []") != std::string::npos);
  CHECK(t.find("{ input = \"duty\", kind = \"constant\", value = 0.25 }") != std::string::npos);
  CHECK(t.find("{ input = \"load_torque\", kind = \"step\", t = 0.5, from = 0, to = 1 }") !=
        std::string::npos);
  CHECK(t.find("segments") == std::string::npos);

  // An unused input drops its stimulus
  set(f, "in:load_torque", "false");
  CHECK(f.to_toml().find("load_torque") == std::string::npos);
}

TEST_CASE("form: checks")
{
  Form f = Form::from_describe(kReply, {});
  set(f, "model.step", "abc");
  CHECK(error(f, "model.step") == "not a number");
  set(f, "model.step", "0");
  CHECK(error(f, "model.step") == "at least 1e-09");
  set(f, "model.step", "0.001");
  set(f, "model.until_stopped", "false");
  set(f, "model.duration", "0.0015");
  CHECK(error(f, "model.duration") == "a whole number of steps");
  set(f, "model.duration", "0.0001");
  CHECK(error(f, "model.duration") == "at least one step");
  set(f, "model.duration", "1");
  CHECK(f.ok());
  set(f, "rt.fifo", "true");
  set(f, "rt.priority", "0");
  CHECK(error(f, "rt.priority") == "1..99");
  set(f, "rt.priority", "1.5");
  CHECK(error(f, "rt.priority") == "a whole number");
  set(f, "rt.fifo", "false"); // hidden: its error does not count
  CHECK(f.ok());
  set(f, "rt.wait", "none");
  set(f, "rt.max_overruns", "-1");
  CHECK(f.ok());
  set(f, "stim:duty", "sine");
  set(f, "stim:duty:freq_hz", "0");
  CHECK(error(f, "stim:duty:freq_hz") == "more than 0");
  set(f, "stim:duty:freq_hz", "2");
  set(f, "log.file", "");
  CHECK(error(f, "log.file") == "empty");
  CHECK_FALSE(f.ok());
}

TEST_CASE("form: at most 16 inputs and parameters")
{
  json reply = kReply;
  for (int i = 0; i < 17; i++) {
    reply["variables"].push_back({{"name", "u" + std::to_string(i)}, {"type", "Float64"},
                                  {"causality", "input"}});
    reply["variables"].push_back({{"name", "p" + std::to_string(i)}, {"type", "Float64"},
                                  {"causality", "parameter"}, {"start", "1"}});
  }
  Form f = Form::from_describe(reply, {});
  CHECK(f.ok()); // parameters at their start values are not written
  for (int i = 0; i < 16; i++) {
    set(f, "in:u" + std::to_string(i), "true");
    set(f, "param:p" + std::to_string(i), "2");
  }
  CHECK(error(f, "in:u15") == "more than 16 inputs"); // duty + 15
  CHECK(error(f, "param:p15") == "more than 16 parameters"); // Vbus + 15
}

TEST_CASE("form: the uart backend's keys")
{
  Form f = Form::from_describe(kReply, {});
  CHECK_FALSE(f.find("io.device")->shown);
  set(f, "io.backend", "uart");
  CHECK(error(f, "io.device") == "empty");
  set(f, "io.device", "/dev/pts/3");
  set(f, "io.timeout_ms", "0");
  set(f, "io.late", "hold");
  CHECK(f.ok());
  CHECK(f.to_toml().find("backend = \"uart\"\ndevice = \"/dev/pts/3\"\nbaud = 921600\n"
                         "timeout_ms = 0\nlate = \"hold\"\nmax_late = 10\n") != std::string::npos);
}

TEST_CASE("form: a reply without a scenario")
{
  Form f = Form::from_describe(json{{"text", "[model]\n"}, {"problem", "missing [io]"}}, {});
  CHECK(f.fields.empty());
  CHECK(f.text == "[model]\n");
  CHECK(f.problem == "missing [io]");
}

TEST_CASE("form: round trip through a runner (KT_RUNNER)")
{
  const char *address = std::getenv("KT_RUNNER");
  if (address == nullptr) {
    return;
  }
  Service svc;
  svc.connect(address);
  const json files = svc.call("files");
  const auto fmus = files["fmus"].get<std::vector<std::string>>();
  for (const auto &path : files["scenarios"]) {
    CAPTURE(path);
    const json d = svc.call("describe", {{"scenario", path}});
    const Form f = Form::from_describe(d, fmus);
    REQUIRE(f.problem.empty());
    CHECK(f.ok());
    const json again = svc.call("describe", {{"toml", f.to_toml()}});
    CAPTURE(f.to_toml());
    CHECK(again.value("problem", "") == "");
    CHECK(again["scenario"] == d["scenario"]);
    svc.call("check", {{"toml", f.to_toml()}});
  }
}
