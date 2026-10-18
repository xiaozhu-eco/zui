#ifndef CONTACTS_FILTER_H
#define CONTACTS_FILTER_H

#include "store.h"

#include <string>

namespace contacts {

/// One question about a person. The list composes a group rule, a star rule, and a text rule.
class PersonFilter {
public:
    virtual ~PersonFilter() = default;
    virtual bool accept(const Person& person) const = 0;
};

class GroupFilter : public PersonFilter {
public:
    explicit GroupFilter(std::string group) : group_(std::move(group)) {}
    bool accept(const Person& person) const override;

private:
    std::string group_;
};

class StarFilter : public PersonFilter {
public:
    explicit StarFilter(bool only) : only_(only) {}
    bool accept(const Person& person) const override;

private:
    bool only_ = false;
};

class TextFilter : public PersonFilter {
public:
    explicit TextFilter(std::string query) : query_(std::move(query)) {}
    bool accept(const Person& person) const override;

private:
    std::string query_;
};

class FilterChain : public PersonFilter {
public:
    FilterChain(const PersonFilter& group, const PersonFilter& star, const PersonFilter& text)
        : group_(group), star_(star), text_(text) {}
    bool accept(const Person& person) const override;

private:
    const PersonFilter& group_;
    const PersonFilter& star_;
    const PersonFilter& text_;
};

}  // namespace contacts

#endif
