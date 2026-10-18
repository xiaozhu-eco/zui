#ifndef PARTICLE_SHADER_H
#define PARTICLE_SHADER_H

#include <filesystem>
#include <string>

namespace particle {

std::string trim(std::string_view in);
std::string num1(float value);
std::string default_shader();
bool read_define(const std::string& src, const std::string& name, float& out);
void write_define(std::string& src, const std::string& name, float value);
std::string read_file(const std::filesystem::path& path);
bool write_file(const std::filesystem::path& path, const std::string& text);

}  // namespace particle

#endif
