#ifndef ALBUM_COMMANDS_H
#define ALBUM_COMMANDS_H

#include "lightbox.h"
#include "store.h"

#include <filesystem>
#include <memory>
#include <string>

namespace album {

/// What a confirm command is allowed to change. The session persists and repaints afterwards.
struct ConfirmContext {
    AlbumStore& store;
    Lightbox& light;
    std::string& current;
    const std::filesystem::path& dir;
};

/// The confirm box runs one of these. Delete-album, delete-photo, and restore are different
/// operations, so each is its own command instead of a branch in the ok handler.
class ConfirmCommand {
public:
    virtual ~ConfirmCommand() = default;
    virtual std::string body() const = 0;
    virtual const char* ok_label() const = 0;
    virtual void execute(ConfirmContext& ctx) = 0;
};

std::unique_ptr<ConfirmCommand> make_delete_album(std::string name);
std::unique_ptr<ConfirmCommand> make_delete_photo(std::string name);
std::unique_ptr<ConfirmCommand> make_restore_samples();

}  // namespace album

#endif
