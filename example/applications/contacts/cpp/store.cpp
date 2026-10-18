#include "store.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>

namespace contacts {

void PeopleStore::seed() {
    people_ = {
        {"p1", "林晚", "渡口书局", "编辑", "13800001001", "linwan@example.com", "杭州", "work", "目录和校样找她。", true},
        {"p2", "陈屿", "竹纸工坊", "纸样", "13800001002", "chenyu@example.com", "富阳", "work", "", false},
        {"p3", "赵岑", "灯塔印刷", "排版", "13800001003", "zhaocen@example.com", "宁波", "work", "夜航表在他那儿。", false},
        {"p4", "周宁", "", "作者", "13800001004", "zhouning@example.com", "绍兴", "friend", "渡口手记、灯塔摘录。", true},
        {"p5", "何清", "旧书店", "店主", "13800001005", "heqing@example.com", "杭州", "friend", "", false},
        {"p6", "沈母", "", "", "13800001006", "", "嘉兴", "family", "周末通常在家。", true},
        {"p7", "阿禾", "", "", "13800001007", "", "杭州", "family", "", false},
        {"p8", "邮差老吴", "渡口邮局", "", "13800001008", "", "杭州", "other", "包裹放传达室。", false},
    };
}

std::string PeopleStore::uid() {
    return "p" + std::to_string(++seq_) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

const Person* PeopleStore::find(const std::string& id) const {
    for (const Person& person : people_) {
        if (person.id == id) {
            return &person;
        }
    }
    return nullptr;
}

Person* PeopleStore::find(const std::string& id) {
    for (Person& person : people_) {
        if (person.id == id) {
            return &person;
        }
    }
    return nullptr;
}

bool PeopleStore::save(const std::filesystem::path& file) const {
    nlohmann::json out = nlohmann::json::array();
    for (const Person& person : people_) {
        out.push_back({{"id", person.id},
                       {"name", person.name},
                       {"org", person.org},
                       {"title", person.title},
                       {"phone", person.phone},
                       {"email", person.email},
                       {"city", person.city},
                       {"group", person.group},
                       {"note", person.note},
                       {"star", person.star}});
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream file_out(file, std::ios::binary);
    if (!file_out) {
        return false;
    }
    file_out << out.dump(2);
    return static_cast<bool>(file_out);
}

LoadResult PeopleStore::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        seed();
        return LoadResult::Missing;
    }
    try {
        nlohmann::json data;
        in >> data;
        people_.clear();
        if (data.is_array()) {
            for (const auto& item : data) {
                if (!item.is_object() || !item.contains("id") || !item.contains("name")) {
                    continue;
                }
                Person person;
                person.id = item.value("id", "");
                person.name = item.value("name", "");
                person.org = item.value("org", "");
                person.title = item.value("title", "");
                person.phone = item.value("phone", "");
                person.email = item.value("email", "");
                person.city = item.value("city", "");
                person.group = item.value("group", "other");
                person.note = item.value("note", "");
                person.star = item.value("star", false);
                if (!person.id.empty() && !person.name.empty()) {
                    people_.push_back(std::move(person));
                }
            }
        }
        if (people_.empty()) {
            seed();
            return LoadResult::Seeded;
        }
        return LoadResult::Loaded;
    } catch (...) {
        seed();
        return LoadResult::Corrupt;
    }
}

}  // namespace contacts
