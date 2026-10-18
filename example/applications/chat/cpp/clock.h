#ifndef CHAT_CLOCK_H
#define CHAT_CLOCK_H

#include <string>
#include <string_view>

namespace chat {

std::string trim(std::string_view in);
std::string first_char(const std::string& text);
/// ASCII A–Z only. Conversation search does not case-fold other letters.
std::string fold_ascii(std::string text);
long long now_ms();
long long ago(double hours);
std::string day_label(long long ms);
std::string clock_label(long long ms);

}  // namespace chat

#endif
