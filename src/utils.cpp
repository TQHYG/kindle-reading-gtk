#include "types.hpp"
#include "utils.hpp"

#include <cairo.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

time_t get_day_start(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    return mktime(&tmv);
}

void get_today_bounds(time_t &today_start, time_t &tomorrow_start) {
    time_t now = time(NULL);
    today_start = get_day_start(now);
    tomorrow_start = today_start + 24 * 3600;
}

void get_week_start(time_t &week_start) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int wday = tmv.tm_wday; // 0=Sun
    int offset = (wday == 0 ? 6 : wday - 1);
    time_t today_start = get_day_start(now);
    week_start = today_start - offset * 24 * 3600;
}

void get_month_start(time_t &month_start, int &year, int &month) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    year = tmv.tm_year + 1900;
    month = tmv.tm_mon + 1;
    tmv.tm_mday = 1;
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    month_start = mktime(&tmv);
}

int days_in_month(int y, int m) {
    static int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int d = md[m-1];
    if (m == 2) {
        bool leap = (y%4==0 && (y%100!=0 || y%400==0));
        if (leap) d = 29;
    }
    return d;
}

void format_hms(long sec, char *buf, size_t sz) {
    long h = sec / 3600;
    long m = (sec % 3600) / 60;
    long s = sec % 60;
    snprintf(buf, sz, "%4ldH %02ldm %02lds", h, m, s);
}

void save_target_config() {
    // 确保目录存在
    std::string dir = CONFIG_FILE.substr(0, CONFIG_FILE.find_last_of('/'));
    mkdir(dir.c_str(), 0755);
    
    FILE *fp = fopen(CONFIG_FILE.c_str(), "w");
    if (fp) {
        fprintf(fp, "daily_target_minutes=%d\n", g_daily_target_minutes);
        fprintf(fp, "share_domain=%s\n", g_share_domain.c_str()); 
        fclose(fp);
    }
}

void load_target_config() {
    // 首先设置默认值
    g_daily_target_minutes = DEFAULT_TARGET_MINUTES;
    g_share_domain = "reading.tqhyg.net";
    
    FILE *fp = fopen(CONFIG_FILE.c_str(), "r");
    if (!fp) {
        // 文件不存在，创建默认配置
        save_target_config();
        return;
    }
    
    // 一次读取解析所有配置项
    char line[512];
    bool has_target = false;
    bool has_domain = false;
    
    while (fgets(line, sizeof(line), fp)) {
        // 移除换行符
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // 解析每日目标分钟数
        if (strncmp(line, "daily_target_minutes=", 21) == 0) {
            int value = atoi(line + 21);
            if (value >= MIN_TARGET_MINUTES && value <= MAX_TARGET_MINUTES) {
                g_daily_target_minutes = value;
            }
            has_target = true;
        }
        // 解析分享域名
        else if (strncmp(line, "share_domain=", 13) == 0) {
            std::string domain = line + 13;
            if (!domain.empty()) {
                g_share_domain = domain;
            }
            has_domain = true;
        }
    }
    
    fclose(fp);
    
    // 如果配置项缺失，补全配置
    if (!has_target || !has_domain) {
        save_target_config();
    }
}