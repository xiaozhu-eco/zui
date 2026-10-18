#ifndef KANBAN_STORE_H
#define KANBAN_STORE_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kanban {

struct Card {
    std::string id;
    std::string title;
    std::string note;
};

struct Column {
    std::string id;
    std::string name;
    std::vector<std::string> card_ids;
};

enum class LoadResult { Seeded, Loaded, Corrupt };

std::string trim(std::string_view in);

/// The board's cards and columns. It does not know about `App`; the session paints and
/// reports save failures.
class BoardStore {
public:
    const std::vector<Column>& columns() const { return columns_; }
    std::vector<Column>& columns() { return columns_; }
    const std::vector<Card>& cards() const { return cards_; }

    LoadResult load(const std::filesystem::path& file);
    void seed();
    /// Writes the current board. False when the file cannot be opened or the write fails.
    bool save(const std::filesystem::path& file) const;

    Card* find_card(const std::string& id);
    const Card* find_card(const std::string& id) const;
    Column* find_column(const std::string& id);
    const Column* find_column(const std::string& id) const;
    int column_index(const std::string& id) const;
    Column* column_of(const std::string& card_id);
    const Column* column_of(const std::string& card_id) const;

    void move_card(const std::string& card_id, const std::string& column_id, int index);
    void move_column(const std::string& column_id, int index);
    /// Swaps a card with its neighbour in the same column. False at either end or when missing.
    bool nudge_card(const std::string& id, int delta);
    void add_column(const std::string& name);
    /// Appends a card. Returns false when the column is missing.
    bool add_card(const std::string& column_id, std::string title);
    void erase_card(const std::string& id);
    /// Moves the column's cards onto the neighbour, then drops the column.
    /// False when the id is missing or it is the last column.
    bool erase_column(const std::string& id);

    std::string uid(const char* prefix);

private:
    std::vector<Column> columns_;
    std::vector<Card> cards_;
    unsigned seq_ = 0;
};

}  // namespace kanban

#endif
