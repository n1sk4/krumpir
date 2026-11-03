#pragma once

#include <cmath>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace krumpir {

using json = nlohmann::json;

// One editable value of a scenario.
struct Field {
  enum Kind { Number, Integer, Text, Choice, Flag, Fixed }; // Fixed: shown, not editable
  // key: "model.step", "param:<name>", "rt.wait", "in:<name>", "out:<name>", "stim:<input>"
  // (the kind), "stim:<input>:<key>", "log.file", ...
  std::string key, section, label, unit, hint;
  Kind kind = Number;
  std::string value, original; // Flag: "true" / "false"
  std::vector<std::string> choices;
  double lo = -INFINITY, hi = INFINITY; // Number, Integer: allowed range
  bool from_fmu = false; // a parameter not in the scenario: shown at the FMU's start value
  bool shown = true;     // false while its switch is off (not part of the scenario then)
  std::string error;     // set by Form::check
  json extra;            // stim: the segment stimulus as the runner described it
};

// A scenario as fields, from the runner's `describe` reply, and back to scenario text.
// Without a "scenario" in the reply (the text is not a valid scenario) it has no fields; `text`
// and `problem` say what there is.
struct Form {
  std::string text;    // the scenario text as described
  std::string problem; // why the text or its FMU could not be read ("" when fine)
  std::vector<Field> fields;

  // fmus: the runner's FMU files, the choices for model.fmu.
  static Form from_describe(const json &reply, const std::vector<std::string> &fmus);

  Field *find(const std::string &key);
  const Field *find(const std::string &key) const;
  // Shows and hides fields by their switches and sets each shown field's error.
  void check();
  bool ok() const;    // no shown field has an error (after check)
  bool dirty() const; // a value differs from the one it started with
  std::string to_toml() const;
};

// The shortest of %.15g / %.17g that reads back exactly.
std::string format_number(double v);

} // namespace krumpir
