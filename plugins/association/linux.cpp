#include "ext/slots.h"
#include "plugins/association/linux_launch.h"
#include "plugins/association/linux_query.h"
#include "plugins/association/linux_store.h"
#include "plugins/association/record.h"

#include <memory>

namespace zui {
namespace {

struct LinuxShared {
    association_linux::LinuxAssociationStore store;
    association_linux::LinuxAssociationQuery query;
    association_linux::LinuxAssociationLaunch launch;
};

plugin::OpenerReport write_opener(LinuxShared& shared, const plugin::OpenerSpec& spec) {
    association_detail::Prepared prepared = association_detail::prepare(spec);
    if (!prepared.ready) {
        return prepared.report;
    }
    return shared.store.write(prepared.record, std::move(prepared.report));
}

class LinuxFiles final : public FileAssociationService {
public:
    explicit LinuxFiles(std::shared_ptr<LinuxShared> shared) : shared_(std::move(shared)) {}

    bool register_file_extension(const plugin::FileAssociation& file) override {
        plugin::OpenerSpec spec;
        spec.prog_id = file.prog_id;
        spec.executable = file.executable;
        spec.extension = file.extension;
        spec.app_name = file.prog_id;
        return write_opener(*shared_, spec).ok;
    }

    bool register_url_scheme(const plugin::SchemeAssociation& scheme) override {
        plugin::OpenerSpec spec;
        spec.prog_id = scheme.prog_id;
        spec.executable = scheme.executable;
        spec.scheme = scheme.scheme;
        spec.app_name = scheme.prog_id;
        return write_opener(*shared_, spec).ok;
    }

    bool unregister_file_extension(const plugin::FileAssociation& file) override {
        plugin::OpenerSpec spec;
        spec.prog_id = file.prog_id;
        spec.executable = file.executable.empty() ? std::string("unregister") : file.executable;
        spec.extension = file.extension;
        return shared_->store.remove(association_detail::build_record(spec));
    }

    bool unregister_url_scheme(const plugin::SchemeAssociation& scheme) override {
        plugin::OpenerSpec spec;
        spec.prog_id = scheme.prog_id;
        spec.executable = scheme.executable.empty() ? std::string("unregister") : scheme.executable;
        spec.scheme = scheme.scheme;
        return shared_->store.remove(association_detail::build_record(spec));
    }

private:
    std::shared_ptr<LinuxShared> shared_;
};

class LinuxOpeners final : public OpenerService {
public:
    explicit LinuxOpeners(std::shared_ptr<LinuxShared> shared) : shared_(std::move(shared)) {}

    plugin::OpenerReport register_opener(const plugin::OpenerSpec& spec) override {
        return write_opener(*shared_, spec);
    }

    plugin::OpenerReport query_opener(const plugin::OpenerSpec& spec) override {
        return shared_->query.query(plugin::opener_lookup(spec));
    }

    plugin::OpenerReport unregister_opener(const plugin::OpenerSpec& spec) override {
        plugin::OpenerSpec stored = spec;
        if (stored.executable.empty()) {
            stored.executable = "unregister";
        }
        plugin::OpenerReport report;
        if (!shared_->store.remove(association_detail::build_record(stored))) {
            return report;
        }
        report.ran = true;
        report.ok = true;
        return report;
    }

    plugin::OpenerReport launch_opener(const plugin::OpenerSpec& spec, std::string_view file_path) override {
        return shared_->launch.launch(spec.executable, file_path);
    }

private:
    std::shared_ptr<LinuxShared> shared_;
};

}  // namespace

void register_linux_association(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    auto shared = std::make_shared<LinuxShared>();
    registry.add_file_association(std::make_unique<LinuxFiles>(shared));
    registry.add_opener(std::make_unique<LinuxOpeners>(shared));
#else
    (void)registry;
#endif
}

}  // namespace zui
