#ifndef CALENDAR_LAYOUT_H
#define CALENDAR_LAYOUT_H

#include "store.h"

#include <vector>

namespace calendar {

struct Placed {
    const Ev* ev = nullptr;
    double top = 0;
    double height = 0;
    double left = 0;
    double width = 0;
};

/// Side-by-side columns for events that overlap on one day. The week board uses this;
/// the month grid does not.
std::vector<Placed> layout_day(const std::vector<const Ev*>& events);

}  // namespace calendar

#endif
