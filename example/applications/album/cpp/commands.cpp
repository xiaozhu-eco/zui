#include "commands.h"

#include <algorithm>
#include <system_error>

namespace album {
namespace {

class DeleteAlbumCommand : public ConfirmCommand {
public:
    explicit DeleteAlbumCommand(std::string name) : name_(std::move(name)) {}

    std::string body() const override {
        return "删除相册「" + name_ + "」？里面的照片会挪到未分类。";
    }
    const char* ok_label() const override { return "删除"; }

    void execute(ConfirmContext& ctx) override {
        for (Photo& photo : ctx.store.photos()) {
            if (photo.album_id == ctx.current) {
                photo.album_id = "loose";
            }
        }
        std::vector<Album>& albums = ctx.store.albums();
        albums.erase(std::remove_if(albums.begin(), albums.end(),
                                    [&](const Album& album) { return album.id == ctx.current; }),
                     albums.end());
        ctx.current = "loose";
        ctx.light.clear();
    }

private:
    std::string name_;
};

class DeletePhotoCommand : public ConfirmCommand {
public:
    explicit DeletePhotoCommand(std::string name) : name_(std::move(name)) {}

    std::string body() const override { return "删除「" + name_ + "」？"; }
    const char* ok_label() const override { return "删除"; }

    void execute(ConfirmContext& ctx) override {
        const std::string id = ctx.light.photo_id();
        const Photo* photo = ctx.store.find_photo(id);
        if (photo != nullptr && photo->file.rfind("album-photos/", 0) == 0) {
            std::error_code error;
            std::filesystem::remove(ctx.dir / photo->file, error);
        }
        std::vector<Photo>& photos = ctx.store.photos();
        photos.erase(std::remove_if(photos.begin(), photos.end(),
                                    [&](const Photo& item) { return item.id == id; }),
                     photos.end());
        ctx.light.clear();
    }

private:
    std::string name_;
};

class RestoreSamplesCommand : public ConfirmCommand {
public:
    std::string body() const override { return "恢复示例会清掉导入的照片，回到这六张。"; }
    const char* ok_label() const override { return "恢复"; }

    void execute(ConfirmContext& ctx) override {
        std::error_code error;
        std::filesystem::remove_all(ctx.dir / "album-photos", error);
        ctx.store.seed();
        ctx.current = "all";
        ctx.light.clear();
    }
};

}  // namespace

std::unique_ptr<ConfirmCommand> make_delete_album(std::string name) {
    return std::make_unique<DeleteAlbumCommand>(std::move(name));
}

std::unique_ptr<ConfirmCommand> make_delete_photo(std::string name) {
    return std::make_unique<DeletePhotoCommand>(std::move(name));
}

std::unique_ptr<ConfirmCommand> make_restore_samples() {
    return std::make_unique<RestoreSamplesCommand>();
}

}  // namespace album
