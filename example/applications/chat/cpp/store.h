#ifndef CHAT_STORE_H
#define CHAT_STORE_H

#include <filesystem>
#include <string>
#include <vector>

namespace chat {

struct Convo {
    std::string id;
    std::string name;
    long long created = 0;
};

struct Message {
    std::string id;
    std::string conv_id;
    bool mine = false;
    std::string text;
    long long ts = 0;
};

enum class LoadResult { Seeded, Loaded, Corrupt };

/// Conversations and messages. No `App`: painting and status text stay on the session.
class ChatStore {
public:
    LoadResult load(const std::filesystem::path& file);
    /// False when the file could not be written. The caller sets the status line.
    bool save(const std::filesystem::path& file) const;

    void seed();
    std::string uid(const char* prefix);

    const std::vector<Convo>& convos() const { return convos_; }
    std::vector<Convo>& convos() { return convos_; }
    const std::vector<Message>& messages() const { return messages_; }
    std::vector<Message>& messages() { return messages_; }

    const Convo* find_convo(const std::string& id) const;
    const Message* last_of(const std::string& id) const;
    void erase_convo(const std::string& id);

private:
    std::vector<Convo> convos_;
    std::vector<Message> messages_;
    unsigned seq_ = 0;
};

}  // namespace chat

#endif
