#include "a2l.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <variant>
#include <vector>

namespace krumpir {

namespace {

const std::map<std::string, char> kTypes{
    {"UBYTE", 'B'}, {"SBYTE", 'b'}, {"UWORD", 'H'}, {"SWORD", 'h'}, {"ULONG", 'I'},
    {"SLONG", 'i'}, {"A_UINT64", 'Q'}, {"A_INT64", 'q'}, {"FLOAT32_IEEE", 'f'},
    {"FLOAT64_IEEE", 'd'}};

struct Block {
  std::string kind;
  std::vector<std::string> args; // the loose tokens of the block, quotes kept
  std::vector<Block> children;
};

bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::vector<std::string> tokens(const std::string &text)
{
  std::vector<std::string> out;
  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    const char c = text[i];
    if (space(c)) {
      i++;
    } else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
      const std::size_t e = text.find("*/", i + 2);
      i = e == std::string::npos ? n : e + 2;
    } else if (c == '"') {
      std::size_t j = i + 1;
      while (j < n && text[j] != '"') {
        j += text[j] == '\\' ? 2 : 1;
      }
      j = std::min(j, n - 1);
      out.push_back(text.substr(i, j + 1 - i));
      i = j + 1;
    } else {
      std::size_t j = i;
      while (j < n && !space(text[j])) {
        j++;
      }
      out.push_back(text.substr(i, j - i));
      i = j;
    }
  }
  return out;
}

// /begin KIND args... children... /end KIND, from tokens[i] on, until the enclosing /end
std::vector<Block> blocks(const std::vector<std::string> &t, std::size_t &i,
                          std::vector<std::string> *loose)
{
  std::vector<Block> out;
  while (i < t.size()) {
    if (t[i] == "/end") {
      i += 2;
      return out;
    }
    if (t[i] == "/begin" && i + 1 < t.size()) {
      Block b;
      b.kind = t[i + 1];
      i += 2;
      b.children = blocks(t, i, &b.args);
      out.push_back(std::move(b));
    } else {
      if (loose != nullptr) {
        loose->push_back(t[i]);
      }
      i++;
    }
  }
  return out;
}

void walk(const std::vector<Block> &bs, std::vector<const Block *> &out)
{
  for (const auto &b : bs) {
    out.push_back(&b);
    walk(b.children, out);
  }
}

std::string unquote(const std::string &s)
{
  return s.size() >= 2 && s.front() == '"' ? s.substr(1, s.size() - 2) : s;
}

const std::string *keyword(const std::vector<std::string> &a, const char *key)
{
  for (std::size_t k = 0; k + 1 < a.size(); k++) {
    if (a[k] == key) {
      return &a[k + 1];
    }
  }
  return nullptr;
}

long long to_int(const std::string &s) { return std::stoll(s, nullptr, 0); }

// The event of IF_DATA XCP / DAQ_EVENT / DEFAULT_EVENT_LIST EVENT n under b, -1 when none.
int default_event(const Block &b)
{
  for (const auto &c : b.children) {
    if (c.kind == "DEFAULT_EVENT_LIST") {
      const auto *e = keyword(c.args, "EVENT");
      return e != nullptr ? static_cast<int>(to_int(*e)) : -1;
    }
    if (const int e = default_event(c); e >= 0) {
      return e;
    }
  }
  return -1;
}

double to_num(const std::vector<std::string> &a, std::size_t k)
{
  return k < a.size() ? std::strtod(a[k].c_str(), nullptr) : 0.0;
}

std::size_t dims(const std::vector<std::string> &a)
{
  std::size_t n = 1;
  for (std::size_t k = 0; k < a.size(); k++) {
    if (a[k] == "MATRIX_DIM") {
      for (k++; k < a.size() && std::isdigit(static_cast<unsigned char>(a[k][0])); k++) {
        n *= static_cast<std::size_t>(std::stoul(a[k]));
      }
      break;
    }
  }
  return n;
}

struct Conv {
  std::string unit;
  std::map<long long, std::string> names;
};

struct Component {
  std::string name, type;
  std::uint32_t offset;
  std::size_t count;
};
// A scalar typedef (no address yet) or a struct.
using Type = std::variant<Symbol, std::vector<Component>>;

} // namespace

std::size_t Symbol::elem_size() const
{
  switch (fmt) {
  case 'B':
  case 'b':
    return 1;
  case 'H':
  case 'h':
    return 2;
  case 'I':
  case 'i':
  case 'f':
    return 4;
  default:
    return 8;
  }
}

const Symbol &A2l::at(const std::string &name) const
{
  const auto it = symbols.find(name);
  if (it == symbols.end()) {
    throw A2lError("'" + name + "' is not in the A2L");
  }
  return it->second;
}

A2l parse_a2l(const std::string &text)
{
  const auto t = tokens(text);
  std::size_t i = 0;
  const auto tree = blocks(t, i, nullptr);
  std::vector<const Block *> all;
  walk(tree, all);

  A2l a;
  std::map<std::string, char> layouts;
  std::map<std::string, std::map<long long, std::string>> vtabs;
  for (const Block *b : all) {
    const auto &g = b->args;
    if (b->kind == "MOD_PAR") {
      if (const auto *e = keyword(g, "EPK")) {
        a.epk = unquote(*e);
      }
      if (const auto *e = keyword(g, "ADDR_EPK")) {
        a.epk_addr = static_cast<std::uint32_t>(to_int(*e));
      }
    } else if (b->kind == "XCP_ON_UDP_IP" && g.size() > 1) {
      a.port = static_cast<unsigned>(to_int(g[1]));
      if (const auto *h = keyword(g, "ADDRESS")) {
        a.host = unquote(*h);
      }
    } else if (b->kind == "EVENT" && g.size() > 2) { // "name" "short name" channel ...
      a.events[static_cast<int>(to_int(g[2]))] = unquote(g[0]);
    } else if (b->kind == "RECORD_LAYOUT" && g.size() > 3 && kTypes.count(g[3])) {
      layouts[g[0]] = kTypes.at(g[3]);
    } else if (b->kind == "COMPU_VTAB" && g.size() > 3) {
      auto &m = vtabs[g[0]];
      for (std::size_t k = 4; k + 1 < g.size(); k += 2) {
        m[to_int(g[k])] = unquote(g[k + 1]);
      }
    }
  }
  std::map<std::string, Conv> convs;
  for (const Block *b : all) {
    const auto &g = b->args;
    if (b->kind == "COMPU_METHOD" && g.size() > 4) {
      Conv c{unquote(g[4]), {}};
      if (const auto *ref = keyword(g, "COMPU_TAB_REF"); ref && vtabs.count(*ref)) {
        c.names = vtabs.at(*ref);
      }
      convs[g[0]] = std::move(c);
    }
  }

  // Format from the caller; count, conversion (args[conv]), limits (args[lo], args[lo + 1]) and
  // PHYS_UNIT from the args.
  auto scalar = [&](char fmt, const std::vector<std::string> &g, std::size_t conv, std::size_t lo) {
    Symbol s;
    s.fmt = fmt;
    s.count = dims(g);
    s.min = to_num(g, lo);
    s.max = to_num(g, lo + 1);
    if (conv < g.size() && convs.count(g[conv])) {
      s.unit = convs.at(g[conv]).unit;
      s.names = convs.at(g[conv]).names;
    }
    if (const auto *u = keyword(g, "PHYS_UNIT")) {
      s.unit = unquote(*u);
    }
    return s;
  };

  std::map<std::string, Type> types;
  for (const Block *b : all) {
    const auto &g = b->args;
    if (b->kind == "TYPEDEF_MEASUREMENT" && g.size() > 7 && kTypes.count(g[2])) {
      types[g[0]] = scalar(kTypes.at(g[2]), g, 3, 6);
    } else if (b->kind == "TYPEDEF_CHARACTERISTIC" && g.size() > 7 && layouts.count(g[3])) {
      Symbol s = scalar(layouts.at(g[3]), g, 5, 6);
      s.writable = true;
      types[g[0]] = s;
    } else if (b->kind == "TYPEDEF_STRUCTURE" && !g.empty()) {
      std::vector<Component> comps;
      for (const auto &c : b->children) {
        if (c.kind == "STRUCTURE_COMPONENT" && c.args.size() > 2) {
          comps.push_back({c.args[0], c.args[1], static_cast<std::uint32_t>(to_int(c.args[2])),
                           dims(c.args)});
        }
      }
      types[g[0]] = std::move(comps);
    }
  }

  // ponytail: arrays of structs are not flattened (xcp.py does the same)
  std::function<void(const std::string &, const std::string &, std::uint32_t, std::uint8_t,
                     std::size_t, int, int)>
      add = [&](const std::string &name, const std::string &type, std::uint32_t addr,
                std::uint8_t ext, std::size_t count, int event, int depth) {
        const auto it = types.find(type);
        if (it == types.end() || depth > 16) {
          return;
        }
        if (const auto *s = std::get_if<Symbol>(&it->second)) {
          Symbol v = *s;
          v.addr = addr;
          v.ext = ext;
          v.count *= count;
          v.event = event;
          a.symbols[name] = std::move(v);
        } else if (count == 1) {
          for (const auto &c : std::get<std::vector<Component>>(it->second)) {
            add(name + "." + c.name, c.type, addr + c.offset, ext, c.count, event, depth + 1);
          }
        }
      };

  for (const Block *b : all) {
    const auto &g = b->args;
    const auto *x = keyword(g, "ECU_ADDRESS_EXTENSION");
    const auto ext = static_cast<std::uint8_t>(x ? to_int(*x) : 0);
    if (b->kind == "MEASUREMENT" && g.size() > 7 && kTypes.count(g[2])) {
      Symbol s = scalar(kTypes.at(g[2]), g, 3, 6);
      const auto *addr = keyword(g, "ECU_ADDRESS");
      s.addr = addr ? static_cast<std::uint32_t>(to_int(*addr)) : 0;
      s.ext = ext;
      s.event = default_event(*b);
      a.symbols[g[0]] = std::move(s);
    } else if (b->kind == "CHARACTERISTIC" && g.size() > 8 && layouts.count(g[4])) {
      Symbol s = scalar(layouts.at(g[4]), g, 6, 7);
      s.addr = static_cast<std::uint32_t>(to_int(g[3]));
      s.ext = ext;
      s.writable = true;
      a.symbols[g[0]] = std::move(s);
    } else if (b->kind == "INSTANCE" && g.size() > 3) {
      add(g[0], g[2], static_cast<std::uint32_t>(to_int(g[3])), ext, dims(g), default_event(*b), 0);
    }
  }
  return a;
}

A2l load_a2l(const std::string &path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw A2lError("cannot read " + path);
  }
  std::ostringstream s;
  s << f.rdbuf();
  try {
    return parse_a2l(s.str());
  } catch (const std::logic_error &e) { // stoll/stoul on a malformed number
    throw A2lError(path + ": not a valid A2L (" + e.what() + ")");
  }
}

} // namespace krumpir
