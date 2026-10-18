#ifndef CONTACTS_STORE_H
#define CONTACTS_STORE_H

#include <filesystem>
#include <string>
#include <vector>

namespace contacts {

struct Person {
    std::string id;
    std::string name;
    std::string org;
    std::string title;
    std::string phone;
    std::string email;
    std::string city;
    std::string group;
    std::string note;
    bool star = false;
};

enum class LoadResult { Missing, Seeded, Loaded, Corrupt };

class PeopleStore {
public:
    void seed();
    LoadResult load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    std::string uid();

    const Person* find(const std::string& id) const;
    Person* find(const std::string& id);

    std::vector<Person>& people() { return people_; }
    const std::vector<Person>& people() const { return people_; }

private:
    std::vector<Person> people_;
    unsigned seq_ = 0;
};

}  // namespace contacts

#endif
