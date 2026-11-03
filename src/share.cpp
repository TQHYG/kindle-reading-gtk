#include "types.hpp"
#include "utils.hpp"
#include "share.hpp"

#include <cstdio>
#include <ctime>
#include <vector>

// 生成当前自然月的分享 URL。
// 使用内存中的 history_map / daily_detail_map，不改变当前界面浏览的月份。
std::string generate_share_url() {
    time_t now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    int year = now_tm.tm_year + 1900;
    int month = now_tm.tm_mon + 1;
    int days = days_in_month(year, month);

    std::string url = share_base_url() + "/share.html?";
    char buf[64];
    snprintf(buf, sizeof(buf), "year=%d&month=%d&goal=%d", year, month, g_daily_target_minutes);
    url += buf;

    struct tm day_tm = now_tm;
    day_tm.tm_hour = 0;
    day_tm.tm_min = 0;
    day_tm.tm_sec = 0;
    for (int d = 1; d <= days; d++) {
        day_tm.tm_mday = d;
        day_tm.tm_isdst = -1;
        time_t day_ts = mktime(&day_tm);
        long seconds = 0;
        auto it = g_stats.history_map.find(day_ts);
        if (it != g_stats.history_map.end()) {
            seconds = it->second;
        }
        long minutes = (seconds + 59) / 60;
        snprintf(buf, sizeof(buf), "&%d=%ld", d, minutes);
        url += buf;
    }

    time_t today_start = get_day_start(now);
    std::vector<long> buckets(12, 0);
    auto detail = g_stats.daily_detail_map.find(today_start);
    if (detail != g_stats.daily_detail_map.end() && detail->second.size() >= 12) {
        for (int i = 0; i < 12; i++) {
            buckets[i] = detail->second[i];
        }
    }
    for (int i = 0; i < 12; i++) {
        long minutes = (buckets[i] + 59) / 60;
        snprintf(buf, sizeof(buf), "&d%d=%ld", i + 1, minutes);
        url += buf;
    }

    return url;
}
