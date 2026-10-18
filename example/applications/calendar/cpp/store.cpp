#include "store.h"

#include "dates.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <fstream>

namespace calendar {

std::string EventStore::seed() {
    const std::tm today = today_tm();
    const std::string day = iso_from(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
    events_ = {
        {"e1", "校对第三章", day, "10:00", "11:30", "渡口书局", "green"},
        {"e2", "和林晚对目录", day, "10:30", "12:00", "二楼", "clay"},
        {"e3", "还书", add_days(day, 1), "15:00", "15:30", "灯塔印刷", "sea"},
        {"e4", "看纸样", add_days(day, 2), "09:00", "10:00", "竹纸工坊", "gold"},
        {"e5", "夜校", add_days(day, -1), "19:00", "20:30", "", "green"},
    };
    return day;
}

std::string EventStore::uid() {
    return "e" + std::to_string(std::time(nullptr)) + "-" + std::to_string(++seq_);
}

const Ev* EventStore::find(const std::string& id) const {
    for (const Ev& ev : events_) {
        if (ev.id == id) {
            return &ev;
        }
    }
    return nullptr;
}

std::vector<const Ev*> EventStore::on_date(const std::string& iso) const {
    std::vector<const Ev*> rows;
    for (const Ev& ev : events_) {
        if (ev.date == iso) {
            rows.push_back(&ev);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Ev* a, const Ev* b) { return a->start < b->start; });
    return rows;
}

void EventStore::erase(const std::string& id) {
    events_.erase(std::remove_if(events_.begin(), events_.end(),
                                 [&](const Ev& ev) { return ev.id == id; }),
                  events_.end());
}

bool EventStore::save(const std::filesystem::path& file) const {
    nlohmann::json rows = nlohmann::json::array();
    for (const Ev& ev : events_) {
        rows.push_back({{"id", ev.id},
                        {"title", ev.title},
                        {"date", ev.date},
                        {"start", ev.start},
                        {"end", ev.end},
                        {"place", ev.place},
                        {"color", ev.color}});
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        return false;
    }
    out << rows.dump(2);
    return static_cast<bool>(out);
}

LoadOutcome EventStore::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {LoadResult::Seeded, seed()};
    }
    try {
        nlohmann::json data;
        in >> data;
        events_.clear();
        for (const auto& item : data) {
            Ev ev;
            ev.id = item.value("id", "");
            ev.title = item.value("title", "");
            ev.date = item.value("date", "");
            ev.start = item.value("start", "");
            ev.end = item.value("end", "");
            ev.place = item.value("place", "");
            ev.color = item.value("color", "green");
            if (!ev.id.empty() && !ev.date.empty() && !ev.title.empty()) {
                events_.push_back(std::move(ev));
            }
        }
        if (events_.empty()) {
            return {LoadResult::Seeded, seed()};
        }
        return {LoadResult::Loaded, {}};
    } catch (...) {
        return {LoadResult::Corrupt, seed()};
    }
}

}  // namespace calendar
