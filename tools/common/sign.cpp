#include "sign.h"

#include "subprocess.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zui_tool {
namespace {

std::string env_string(const char* key) {
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
}

// A .pfx is a path (the project's, or an absolute one); anything else is a
// SHA-1 thumbprint of a certificate in the store.
bool certificate_is_file(const std::string& certificate) {
    return certificate.find('.') != std::string::npos ||
           certificate.find('/') != std::string::npos ||
           certificate.find('\\') != std::string::npos;
}

#if defined(_WIN32)

bool newest_signtool_in(const std::filesystem::path& kits, std::filesystem::path& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(kits, ec)) {
        return false;
    }
    std::string best;
    for (const auto& entry : std::filesystem::directory_iterator(kits, ec)) {
        if (ec || !entry.is_directory()) {
            break;
        }
        const std::filesystem::path exe = entry.path() / "x64" / "signtool.exe";
        std::error_code file_ec;
        if (!std::filesystem::is_regular_file(exe, file_ec)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (best.empty() || name > best) {
            best = name;
            out = exe;
        }
    }
    return !best.empty();
}

std::filesystem::path find_signtool() {
    std::vector<std::filesystem::path> roots;
    const std::string program_files_x86 = env_string("ProgramFiles(x86)");
    if (!program_files_x86.empty()) {
        roots.push_back(std::filesystem::path(program_files_x86) / "Windows Kits" / "10" / "bin");
    }
    const std::string program_files = env_string("ProgramFiles");
    if (!program_files.empty()) {
        roots.push_back(std::filesystem::path(program_files) / "Windows Kits" / "10" / "bin");
    }
    for (const std::filesystem::path& root : roots) {
        std::filesystem::path found;
        if (newest_signtool_in(root, found)) {
            return found;
        }
    }
    return {};
}

#endif

bool sign_one(const SigningOptions& options, const std::filesystem::path& file, std::string& error) {
#if defined(_WIN32)
    const std::filesystem::path signtool = find_signtool();
    if (signtool.empty()) {
        error = "missing signtool.exe; install the Windows SDK signing tools";
        return false;
    }
    std::vector<std::string> args = command(signtool.string(), "sign", "/fd", "sha256", "/td",
                                           "sha256");
    // A release signature is timestamped: without one it stops verifying when
    // the certificate expires. "none" turns that off for an offline build.
    std::string timestamp = options.timestamp_url;
    for (char& c : timestamp) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (timestamp == "none") {
        args.pop_back();
        args.pop_back();
    } else if (!options.timestamp_url.empty()) {
        args.push_back("/tr");
        args.push_back(options.timestamp_url);
    } else {
        args.push_back("/tr");
        args.push_back("http://timestamp.digicert.com");
    }
    if (certificate_is_file(options.certificate)) {
        std::filesystem::path pfx(options.certificate);
        if (pfx.is_relative()) {
            pfx = options.project / pfx;
        }
        const std::string password = env_string("ZUI_PFX_PASSWORD");
        if (password.empty()) {
            error = "set ZUI_PFX_PASSWORD to sign with " + options.certificate;
            return false;
        }
        args.push_back("/f");
        args.push_back(pfx.string());
        args.push_back("/p");
        args.push_back(password);
    } else {
        args.push_back("/sha1");
        args.push_back(options.certificate);
    }
    args.push_back(file.string());
    if (run_process(args) != 0) {
        error = "signtool failed on " + file.filename().string();
        return false;
    }
    std::cout << "zui-pack: signed " << file.filename().string() << " with "
              << (certificate_is_file(options.certificate) ? "the project certificate"
                                                           : "the store certificate")
              << std::endl;
    return true;
#else
    (void)options;
    (void)file;
    error = "signing runs on Windows";
    return false;
#endif
}

}  // namespace

bool signing_requested(const SigningOptions& options) { return !options.certificate.empty(); }

bool sign_file(const SigningOptions& options, const std::filesystem::path& file, std::string& error) {
    if (!signing_requested(options)) {
        return true;
    }
    return sign_one(options, file, error);
}

bool sign_bytes(const SigningOptions& options, std::vector<std::uint8_t>& bytes,
                const std::string& name, std::string& error) {
    if (!signing_requested(options)) {
        return true;
    }
    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() / "zui-pack-sign" / name;
    std::error_code ec;
    std::filesystem::create_directories(temp.parent_path(), ec);
    std::filesystem::remove(temp, ec);
    {
        std::ofstream out(temp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "cannot write " + temp.string();
            return false;
        }
    }
    if (!sign_one(options, temp, error)) {
        return false;
    }
    std::ifstream in(temp, std::ios::binary);
    std::vector<std::uint8_t> signed_bytes((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
    if (signed_bytes.empty()) {
        error = "the signed " + name + " is empty";
        return false;
    }
    bytes.swap(signed_bytes);
    return true;
}

}  // namespace zui_tool
