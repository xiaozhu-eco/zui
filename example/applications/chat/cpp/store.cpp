#include "store.h"

#include "clock.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace chat {

void ChatStore::seed() {
    convos_ = {{"lin", "林晚", ago(40)},
               {"shop", "渡口书局", ago(20)},
               {"chen", "陈屿", ago(10)},
               {"print", "灯塔印刷", ago(6)}};
    messages_ = {
        {"l1", "lin", false, "第三章的校样我放在传达室了。", ago(30)},
        {"l2", "lin", true, "收到。目录那一页我今晚看。", ago(29)},
        {"l3", "lin", false, "第二节标题和正文对不齐，你留意一下。", ago(26)},
        {"l4", "lin", true, "是「潮汐」那节吗？", ago(25)},
        {"l5", "lin", false, "对。还有一张表被拆到了下一页。", ago(24)},
        {"l6", "lin", true, "表我拉回来。人名索引先不动。", ago(22)},
        {"l7", "lin", false, "索引可以等纸样。周宁说他会补一条注。", ago(18)},
        {"l8", "lin", true, "那我在页边留白。", ago(17)},
        {"l9", "lin", false, "明天上午十点，二楼对一下目录。", ago(5)},
        {"l10", "lin", true, "我带着校样过去。", ago(4.5)},
        {"l11", "lin", false, "如果迟到就先看渡口手记的前言。", ago(3)},
        {"l12", "lin", true, "前言我已经改过一轮。", ago(2.5)},
        {"l13", "lin", false, "好。改动用铅笔，别直接上墨。", ago(2)},
        {"l14", "lin", true, "知道。", ago(1.2)},
        {"s1", "shop", false, "这周的还书日改到周三下午。", ago(16)},
        {"s2", "shop", true, "夜航表和海岸植物都在我这儿。", ago(15)},
        {"s3", "shop", false, "海岸植物可以续借，夜航表有人等。", ago(14)},
        {"c1", "chen", false, "竹纸样打出来了，边有一点毛。", ago(8)},
        {"c2", "chen", true, "毛边留下吧，别裁。", ago(7)},
        {"p1", "print", false, "灯塔摘录的封面色号我发你了吗？", ago(6)},
        {"p2", "print", true, "还没有。用上次那张赭石就行。", ago(5.5)},
    };
}

std::string ChatStore::uid(const char* prefix) {
    return std::string(prefix) + std::to_string(now_ms()) + "-" + std::to_string(++seq_);
}

const Convo* ChatStore::find_convo(const std::string& id) const {
    for (const Convo& item : convos_) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

const Message* ChatStore::last_of(const std::string& id) const {
    const Message* found = nullptr;
    for (const Message& message : messages_) {
        if (message.conv_id == id && (found == nullptr || message.ts >= found->ts)) {
            found = &message;
        }
    }
    return found;
}

void ChatStore::erase_convo(const std::string& id) {
    convos_.erase(std::remove_if(convos_.begin(), convos_.end(),
                                 [&](const Convo& convo) { return convo.id == id; }),
                  convos_.end());
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(),
                                   [&](const Message& message) { return message.conv_id == id; }),
                    messages_.end());
}

bool ChatStore::save(const std::filesystem::path& file) const {
    nlohmann::json convos = nlohmann::json::array();
    for (const Convo& convo : convos_) {
        convos.push_back({{"id", convo.id}, {"name", convo.name}, {"created", convo.created}});
    }
    nlohmann::json messages = nlohmann::json::array();
    for (const Message& message : messages_) {
        messages.push_back({{"id", message.id},
                            {"convId", message.conv_id},
                            {"mine", message.mine},
                            {"text", message.text},
                            {"ts", message.ts}});
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        return false;
    }
    out << nlohmann::json{{"convos", convos}, {"messages", messages}}.dump(2);
    return static_cast<bool>(out);
}

LoadResult ChatStore::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        seed();
        return LoadResult::Seeded;
    }
    try {
        nlohmann::json data;
        in >> data;
        convos_.clear();
        messages_.clear();
        for (const auto& item : data.at("convos")) {
            Convo convo;
            convo.id = item.value("id", "");
            convo.name = item.value("name", "");
            convo.created = item.value("created", 0LL);
            if (!convo.id.empty()) {
                convos_.push_back(std::move(convo));
            }
        }
        for (const auto& item : data.at("messages")) {
            Message message;
            message.id = item.value("id", "");
            message.conv_id = item.value("convId", "");
            message.mine = item.value("mine", false);
            message.text = item.value("text", "");
            message.ts = item.value("ts", 0LL);
            if (!message.id.empty()) {
                messages_.push_back(std::move(message));
            }
        }
        if (convos_.empty()) {
            seed();
            return LoadResult::Seeded;
        }
        return LoadResult::Loaded;
    } catch (...) {
        seed();
        return LoadResult::Corrupt;
    }
}

}  // namespace chat
