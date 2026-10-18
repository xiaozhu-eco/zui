#include "layout.h"

#include "dates.h"

#include <algorithm>

namespace calendar {

std::vector<Placed> layout_day(const std::vector<const Ev*>& events) {
    struct Item {
        const Ev* ev;
        int start;
        int end;
        int cluster;
        int col;
    };
    std::vector<Item> items;
    for (const Ev* ev : events) {
        const int start = minutes_of(ev->start);
        const int end = minutes_of(ev->end);
        if (start < 0 || end <= start) {
            continue;
        }
        items.push_back({ev, start, end, 0, 0});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.start < b.start || (a.start == b.start && a.end < b.end);
    });
    std::vector<int> col_ends;
    int cluster = -1;
    int cluster_end = -1;
    for (Item& item : items) {
        if (cluster < 0 || item.start >= cluster_end) {
            ++cluster;
            cluster_end = item.end;
        } else if (item.end > cluster_end) {
            cluster_end = item.end;
        }
        item.cluster = cluster;
        int col = -1;
        for (int i = 0; i < static_cast<int>(col_ends.size()); ++i) {
            if (col_ends[static_cast<std::size_t>(i)] <= item.start) {
                col = i;
                break;
            }
        }
        if (col < 0) {
            col = static_cast<int>(col_ends.size());
            col_ends.push_back(item.end);
        } else {
            col_ends[static_cast<std::size_t>(col)] = item.end;
        }
        item.col = col;
    }
    std::vector<int> widths;
    for (const Item& item : items) {
        if (static_cast<int>(widths.size()) <= item.cluster) {
            widths.resize(static_cast<std::size_t>(item.cluster) + 1);
        }
        widths[static_cast<std::size_t>(item.cluster)] =
            std::max(widths[static_cast<std::size_t>(item.cluster)], item.col + 1);
    }
    std::vector<Placed> out;
    for (const Item& item : items) {
        const int cols = std::max(1, widths[static_cast<std::size_t>(item.cluster)]);
        const double height = std::max(22.0, (item.end - item.start) / 60.0 * 44.0);
        out.push_back(
            {item.ev, (item.start / 60.0) * 44.0, height, (item.col * 100.0) / cols, 100.0 / cols});
    }
    return out;
}

}  // namespace calendar
