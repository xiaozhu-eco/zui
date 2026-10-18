#include "filter.h"

namespace contacts {
namespace {

void fold_ascii(std::string& text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
}

}  // namespace

bool GroupFilter::accept(const Person& person) const {
    return group_ == "all" || person.group == group_;
}

bool StarFilter::accept(const Person& person) const { return !only_ || person.star; }

bool TextFilter::accept(const Person& person) const {
    if (query_.empty()) {
        return true;
    }
    std::string hay = person.name + "\n" + person.org + "\n" + person.title + "\n" + person.phone + "\n" +
                      person.email + "\n" + person.city + "\n" + person.note;
    std::string needle = query_;
    fold_ascii(hay);
    fold_ascii(needle);
    return hay.find(needle) != std::string::npos;
}

bool FilterChain::accept(const Person& person) const {
    return group_.accept(person) && star_.accept(person) && text_.accept(person);
}

}  // namespace contacts
