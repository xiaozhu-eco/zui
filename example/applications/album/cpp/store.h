#ifndef ALBUM_STORE_H
#define ALBUM_STORE_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace album {

struct Album {
    std::string id;
    std::string name;
};

struct Photo {
    std::string id;
    std::string album_id;
    std::string name;
    std::string note;
    std::string file;
    long long added = 0;
};

enum class LoadResult { Seeded, Loaded, Corrupt };

std::string trim(std::string_view in);

/// Albums and photos. It does not know about `App`; the session paints and reports save failures.
/// Sidebar order matches the browser's `sortAlbums`: built-in ids keep their seed rank, other
/// albums sort by name, and 未分类 (`loose`) is last.
class AlbumStore {
public:
    const std::vector<Album>& albums() const { return albums_; }
    std::vector<Album>& albums() { return albums_; }
    const std::vector<Photo>& photos() const { return photos_; }
    std::vector<Photo>& photos() { return photos_; }

    LoadResult load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    void seed();

    Album* find_album(const std::string& id);
    const Album* find_album(const std::string& id) const;
    Photo* find_photo(const std::string& id);
    const Photo* find_photo(const std::string& id) const;

    std::vector<Photo*> visible(const std::string& current);
    int count_in(const std::string& album_id) const;
    /// Built-ins, then named albums, then `loose`.
    std::vector<const Album*> ordered() const;

    std::string uid(const char* prefix);

private:
    std::vector<Album> albums_;
    std::vector<Photo> photos_;
    unsigned seq_ = 0;
};

}  // namespace album

#endif
