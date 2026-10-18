#include "ext/slots.h"
#include "plugins/association/android_launch.h"
#include "plugins/association/android_query.h"
#include "plugins/association/android_store.h"
#include "plugins/association/record.h"

#include <memory>

namespace zui {
namespace {

struct AndroidShared {
    association_android::AndroidAssociationStore store;
    association_android::AndroidAssociationQuery query;
    association_android::AndroidAssociationLaunch launch;
};

plugin::OpenerReport write_opener(AndroidShared& shared, const plugin::OpenerSpec& spec) {
    association_detail::Prepared prepared = association_detail::prepare(spec);
    if (!prepared.ready) {
        return prepared.report;
    }
    return shared.store.write(prepared.record, std::move(prepared.report));
}

class AndroidFiles final : public FileAssociationService {
public:
    explicit AndroidFiles(std::shared_ptr<AndroidShared> shared) : shared_(std::move(shared)) {}

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
    std::shared_ptr<AndroidShared> shared_;
};

class AndroidOpeners final : public OpenerService {
public:
    explicit AndroidOpeners(std::shared_ptr<AndroidShared> shared) : shared_(std::move(shared)) {}

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
    std::shared_ptr<AndroidShared> shared_;
};

}  // namespace

void register_android_association(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    auto shared = std::make_shared<AndroidShared>();
    registry.add_file_association(std::make_unique<AndroidFiles>(shared));
    registry.add_opener(std::make_unique<AndroidOpeners>(shared));
#else
    (void)registry;
#endif
}

}  // namespace zui
