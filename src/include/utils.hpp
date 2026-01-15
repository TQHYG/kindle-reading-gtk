#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <ctime>

// 时间工具
time_t get_day_start(time_t t);
void get_today_bounds(time_t &today_start, time_t &tomorrow_start);
void get_week_start(time_t &week_start);
void get_month_start(time_t &month_start, int &year, int &month);
int days_in_month(int y, int m);
void format_hms(long sec, char *buf, size_t sz);

void run_command(const char* cmd);
bool file_exists(const std::string& path);
void refresh_all_status_labels();


std::string base64_encode(const std::string& in);

// 配置管理
void save_target_config();
void load_target_config();

#endif
