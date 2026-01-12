#ifndef TYPES_HPP
#define TYPES_HPP

#include <gtk/gtk.h>
#include <map>
#include <vector>
#include <string>

// —— 统计结构 ——
struct Stats {
    long total_seconds;
    long today_seconds;
    long week_seconds;
    long month_seconds;

    long view_daily_seconds;      // 当前查看日期的总秒数
    long view_daily_buckets[12];  // 当前查看日期的分布桶

    long week_days[7];      // 周一到周日

    std::vector<long> month_day_seconds;
    int month_year;
    int month_month;

    // 每日总秒数map
    std::map<time_t, long> history_map;
    // 每日分桶详情map
    std::map<time_t, std::vector<long>> daily_detail_map;

    // 标记数据是否已加载
    bool loaded;
};

// 用于日视图的控件包
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *label_total_time; // 显示 "总时长: XX"
    GtkWidget *label_year;       // 显示 "2023"
    GtkWidget *label_date;       // 显示 "10月27日"
} DailyViewWidgets;

// 用于月视图的控件包
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *label_title;
} MonthViewWidgets;


// 声明常量
extern const std::string BASE_DIR;
extern const std::string LOG_DIR;
extern const std::string ETC_ENABLE_FILE;
extern const std::string SETUP_SCRIPT;
extern const std::string ARCHIVE_FILE;
extern const std::string CONFIG_FILE;

extern const char *LOG_PREFIX; 
extern const char *TEMP_LOG_FILE;

extern const int DEFAULT_TARGET_MINUTES;
extern const int MIN_TARGET_MINUTES;
extern const int MAX_TARGET_MINUTES;

// 声明全局变量
extern int g_daily_target_minutes;
extern std::string g_share_domain;
extern GdkColor white;

extern Stats g_stats;
extern int g_view_year;
extern int g_view_month;
extern time_t g_view_daily_ts;

extern GtkWidget *g_notebook;
extern DailyViewWidgets *g_daily_widgets;

#endif