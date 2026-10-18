#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zui_tool {

// How a Windows release is signed. An empty certificate means the package is
// not signed, and the packer says so rather than pretending. The password is
// never in the project file: it comes from the environment.
struct SigningOptions {
    std::filesystem::path project;
    std::string certificate;    // a .pfx in the project, or a SHA-1 thumbprint
    std::string timestamp_url;  // empty uses a public timestamp service
};

bool signing_requested(const SigningOptions& options);

// Signs one file in place. Fails when a certificate is configured but the
// signer or the password is missing.
bool sign_file(const SigningOptions& options, const std::filesystem::path& file,
               std::string& error);

// Signs bytes: an executable on its way into an archive. `name` names the
// temporary file the signer reads and writes.
bool sign_bytes(const SigningOptions& options, std::vector<std::uint8_t>& bytes,
                const std::string& name, std::string& error);

}  // namespace zui_tool
