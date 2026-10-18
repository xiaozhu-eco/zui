#ifndef CALENDAR_DATES_H
#define CALENDAR_DATES_H

#include <ctime>
#include <string>
#include <string_view>

namespace calendar {

std::string trim(std::string_view in);
std::tm today_tm();
std::string iso_from(int y, int m, int d);
bool parse_iso(const std::string& iso, int& y, int& m, int& d);
std::string add_days(const std::string& iso, int days);
int weekday_monday(const std::string& iso);
std::string start_of_week(const std::string& iso);
int minutes_of(const std::string& hhmm);

}  // namespace calendar

#endif
