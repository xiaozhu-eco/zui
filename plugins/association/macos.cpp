#include "ext/slots.h"
#include "plugins/association/macos_launch.h"
#include "plugins/association/macos_query.h"
#include "plugins/association/macos_store.h"
#include "plugins/association/record.h"

#include <memory>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

struct MacShared {
    association_macos::MacAssociationStore store;
    association_macos::MacAssociationQuery query;
    association_macos::MacAssociationLaunch launch;
};

plugin::OpenerReport write_opener(MacShared& shared, const plugin::OpenerSpec& spec) {
    association_detail::Prepared prepared = association_detail::prepare(spec);
    if (!prepared.ready) {
        return prepared.report;
    }
    return shared.store.write(prepared.record, std::move(prepared.report));
}

class MacFiles final : public FileAssociationService {
public:
    explicit MacFiles(std::shared_ptr<MacShared> shared) : shared_(std::move(shared)) {}

    bool register_file_extension(const plugin::FileAssociation& file) override {
        plugin::OpenerSpec spec;
        spec.app_name = file.prog_id;
        spec.prog_id = file.prog_id;
        spec.executable = file.executable;
        spec.extension = file.extension;
        return write_opener(*shared_, spec).ok;
    }
    bool register_url_scheme(const plugin::SchemeAssociation& scheme) override {
        plugin::OpenerSpec spec;
        spec.app_name = scheme.prog_id;
        spec.prog_id = scheme.prog_id;
        spec.executable = scheme.executable;
        spec.scheme = scheme.scheme;
        return write_opener(*shared_, spec).ok;
    }
    bool unregister_file_extension(const plugin::FileAssociation& file) override {
        plugin::OpenerSpec spec;
        spec.prog_id = file.prog_id;
        spec.extension = file.extension;
        return shared_->store.remove(association_detail::build_record(spec));
    }
    bool unregister_url_scheme(const plugin::SchemeAssociation& scheme) override {
        plugin::OpenerSpec spec;
        spec.prog_id = scheme.prog_id;
        spec.scheme = scheme.scheme;
        return shared_->store.remove(association_detail::build_record(spec));
    }

private:
    std::shared_ptr<MacShared> shared_;
};

class MacOpeners final : public OpenerService {
public:
    explicit MacOpeners(std::shared_ptr<MacShared> shared) : shared_(std::move(shared)) {}

    plugin::OpenerReport register_opener(const plugin::OpenerSpec& spec) override {
        return write_opener(*shared_, spec);
    }
    plugin::OpenerReport query_opener(const plugin::OpenerSpec& spec) override {
        return shared_->query.query(plugin::opener_lookup(spec));
    }
    plugin::OpenerReport unregister_opener(const plugin::OpenerSpec& spec) override {
        plugin::OpenerReport report;
        if (!shared_->store.remove(association_detail::build_record(spec))) {
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
    std::shared_ptr<MacShared> shared_;
};

}  // namespace

void register_macos_association(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    auto shared = std::make_shared<MacShared>();
    registry.add_file_association(std::make_unique<MacFiles>(shared));
    registry.add_opener(std::make_unique<MacOpeners>(shared));
#else
    (void)registry;
#endif
}

}  // namespace zui
