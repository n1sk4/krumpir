#include "form.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace krumpir {

namespace {

constexpr int kMaxNames = 16; // runner: CFG_MAX_PARAMS, TELEM_MAX_CH

const char *const kStimKeys[] = {"value", "t", "from", "to", "offset", "amplitude", "freq_hz"};

// Which keys each stimulus kind uses
bool uses(const std::string &kind, const std::string &key)
{
  if (kind == "constant") {
    return key == "value";
  }
  if (kind == "step") {
    return key == "t" || key == "from" || key == "to";
  }
  if (kind == "sine") {
    return key == "offset" || key == "amplitude" || key == "freq_hz";
  }
  return false;
}

std::string str(const json &j, const char *key)
{
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string num(const json &j, const char *key, const std::string &otherwise)
{
  const auto it = j.find(key);
  return it != j.end() && it->is_number() ? format_number(it->get<double>()) : otherwise;
}

bool parse_number(const std::string &s, double *out)
{
  const char *p = s.c_str();
  char *end = nullptr;
  *out = std::strtod(p, &end);
  while (end != nullptr && *end == ' ') {
    end++;
  }
  return end != p && end != nullptr && *end == '\0' && std::isfinite(*out);
}

// A typed number as TOML writes it (strtod takes forms TOML does not, e.g. ".5").
std::string toml_number(const std::string &s)
{
  double v = 0;
  return parse_number(s, &v) ? format_number(v) : s;
}

std::string quote(const std::string &s)
{
  std::string out = "\"";
  for (const unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20 || c == 0x7f) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04X", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

// A TOML key: bare when it can be, quoted otherwise (e.g. "der(x)", "a.b").
std::string key(const std::string &s)
{
  const bool bare = !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
  });
  return bare ? s : quote(s);
}

// A JSON value of the runner's description as TOML (objects as inline tables).
std::string toml_value(const json &j)
{
  if (j.is_number()) {
    return format_number(j.get<double>());
  }
  if (j.is_boolean()) {
    return j.get<bool>() ? "true" : "false";
  }
  if (j.is_string()) {
    return quote(j.get<std::string>());
  }
  std::string out = j.is_object() ? "{ " : "[";
  bool first = true;
  for (auto it = j.begin(); it != j.end(); ++it) {
    out += first ? "" : ", ";
    first = false;
    if (j.is_object()) {
      out += key(it.key()) + " = ";
    }
    out += toml_value(*it);
  }
  return out + (j.is_object() ? " }" : "]");
}

Field make(std::string key, std::string section, std::string label, Field::Kind kind,
           std::string value)
{
  Field f;
  f.key = std::move(key);
  f.section = std::move(section);
  f.label = std::move(label);
  f.kind = kind;
  f.value = f.original = std::move(value);
  return f;
}

Field flag(std::string key, std::string section, std::string label, bool on)
{
  return make(std::move(key), std::move(section), std::move(label), Field::Flag,
              on ? "true" : "false");
}

} // namespace

std::string format_number(double v)
{
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.15g", v);
  if (std::strtod(buf, nullptr) != v) {
    std::snprintf(buf, sizeof buf, "%.17g", v);
  }
  return buf;
}

Form Form::from_describe(const json &reply, const std::vector<std::string> &fmus)
{
  Form f;
  f.text = str(reply, "text");
  f.problem = str(reply, "problem");
  const auto sc_it = reply.find("scenario");
  if (sc_it == reply.end() || !sc_it->is_object()) {
    return f;
  }
  const json &sc = *sc_it;
  const json model = sc.value("model", json::object()), rt = sc.value("rt", json::object()),
             io = sc.value("io", json::object()), log = sc.value("log", json::object());

  // The FMU's variables by name, in its order
  std::vector<json> vars;
  std::map<std::string, json> by_name;
  for (const auto &v : reply.value("variables", json::array())) {
    vars.push_back(v);
    by_name[str(v, "name")] = v;
  }
  auto describe = [&](Field &x, const std::string &name) {
    const auto it = by_name.find(name);
    if (it != by_name.end()) {
      x.unit = str(it->second, "unit");
      x.hint = str(it->second, "description");
    }
  };
  auto &fs = f.fields;

  // [model]
  Field fmu = make("model.fmu", "model", "fmu", Field::Choice, str(model, "fmu"));
  fmu.choices = fmus;
  if (std::find(fmus.begin(), fmus.end(), fmu.value) == fmus.end()) {
    fmu.choices.insert(fmu.choices.begin(), fmu.value);
  }
  fs.push_back(fmu);
  Field step = make("model.step", "model", "step", Field::Number, num(model, "step", "0.001"));
  step.unit = "s";
  step.lo = 1e-9;
  fs.push_back(step);
  const bool endless = !model.contains("duration") || model["duration"].is_null();
  fs.push_back(flag("model.until_stopped", "model", "run until stopped", endless));
  Field duration = make("model.duration", "model", "duration", Field::Number,
                        num(model, "duration", ""));
  duration.unit = "s";
  duration.hint = "a whole number of steps";
  fs.push_back(duration);

  // [model.params]: the scenario's, then the FMU's other parameters at their start values
  const json params = model.value("params", json::object());
  for (auto it = params.begin(); it != params.end(); ++it) {
    Field p = make("param:" + it.key(), "model.params", it.key(), Field::Number,
                   num(params, it.key().c_str(), ""));
    describe(p, it.key());
    fs.push_back(p);
  }
  for (const auto &v : vars) {
    const std::string name = str(v, "name");
    if (str(v, "causality") != "parameter" || params.contains(name)) {
      continue;
    }
    if (str(v, "type") != "Float64") { // the runner sets Float64 only
      Field p = make("param:" + name, "model.params", name, Field::Fixed, str(v, "start"));
      p.hint = str(v, "type") + ", not settable";
      fs.push_back(p);
      continue;
    }
    Field p = make("param:" + name, "model.params", name, Field::Number, str(v, "start"));
    p.from_fmu = true;
    describe(p, name);
    fs.push_back(p);
  }

  // [rt]
  Field wait = make("rt.wait", "rt", "wait", Field::Choice, str(rt, "wait"));
  wait.choices = {"none", "sleep", "busy"};
  wait.hint = "none: as fast as it goes; sleep, busy: paced to the step";
  fs.push_back(wait);
  Field overruns = make("rt.max_overruns", "rt", "max overruns", Field::Integer,
                        num(rt, "max_overruns", "0"));
  overruns.lo = 0;
  overruns.hi = 1e9;
  overruns.hint = "late steps before the run faults";
  fs.push_back(overruns);
  const bool pin = rt.contains("cpu") && rt["cpu"].is_number();
  fs.push_back(flag("rt.pin", "rt", "pin to a cpu", pin));
  Field cpu = make("rt.cpu", "rt", "cpu", Field::Integer, num(rt, "cpu", "3"));
  cpu.lo = 0;
  cpu.hi = 255;
  fs.push_back(cpu);
  const bool fifo = rt.contains("priority") && rt["priority"].is_number();
  fs.push_back(flag("rt.fifo", "rt", "SCHED_FIFO", fifo));
  Field prio = make("rt.priority", "rt", "priority", Field::Integer, num(rt, "priority", "80"));
  prio.lo = 1;
  prio.hi = 99;
  prio.hint = "also locks the memory";
  fs.push_back(prio);

  // [io]: inputs and outputs the scenario uses (in its order), then the FMU's others
  Field backend = make("io.backend", "io", "backend", Field::Choice, str(io, "backend"));
  backend.choices = {"null"};
  fs.push_back(backend);
  std::map<std::string, json> stimuli;
  for (const auto &st : io.value("stimulus", json::array())) {
    stimuli[str(st, "input")] = st;
  }
  for (const char *dir : {"inputs", "outputs"}) {
    const bool in = std::string(dir) == "inputs";
    std::vector<std::string> names = io.value(dir, std::vector<std::string>{});
    const std::set<std::string> used(names.begin(), names.end());
    for (const auto &v : vars) {
      const std::string name = str(v, "name");
      if (str(v, "causality") == (in ? "input" : "output") && str(v, "type") == "Float64" &&
          !used.count(name)) {
        names.push_back(name);
      }
    }
    for (const auto &name : names) {
      Field u = flag((in ? "in:" : "out:") + name, in ? "io.inputs" : "io.outputs", name,
                     used.count(name) > 0);
      describe(u, name);
      fs.push_back(u);
    }
  }

  // [io.stimulus]: per input a kind and its numbers
  for (const auto &x : std::vector<Field>(fs)) {
    if (x.key.rfind("in:", 0) != 0) {
      continue;
    }
    const std::string input = x.label;
    const auto it = stimuli.find(input);
    const json st = it != stimuli.end() ? it->second : json::object();
    const bool segments = st.contains("segments");
    Field kind = make("stim:" + input, "io.stimulus", input, Field::Choice,
                      segments ? "segments" : st.value("kind", std::string("none")));
    kind.choices = {"none", "constant", "step", "sine"};
    if (segments) {
      kind.choices.insert(kind.choices.begin(), "segments");
      kind.extra = st;
      kind.hint = std::to_string(st["segments"].size()) + " segment(s), as written";
    }
    fs.push_back(kind);
    static const std::map<std::string, std::string> defaults = {
        {"value", "0"}, {"t", "0"}, {"from", "0"}, {"to", "1"},
        {"offset", "0"}, {"amplitude", "1"}, {"freq_hz", "1"}};
    for (const char *k : kStimKeys) {
      Field n = make("stim:" + input + ":" + k, "io.stimulus", std::string("  ") + k,
                     Field::Number, num(st, k, defaults.at(k)));
      n.unit = std::string(k) == "t" ? "s" : std::string(k) == "freq_hz" ? "Hz" : x.unit;
      if (std::string(k) == "t") {
        n.lo = 0;
      }
      fs.push_back(n);
    }
  }

  // [log]
  Field file = make("log.file", "log", "file", Field::Text, str(log, "file"));
  file.hint = "CSV on the runner; its folder is made";
  fs.push_back(file);
  f.check();
  return f;
}

Field *Form::find(const std::string &k)
{
  for (auto &x : fields) {
    if (x.key == k) {
      return &x;
    }
  }
  return nullptr;
}

const Field *Form::find(const std::string &k) const
{
  return const_cast<Form *>(this)->find(k);
}

void Form::check()
{
  auto on = [this](const std::string &k) {
    const Field *x = find(k);
    return x != nullptr && x->value == "true";
  };
  auto value = [this](const std::string &k) {
    const Field *x = find(k);
    return x != nullptr ? x->value : std::string();
  };
  int params = 0, inputs = 0, outputs = 0;
  for (auto &x : fields) {
    x.error.clear();
    const std::string &k = x.key;
    if (k == "model.duration") {
      x.shown = !on("model.until_stopped");
    } else if (k == "rt.max_overruns") {
      x.shown = value("rt.wait") != "none";
    } else if (k == "rt.cpu") {
      x.shown = on("rt.pin");
    } else if (k == "rt.priority") {
      x.shown = on("rt.fifo");
    } else if (k.rfind("stim:", 0) == 0) {
      const auto colon = k.find(':', 5);
      const std::string input = k.substr(5, colon == std::string::npos ? colon : colon - 5);
      x.shown = on("in:" + input) &&
                (colon == std::string::npos || uses(value("stim:" + input), k.substr(colon + 1)));
    }
    if (!x.shown) {
      continue;
    }

    double v = 0;
    if (x.kind == Field::Number || x.kind == Field::Integer) {
      if (!parse_number(x.value, &v)) {
        x.error = "not a number";
      } else if (x.kind == Field::Integer && v != std::floor(v)) {
        x.error = "a whole number";
      } else if (v < x.lo || v > x.hi) {
        x.error = std::isinf(x.hi) ? "at least " + format_number(x.lo)
                                   : format_number(x.lo) + ".." + format_number(x.hi);
      }
    } else if ((x.kind == Field::Text || x.kind == Field::Choice) && x.value.empty()) {
      x.error = "empty";
    }
    if (!x.error.empty()) {
      continue;
    }
    if (k == "model.duration") {
      double step = 0;
      if (parse_number(value("model.step"), &step) && step > 0) {
        const double n = std::round(v / step);
        if (v < step) {
          x.error = "at least one step";
        } else if (std::fabs(n * step - v) > 1e-6 * step) {
          x.error = "a whole number of steps";
        }
      }
    } else if (k.size() > 8 && k.compare(k.size() - 8, 8, ":freq_hz") == 0 && v <= 0) {
      x.error = "more than 0";
    } else if (k.rfind("param:", 0) == 0 && x.kind == Field::Number &&
               (!x.from_fmu || x.value != x.original) && ++params > kMaxNames) {
      x.error = "more than 16 parameters";
    } else if (k.rfind("in:", 0) == 0 && on(k) && ++inputs > kMaxNames) {
      x.error = "more than 16 inputs";
    } else if (k.rfind("out:", 0) == 0 && on(k) && ++outputs > kMaxNames) {
      x.error = "more than 16 outputs";
    }
  }
}

bool Form::ok() const
{
  return std::none_of(fields.begin(), fields.end(),
                      [](const Field &x) { return x.shown && !x.error.empty(); });
}

bool Form::dirty() const
{
  return std::any_of(fields.begin(), fields.end(),
                     [](const Field &x) { return x.value != x.original; });
}

std::string Form::to_toml() const
{
  auto value = [this](const std::string &k) {
    const Field *x = find(k);
    return x != nullptr ? x->value : std::string();
  };
  auto number = [&](const std::string &k) { return toml_number(value(k)); };
  std::ostringstream out;
  out << "[model]\nfmu = " << quote(value("model.fmu")) << "\nstep = " << number("model.step")
      << "\n";
  if (value("model.until_stopped") != "true") {
    out << "duration = " << number("model.duration") << "\n";
  }
  std::string params;
  for (const auto &x : fields) {
    if (x.key.rfind("param:", 0) == 0 && x.kind == Field::Number &&
        (!x.from_fmu || x.value != x.original)) {
      params += key(x.label) + " = " + toml_number(x.value) + "\n";
    }
  }
  if (!params.empty()) {
    out << "\n[model.params]\n" << params;
  }

  out << "\n[rt]\nwait = " << quote(value("rt.wait")) << "\n";
  if (value("rt.wait") != "none") {
    out << "max_overruns = " << number("rt.max_overruns") << "\n";
  }
  if (value("rt.pin") == "true") {
    out << "cpu = " << number("rt.cpu") << "\n";
  }
  if (value("rt.fifo") == "true") {
    out << "priority = " << number("rt.priority") << "\n";
  }

  std::string inputs, outputs, stimuli;
  for (const auto &x : fields) {
    const bool in = x.key.rfind("in:", 0) == 0, out_ = x.key.rfind("out:", 0) == 0;
    if ((in || out_) && x.value == "true") {
      std::string &list = in ? inputs : outputs;
      list += (list.empty() ? "" : ", ") + quote(x.label);
    }
    if (x.key.rfind("stim:", 0) == 0 && x.key.find(':', 5) == std::string::npos && x.shown &&
        x.value != "none") {
      std::string entry;
      if (x.value == "segments") {
        entry = toml_value(x.extra);
      } else {
        entry = "{ input = " + quote(x.label) + ", kind = " + quote(x.value);
        for (const char *k : kStimKeys) {
          if (uses(x.value, k)) {
            entry += std::string(", ") + k + " = " + number(x.key + ":" + k);
          }
        }
        entry += " }";
      }
      stimuli += "  " + entry + ",\n";
    }
  }
  out << "\n[io]\nbackend = " << quote(value("io.backend")) << "\ninputs = [" << inputs
      << "]\noutputs = [" << outputs << "]\n";
  if (!stimuli.empty()) {
    out << "stimulus = [\n" << stimuli << "]\n";
  }
  out << "\n[log]\nfile = " << quote(value("log.file")) << "\n";
  return out.str();
}

} // namespace krumpir
