#include "store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <utility>

namespace kanban {
namespace {

bool contains(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

std::string trim(std::string_view in) {
    std::size_t begin = 0;
    while (begin < in.size() &&
           (in[begin] == ' ' || in[begin] == '\t' || in[begin] == '\n' || in[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = in.size();
    while (end > begin &&
           (in[end - 1] == ' ' || in[end - 1] == '\t' || in[end - 1] == '\n' || in[end - 1] == '\r')) {
        --end;
    }
    return std::string(in.substr(begin, end - begin));
}

void BoardStore::seed() {
    columns_ = {{"todo", "待办", {"c1", "c2", "c3"}},
                {"doing", "进行中", {"c4", "c5"}},
                {"review", "待校对", {"c6", "c7"}},
                {"done", "完成", {"c8"}},
                {"later", "搁置", {"c9"}}};
    cards_ = {{"c1", "校对第三章", "第二节标题和那张被拆开的表。明天上午和林晚对目录。"},
              {"c2", "人名索引留白", "等周宁补注，先不要上墨。"},
              {"c3", "还夜航表", "灯塔印刷有人在等，周三下午之前。"},
              {"c4", "灯塔摘录封面", "色号用上次的赭石，别换新样。"},
              {"c5", "竹纸样", "毛边留下，不裁。"},
              {"c6", "渡口手记前言", "改过一轮，等林晚用铅笔批。"},
              {"c7", "海岸植物续借", "书局说这本可以续，夜航表不行。"},
              {"c8", "旧书店目录归档", "何清已经对过册数。"},
              {"c9", "潮汐口诀再版", "这季先不动。"}};
}

LoadResult BoardStore::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        seed();
        return LoadResult::Seeded;
    }
    try {
        nlohmann::json data;
        in >> data;
        columns_.clear();
        cards_.clear();
        for (const auto& item : data.at("columns")) {
            Column column;
            column.id = item.value("id", "");
            column.name = item.value("name", "");
            for (const auto& id : item.at("cardIds")) {
                column.card_ids.push_back(id.get<std::string>());
            }
            if (!column.id.empty()) {
                columns_.push_back(std::move(column));
            }
        }
        for (const auto& item : data.at("cards")) {
            Card card;
            card.id = item.value("id", "");
            card.title = item.value("title", "");
            card.note = item.value("note", "");
            if (!card.id.empty()) {
                cards_.push_back(std::move(card));
            }
        }
        if (columns_.empty()) {
            seed();
            return LoadResult::Seeded;
        }
        return LoadResult::Loaded;
    } catch (...) {
        seed();
        return LoadResult::Corrupt;
    }
}

bool BoardStore::save(const std::filesystem::path& file) const {
    nlohmann::json columns = nlohmann::json::array();
    for (const Column& column : columns_) {
        columns.push_back({{"id", column.id}, {"name", column.name}, {"cardIds", column.card_ids}});
    }
    nlohmann::json cards = nlohmann::json::array();
    for (const Card& card : cards_) {
        cards.push_back({{"id", card.id}, {"title", card.title}, {"note", card.note}});
    }
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        return false;
    }
    out << nlohmann::json{{"columns", columns}, {"cards", cards}}.dump(2);
    return static_cast<bool>(out);
}

Card* BoardStore::find_card(const std::string& id) {
    for (Card& card : cards_) {
        if (card.id == id) {
            return &card;
        }
    }
    return nullptr;
}

const Card* BoardStore::find_card(const std::string& id) const {
    for (const Card& card : cards_) {
        if (card.id == id) {
            return &card;
        }
    }
    return nullptr;
}

Column* BoardStore::find_column(const std::string& id) {
    for (Column& column : columns_) {
        if (column.id == id) {
            return &column;
        }
    }
    return nullptr;
}

const Column* BoardStore::find_column(const std::string& id) const {
    for (const Column& column : columns_) {
        if (column.id == id) {
            return &column;
        }
    }
    return nullptr;
}

int BoardStore::column_index(const std::string& id) const {
    for (int i = 0; i < static_cast<int>(columns_.size()); ++i) {
        if (columns_[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

Column* BoardStore::column_of(const std::string& card_id) {
    for (Column& column : columns_) {
        if (contains(column.card_ids, card_id)) {
            return &column;
        }
    }
    return nullptr;
}

const Column* BoardStore::column_of(const std::string& card_id) const {
    for (const Column& column : columns_) {
        if (contains(column.card_ids, card_id)) {
            return &column;
        }
    }
    return nullptr;
}

void BoardStore::move_card(const std::string& card_id, const std::string& column_id, int index) {
    for (Column& column : columns_) {
        column.card_ids.erase(std::remove(column.card_ids.begin(), column.card_ids.end(), card_id),
                              column.card_ids.end());
    }
    Column* column = find_column(column_id);
    if (column == nullptr) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<int>(column->card_ids.size())) {
        index = static_cast<int>(column->card_ids.size());
    }
    column->card_ids.insert(column->card_ids.begin() + index, card_id);
}

void BoardStore::move_column(const std::string& column_id, int index) {
    const int from = column_index(column_id);
    if (from < 0) {
        return;
    }
    Column column = std::move(columns_[static_cast<std::size_t>(from)]);
    columns_.erase(columns_.begin() + from);
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<int>(columns_.size())) {
        index = static_cast<int>(columns_.size());
    }
    columns_.insert(columns_.begin() + index, std::move(column));
}

bool BoardStore::nudge_card(const std::string& id, int delta) {
    Column* column = column_of(id);
    if (column == nullptr) {
        return false;
    }
    const auto it = std::find(column->card_ids.begin(), column->card_ids.end(), id);
    if (it == column->card_ids.end()) {
        return false;
    }
    const int index = static_cast<int>(it - column->card_ids.begin());
    const int next = index + delta;
    if (next < 0 || next >= static_cast<int>(column->card_ids.size())) {
        return false;
    }
    std::swap(column->card_ids[static_cast<std::size_t>(index)],
              column->card_ids[static_cast<std::size_t>(next)]);
    return true;
}

std::string BoardStore::uid(const char* prefix) {
    return std::string(prefix) + std::to_string(std::time(nullptr)) + "-" + std::to_string(++seq_);
}

void BoardStore::add_column(const std::string& name) {
    columns_.push_back(Column{uid("col"), name, {}});
}

bool BoardStore::add_card(const std::string& column_id, std::string title) {
    Column* column = find_column(column_id);
    if (column == nullptr) {
        return false;
    }
    Card card{uid("k"), std::move(title), ""};
    column->card_ids.push_back(card.id);
    cards_.push_back(std::move(card));
    return true;
}

void BoardStore::erase_card(const std::string& id) {
    cards_.erase(std::remove_if(cards_.begin(), cards_.end(),
                                [&](const Card& card) { return card.id == id; }),
                 cards_.end());
    for (Column& column : columns_) {
        column.card_ids.erase(std::remove(column.card_ids.begin(), column.card_ids.end(), id),
                              column.card_ids.end());
    }
}

bool BoardStore::erase_column(const std::string& id) {
    const int index = column_index(id);
    if (index < 0 || columns_.size() <= 1) {
        return false;
    }
    Column removed = columns_[static_cast<std::size_t>(index)];
    columns_.erase(columns_.begin() + index);
    const int target = std::max(0, index - 1);
    Column& neighbour = columns_[static_cast<std::size_t>(target)];
    neighbour.card_ids.insert(neighbour.card_ids.end(), removed.card_ids.begin(),
                              removed.card_ids.end());
    return true;
}

}  // namespace kanban
