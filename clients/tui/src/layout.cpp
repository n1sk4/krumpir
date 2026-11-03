#include "layout.hpp"

#include <algorithm>

namespace krumpir {

int ClampSplit(int wanted, int total, int min_left, int min_right)
{
  if (total <= 0) {
    return std::max(wanted, min_left);
  }
  const int most = std::max(min_left, total - 1 - min_right); // 1: the divider
  return std::clamp(wanted, min_left, most);
}

int NameWidth(int pane_width, int other_columns)
{
  return std::clamp(pane_width - 4 - other_columns, 8, 40);
}

} // namespace krumpir
