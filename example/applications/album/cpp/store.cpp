#include "store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <utility>

namespace album {
namespace {

int album_rank(const std::string& id) {
    if (id == "ferry") {
        return 0;
    }
    if (id == "light") {
        return 1;
    }
    if (id == "shop") {
        return 2;
    }
    if (id == "loose") {
        return 100;
    }
    return 50;
}

}  // namespace

std::string trim(std::string_view in) {
    std::size_t a = 0;
    while (a < in.size() && (in[a] == ' ' || in[a] == '\t' || in[a] == '\n' || in[a] == '\r')) {
        ++a;
    }
    std::size_t b = in.size();
    while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\n' || in[b - 1] == '\r')) {
        --b;
    }
    return std::string(in.substr(a, b - a));
}

void AlbumStore::seed() {
    albums_ = {{"ferry", "渡口"}, {"light", "灯塔"}, {"shop", "工坊"}, {"loose", "未分类"}};
    const long long start = 1760000000000LL;
    photos_ = {
        {"p-ferry", "ferry", "渡口", "傍晚的栈桥，潮还没退干净。", "pictures/ferry.svg", start},
        {"p-lighthouse", "light", "灯塔", "灯塔印刷社门口那盏，不是海上的。", "pictures/lighthouse.svg",
         start - 3600000},
        {"p-coast", "ferry", "海岸", "渡口往东的一段，石头是赭色的。", "pictures/coast.svg", start - 7200000},
        {"p-bamboo", "shop", "竹林", "工坊后面的竹，纸样就晒在这里。", "pictures/bamboo.svg", start - 10800000},
        {"p-night", "light", "夜航", "夜航表上画的那条线，灯还亮着。", "pictures/night.svg", start - 14400000},
        {"p-paper", "shop", "纸样", "毛边留下，不裁。", "pictures/paper.svg", start - 18000000},
    };
}

LoadResult AlbumStore::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        seed();
        return LoadResult::Seeded;
    }
    try {
        nlohmann::json data;
        in >> data;
        albums_.clear();
        photos_.clear();
        for (const auto& item : data.at("albums")) {
            Album album;
            album.id = item.value("id", "");
            album.name = item.value("name", "");
            if (!album.id.empty()) {
                albums_.push_back(std::move(album));
            }
        }
        for (const auto& item : data.at("photos")) {
            Photo photo;
            photo.id = item.value("id", "");
            photo.album_id = item.value("albumId", "loose");
            photo.name = item.value("name", "");
            photo.note = item.value("note", "");
            photo.file = item.value("file", "");
            photo.added = item.value("added", 0LL);
            if (!photo.id.empty()) {
                photos_.push_back(std::move(photo));
            }
        }
        if (albums_.empty()) {
            seed();
            return LoadResult::Seeded;
        }
        return LoadResult::Loaded;
    } catch (...) {
        seed();
        return LoadResult::Corrupt;
    }
}

bool AlbumStore::save(const std::filesystem::path& file) const {
    nlohmann::json albums = nlohmann::json::array();
    for (const Album& album : albums_) {
        albums.push_back({{"id", album.id}, {"name", album.name}});
    }
    nlohmann::json photos = nlohmann::json::array();
    for (const Photo& photo : photos_) {
        photos.push_back({{"id", photo.id},
                          {"albumId", photo.album_id},
                          {"name", photo.name},
                          {"note", photo.note},
                          {"file", photo.file},
                          {"added", photo.added}});
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        return false;
    }
    out << nlohmann::json{{"albums", albums}, {"photos", photos}}.dump(2);
    return static_cast<bool>(out);
}

Album* AlbumStore::find_album(const std::string& id) {
    for (Album& album : albums_) {
        if (album.id == id) {
            return &album;
        }
    }
    return nullptr;
}

const Album* AlbumStore::find_album(const std::string& id) const {
    for (const Album& album : albums_) {
        if (album.id == id) {
            return &album;
        }
    }
    return nullptr;
}

Photo* AlbumStore::find_photo(const std::string& id) {
    for (Photo& photo : photos_) {
        if (photo.id == id) {
            return &photo;
        }
    }
    return nullptr;
}

const Photo* AlbumStore::find_photo(const std::string& id) const {
    for (const Photo& photo : photos_) {
        if (photo.id == id) {
            return &photo;
        }
    }
    return nullptr;
}

std::vector<Photo*> AlbumStore::visible(const std::string& current) {
    std::vector<Photo*> rows;
    for (Photo& photo : photos_) {
        if (current == "all" || photo.album_id == current) {
            rows.push_back(&photo);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Photo* a, const Photo* b) { return a->added > b->added; });
    return rows;
}

int AlbumStore::count_in(const std::string& album_id) const {
    int count = 0;
    for (const Photo& photo : photos_) {
        if (photo.album_id == album_id) {
            ++count;
        }
    }
    return count;
}

std::vector<const Album*> AlbumStore::ordered() const {
    std::vector<const Album*> rows;
    rows.reserve(albums_.size());
    for (const Album& album : albums_) {
        rows.push_back(&album);
    }
    std::sort(rows.begin(), rows.end(), [](const Album* a, const Album* b) {
        const int delta = album_rank(a->id) - album_rank(b->id);
        if (delta != 0) {
            return delta < 0;
        }
        return a->name < b->name;
    });
    return rows;
}

std::string AlbumStore::uid(const char* prefix) {
    return std::string(prefix) + std::to_string(std::time(nullptr)) + "-" + std::to_string(++seq_);
}

}  // namespace album
