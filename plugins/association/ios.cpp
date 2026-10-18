#include "ext/slots.h"
#include "plugins/association/ios_launch.h"
#include "plugins/association/ios_query.h"
#include "plugins/association/ios_store.h"
#include "plugins/association/record.h"

#include <memory>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

struct IosShared {
    association_ios::IosAssociationStore store;
    association_ios::IosAssociationQuery query;
    association_ios::IosAssociationLaunch launch;
};

plugin::OpenerReport write_opener(IosShared& shared, const plugin::OpenerSpec& spec) {
    association_detail::Prepared prepared = association_detail::prepare(spec);
    if (!prepared.ready) {
        return prepared.report;
    }
    return shared.store.write(prepared.record, std::move(prepared.report));
}

class IosFiles final : public FileAssociationService {
public:
    explicit IosFiles(std::shared_ptr<IosShared> shared) : shared_(std::move(shared)) {}

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
        spec.executable = "unregister";
        return shared_->store.remove(association_detail::build_record(spec));
    }
    bool unregister_url_scheme(const plugin::SchemeAssociation& scheme) override {
        plugin::OpenerSpec spec;
        spec.prog_id = scheme.prog_id;
        spec.scheme = scheme.scheme;
        spec.executable = "unregister";
        return shared_->store.remove(association_detail::build_record(spec));
    }

private:
    std::shared_ptr<IosShared> shared_;
};

class IosOpeners final : public OpenerService {
public:
    explicit IosOpeners(std::shared_ptr<IosShared> shared) : shared_(std::move(shared)) {}

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
    std::shared_ptr<IosShared> shared_;
};

}  // namespace

void register_ios_association(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    auto shared = std::make_shared<IosShared>();
    registry.add_file_association(std::make_unique<IosFiles>(shared));
    registry.add_opener(std::make_unique<IosOpeners>(shared));
#else
    (void)registry;
#endif
}

}  // namespace zui
