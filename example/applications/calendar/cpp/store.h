#ifndef CALENDAR_STORE_H
#define CALENDAR_STORE_H

#include <filesystem>
#include <string>
#include <vector>

namespace calendar {

struct Ev {
    std::string id;
    std::string title;
    std::string date;
    std::string start;
    std::string end;
    std::string place;
    std::string color;
};

enum class LoadResult { Seeded, Loaded, Corrupt };

struct LoadOutcome {
    LoadResult kind = LoadResult::Seeded;
    /// Set when `kind` is not `Loaded`: the day the sample was built around.
    std::string sample_day;
};

class EventStore {
public:
    /// Replaces the library with the five sample events and returns today's ISO date.
    std::string seed();
    LoadOutcome load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    std::string uid();

    const Ev* find(const std::string& id) const;
    std::vector<const Ev*> on_date(const std::string& iso) const;
    void erase(const std::string& id);

    std::vector<Ev>& events() { return events_; }
    const std::vector<Ev>& events() const { return events_; }

private:
    std::vector<Ev> events_;
    unsigned seq_ = 0;
};

}  // namespace calendar

#endif
