#include "ext/slots.h"
#include "plugins/association/record.h"
#include "plugins/association/win32_launch.h"
#include "plugins/association/win32_query.h"
#include "plugins/association/win32_store.h"

#include <memory>

#if defined(_WIN32)
#include <shlobj.h>
#endif

namespace zui {
namespace {

struct Win32Shared {
    association_win32::Win32AssociationStore store;
    association_win32::Win32AssociationQuery query;
    association_win32::Win32AssociationLaunch launch;
};

class Win32Files final : public FileAssociationService {
public:
    explicit Win32Files(std::shared_ptr<Win32Shared> shared) : shared_(std::move(shared)) {}

    bool register_file_extension(const plugin::FileAssociation& spec) override {
        return shared_->store.register_extension(spec.extension, spec.prog_id, spec.executable);
    }

    bool unregister_file_extension(const plugin::FileAssociation& spec) override {
        return shared_->store.unregister_prog(spec.prog_id, spec.extension);
    }

    bool register_url_scheme(const plugin::SchemeAssociation& spec) override {
        return shared_->store.register_scheme(spec.scheme, spec.prog_id, spec.executable);
    }

    bool unregister_url_scheme(const plugin::SchemeAssociation& spec) override {
        return shared_->store.unregister_prog(spec.prog_id, spec.scheme);
    }

private:
    std::shared_ptr<Win32Shared> shared_;
};

class Win32Openers final : public OpenerService {
public:
    explicit Win32Openers(std::shared_ptr<Win32Shared> shared) : shared_(std::move(shared)) {}

    plugin::OpenerReport register_opener(const plugin::OpenerSpec& spec) override {
        association_detail::Prepared prepared = association_detail::prepare(spec);
        if (!prepared.ready) {
            return prepared.report;
        }
#if defined(_WIN32)
        prepared.report.ran = true;
        unsigned long error = 0;
        if (!shared_->store.write(prepared.record, error)) {
            shared_->store.remove(prepared.record);
            prepared.report.os_error = static_cast<std::uint32_t>(error);
            return prepared.report;
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
        prepared.report.ok = true;
#endif
        return prepared.report;
    }

    plugin::OpenerReport query_opener(const plugin::OpenerSpec& spec) override {
        return shared_->query.query(plugin::opener_lookup(spec));
    }

    plugin::OpenerReport unregister_opener(const plugin::OpenerSpec& spec) override {
        plugin::OpenerSpec stored = spec;
        if (stored.executable.empty()) {
            stored.executable = "unregister";
        }
        const association_detail::Record record = association_detail::build_record(stored);
        plugin::OpenerReport report;
#if defined(_WIN32)
        shared_->store.remove(record);
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
        report.ran = true;
        report.ok = true;
#else
        (void)record;
#endif
        return report;
    }

    plugin::OpenerReport launch_opener(const plugin::OpenerSpec& spec, std::string_view file_path) override {
        return shared_->launch.launch(spec.executable, file_path);
    }

private:
    std::shared_ptr<Win32Shared> shared_;
};

}  // namespace

void register_win32_association(ExtensionRegistry& registry) {
#if defined(_WIN32)
    auto shared = std::make_shared<Win32Shared>();
    registry.add_file_association(std::make_unique<Win32Files>(shared));
    registry.add_opener(std::make_unique<Win32Openers>(shared));
#else
    (void)registry;
#endif
}

}  // namespace zui
