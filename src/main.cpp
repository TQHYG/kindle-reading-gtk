// main.cpp - Kindle GTK2 reading stats app (multi-file + separate week view)
// Author: Copilot (for Kindle GTK2.0)

#include <gtk/gtk.h>
#include <cairo.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <map>
#include <algorithm>

// —— 路径常量定义 ——
// 基础路径
static const std::string BASE_DIR = "/mnt/us/extensions/kykky/";

// 派生路径
static const std::string LOG_DIR = BASE_DIR + "log/";
static const std::string ETC_ENABLE_FILE = BASE_DIR + "etc/enable";
static const std::string SETUP_SCRIPT = BASE_DIR + "bin/metrics_setup.sh";
static const std::string ARCHIVE_FILE = BASE_DIR + "log/history.gz";
static const std::string CONFIG_FILE = BASE_DIR + "etc/config.ini";

static const char *LOG_PREFIX = "metrics_reader_"; 
static const char *TEMP_LOG_FILE = "/tmp/kykky_history.log";

static const int DEFAULT_TARGET_MINUTES = 30;
static const int MIN_TARGET_MINUTES = 10;
static const int MAX_TARGET_MINUTES = 180;
static int g_daily_target_minutes = DEFAULT_TARGET_MINUTES;

static GdkColor white = {0, 0xffff, 0xffff, 0xffff};

// —— 统计结构 ——
struct Stats {
    long total_seconds;
    long today_seconds;
    long week_seconds;
    long month_seconds;

    long today_buckets[12]; // 每 2 小时
    long week_days[7];      // 周一到周日

    std::vector<long> month_day_seconds;
    int month_year;
    int month_month;

    // 全量历史记录：Key=当日0点时间戳, Value=当日阅读秒数
    std::map<time_t, long> history_map;

    // 标记数据是否已加载
    bool loaded;
};

static Stats g_stats;
static int g_view_year;
static int g_view_month;

// —— 时间工具 ——
static time_t get_day_start(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    return mktime(&tmv);
}

static void get_today_bounds(time_t &today_start, time_t &tomorrow_start) {
    time_t now = time(NULL);
    today_start = get_day_start(now);
    tomorrow_start = today_start + 24 * 3600;
}

static void get_week_start(time_t &week_start) {
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    int wday = tmv.tm_wday; // 0=Sun
    int offset = (wday == 0 ? 6 : wday - 1);
    time_t today_start = get_day_start(now);
    week_start = today_start - offset * 24 * 3600;
}

static void get_month_start(time_t &month_start, int &year, int &month) {
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

static int days_in_month(int y, int m) {
    static int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int d = md[m-1];
    if (m == 2) {
        bool leap = (y%4==0 && (y%100!=0 || y%400==0));
        if (leap) d = 29;
    }
    return d;
}

// —— 配置管理 ——
static void save_target_config() {
    // 确保目录存在
    std::string dir = CONFIG_FILE.substr(0, CONFIG_FILE.find_last_of('/'));
    mkdir(dir.c_str(), 0755);
    
    FILE *fp = fopen(CONFIG_FILE.c_str(), "w");
    if (fp) {
        fprintf(fp, "daily_target_minutes=%d\n", g_daily_target_minutes);
        fclose(fp);
    }
}

static void load_target_config() {
    FILE *fp = fopen(CONFIG_FILE.c_str(), "r");
    if (fp) {
        int value = DEFAULT_TARGET_MINUTES;
        if (fscanf(fp, "daily_target_minutes=%d", &value) == 1) {
            if (value >= MIN_TARGET_MINUTES && value <= MAX_TARGET_MINUTES) {
                g_daily_target_minutes = value;
            }
        }
        fclose(fp);
    } else {
        // 文件不存在，创建默认配置
        save_target_config();
    }
}


// —— 辅助函数：解析单行并更新 Stats ——
static void parse_line_and_update(char *line, Stats &s, 
                                  time_t today_start, time_t tomorrow_start,
                                  time_t week_start, time_t week_end,
                                  time_t cur_month_start, time_t cur_month_end) 
{
    char *saveptr = NULL;
    char *token = strtok_r(line, ",", &saveptr);
    int idx = 0;
    long endt = 0;
    long dur_ms = 0;
    char type_field[256] = {0};

    while (token) {
        idx++;
        if (idx == 2) endt = strtol(token, NULL, 10);
        else if (idx == 6) strncpy(type_field, token, 255);
        else if (idx == 7) dur_ms = strtol(token, NULL, 10);
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (strncmp(type_field, "com.lab126.booklet.reader.activeDuration", 40) != 0)
        return;

    long dur = dur_ms / 1000;
    if (dur <= 0) return;

    time_t end_time = (time_t)endt;
    time_t start_time = end_time - dur;

    s.total_seconds += dur;

    // 1. 更新今日/本周/本月 总数 (保持原有逻辑，用于概览页快速显示)
    if (end_time > today_start && start_time < tomorrow_start) {
        time_t sclip = std::max(start_time, today_start);
        time_t eclip = std::min(end_time, tomorrow_start);
        if (eclip > sclip) s.today_seconds += (eclip - sclip);
    }

    if (end_time > week_start && start_time < week_end) {
        time_t sclip = std::max(start_time, week_start);
        time_t eclip = std::min(end_time, week_end);
        if (eclip > sclip) s.week_seconds += (eclip - sclip);
    }

    if (end_time > cur_month_start && start_time < cur_month_end) {
        time_t sclip = std::max(start_time, cur_month_start);
        time_t eclip = std::min(end_time, cur_month_end);
        if (eclip > sclip) s.month_seconds += (eclip - sclip);
    }

    // 2. 今日分桶 (保持不变)
    if (end_time > today_start && start_time < tomorrow_start) {
        time_t sclip = std::max(start_time, today_start);
        time_t eclip = std::min(end_time, tomorrow_start);
        time_t tcur = sclip;
        while (tcur < eclip) {
            int bi = (tcur - today_start) / (2 * 3600);
            if (bi < 0) bi = 0; 
            if (bi > 11) bi = 11;
            time_t bend = today_start + (bi + 1) * 2 * 3600;
            if (bend > eclip) bend = eclip;
            s.today_buckets[bi] += (bend - tcur);
            tcur = bend;
        }
    }

    // 3. 本周分天 (保持不变)
    if (end_time > week_start && start_time < week_end) {
        time_t sclip = std::max(start_time, week_start);
        time_t eclip = std::min(end_time, week_end);
        time_t tcur = sclip;
        while (tcur < eclip) {
            int di = (tcur - week_start) / (24 * 3600);
            if (di < 0) di = 0; 
            if (di > 6) di = 6;
            time_t dend = week_start + (di + 1) * 24 * 3600;
            if (dend > eclip) dend = eclip;
            s.week_days[di] += (dend - tcur);
            tcur = dend;
        }
    }

    // 更新全历史每日统计
    time_t t_cursor = start_time;
    while (t_cursor < end_time) {
        time_t day_start_ts = get_day_start(t_cursor);
        time_t next_day_ts = day_start_ts + 24 * 3600;
        
        time_t clip_end = std::min(end_time, next_day_ts);
        
        // 累加时长到对应日期
        if (clip_end > t_cursor) {
            s.history_map[day_start_ts] += (clip_end - t_cursor);
        }
        
        t_cursor = clip_end;
    }

}

// —— 数据预处理 ——
static void preprocess_data() {
    // 1. 确定当月文件名后缀 (YYMM)
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char current_month_suffix[16];
    // tm_year 是从1900起算的年数，% 100 得到 YY
    snprintf(current_month_suffix, sizeof(current_month_suffix), "%02d%02d", 
             (tmv.tm_year + 1900) % 100, tmv.tm_mon + 1);

    char current_log_filename[128];
    snprintf(current_log_filename, sizeof(current_log_filename), "%s%s", LOG_PREFIX, current_month_suffix);

    // 2. 准备临时文件
    // 如果存在 history.gz，解压覆盖到 TEMP；否则创建空文件
    char cmd[512];
    struct stat st;
    if (stat(ARCHIVE_FILE.c_str(), &st) == 0) {
        snprintf(cmd, sizeof(cmd), "gunzip -c %s > %s", ARCHIVE_FILE.c_str(), TEMP_LOG_FILE);
        system(cmd);
    } else {
        // 创建空文件
        FILE *fp = fopen(TEMP_LOG_FILE, "w");
        if (fp) fclose(fp);
    }

    // 3. 扫描目录，追加旧日志
    DIR *dir = opendir(LOG_DIR.c_str());
    if (!dir) return;

    FILE *fp_temp = fopen(TEMP_LOG_FILE, "a"); // 追加模式
    if (!fp_temp) {
        closedir(dir);
        return;
    }

    struct dirent *ent;
    bool has_updates = false;
    char filepath[512];

    while ((ent = readdir(dir)) != NULL) {
        // 筛选 metrics_reader_ 开头
        if (strncmp(ent->d_name, LOG_PREFIX, strlen(LOG_PREFIX)) != 0) continue;
        
        // 跳过当月日志
        if (strcmp(ent->d_name, current_log_filename) == 0) continue;

        // 处理旧日志
        snprintf(filepath, sizeof(filepath), "%s%s", LOG_DIR.c_str(), ent->d_name);
        
        FILE *fp_old = fopen(filepath, "r");
        if (fp_old) {
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), fp_old)) {
                fputs(buffer, fp_temp);
            }
            fclose(fp_old);
            unlink(filepath); // 删除旧文件
            has_updates = true;
        }
    }
    fclose(fp_temp);
    closedir(dir);

    // 4. 如果有追加操作，重新压缩归档 (保存到 LOG_DIR)
    if (has_updates) {
        snprintf(cmd, sizeof(cmd), "gzip -c %s > %s", TEMP_LOG_FILE, ARCHIVE_FILE.c_str());
        system(cmd);
    }
}

// —— 读取日志与计算 ——
// 参数说明：
// force_reload: true=重新读取磁盘文件; false=仅重新生成月视图数据(用于翻页)
static void read_logs_and_compute_stats(Stats &s, int view_year, int view_month, bool force_reload) {
    
    // 1. 初始化/清理 (解决 memset 问题)
    // 只有在强制重载时，才清空所有核心数据
    if (force_reload || !s.loaded) {
        s.total_seconds = 0;
        s.today_seconds = 0;
        s.week_seconds = 0;
        s.month_seconds = 0;
        
        // 使用 std::fill 初始化数组，安全且标准
        std::fill(std::begin(s.today_buckets), std::end(s.today_buckets), 0);
        std::fill(std::begin(s.week_days), std::end(s.week_days), 0);
        
        // 清空 Map
        s.history_map.clear();
        s.loaded = true;

        // --- 准备时间边界 (用于 parse_line 里的判断) ---
        time_t today_start, tomorrow_start;
        get_today_bounds(today_start, tomorrow_start);

        time_t week_start;
        get_week_start(week_start);
        time_t week_end = week_start + 7 * 24 * 3600;

        time_t cur_month_start;
        int cur_year, cur_month;
        get_month_start(cur_month_start, cur_year, cur_month);
        time_t cur_month_end = cur_month_start + days_in_month(cur_year, cur_month) * 24 * 3600;

        // --- 读取文件 Lambda ---
        auto process_file = [&](const char* fpath) {
            FILE *fp = fopen(fpath, "r");
            if (!fp) return;
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                // 调用简化后的解析函数
                parse_line_and_update(line, s, 
                    today_start, tomorrow_start,
                    week_start, week_end,
                    cur_month_start, cur_month_end
                );
            }
            fclose(fp);
        };

        // 读取历史汇总
        process_file(TEMP_LOG_FILE);

        // 读取当月实时日志
        time_t now = time(NULL);
        struct tm now_tm;
        localtime_r(&now, &now_tm);
        char current_path[256];
        snprintf(current_path, sizeof(current_path), "%s%s%02d%02d", 
                 LOG_DIR.c_str(), LOG_PREFIX, (now_tm.tm_year + 1900) % 100, now_tm.tm_mon + 1);
        process_file(current_path);
    }

    // 2. 生成视图数据 (优化部分)
    // 无论是否重读文件，都根据 view_year/view_month 从 map 中提取数据
    s.month_year = view_year;
    s.month_month = view_month;

    int vdays = days_in_month(view_year, view_month);
    s.month_day_seconds.assign(vdays, 0); // 重置并调整大小

    // 构造该月每一天的时间戳，去 Map 里查
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = view_year - 1900;
    tmv.tm_mon = view_month - 1;
    tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;

    for (int d = 1; d <= vdays; d++) {
        tmv.tm_mday = d;
        time_t day_ts = mktime(&tmv); // 获取该日0点时间戳
        
        // 如果 Map 里有记录，就填入 vector
        if (s.history_map.count(day_ts)) {
            s.month_day_seconds[d - 1] = s.history_map[day_ts];
        }
    }
}

// —— 格式化时间 ——
static void format_hms(long sec, char *buf, size_t sz) {
    long h = sec / 3600;
    long m = (sec % 3600) / 60;
    long s = sec % 60;
    snprintf(buf, sz, "%4ldH %02ldm %02lds", h, m, s);
}

// —— 今日分布绘图 ——
static gboolean draw_today_dist(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    int w = widget->allocation.width;
    int h = widget->allocation.height;

    int left = 50, right = 20, top = 60, bottom = 60;

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);

    cairo_move_to(cr, left, top);
    cairo_line_to(cr, left, h - bottom);
    cairo_line_to(cr, w - right, h - bottom);
    cairo_stroke(cr);

    long maxv = 3600;
    for (int i = 0; i < 12; i++)
        if (g_stats.today_buckets[i] > maxv) maxv = g_stats.today_buckets[i];

    int chart_w = w - left - right;
    int chart_h = h - top - bottom - 100;

    double bar_space = chart_w / 12.0;
    double bar_w = bar_space * 0.8;

    for (int i = 0; i < 12; i++) {
        double x = left + bar_space * i + (bar_space - bar_w) / 2;
        double val = g_stats.today_buckets[i];
        double bh = (val / (double)maxv) * chart_h;
        double y = h - bottom - bh;

        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_rectangle(cr, x, y, bar_w, bh);
        cairo_fill(cr);

        long mins = val / 60;
        if (mins > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ldm", mins);
            cairo_set_font_size(cr, 40);
            cairo_move_to(cr, x - 25 + bar_w/4, y - 5);
            cairo_show_text(cr, buf);
        }

        char label[16];
        snprintf(label, sizeof(label), "%02d-%02d", i*2, i*2+2);
        cairo_set_font_size(cr, 25);
        cairo_move_to(cr, x, h - bottom + 25);
        cairo_show_text(cr, label);
    }

    int best = 0;
    for (int i = 1; i < 12; i++)
        if (g_stats.today_buckets[i] > g_stats.today_buckets[best]) best = i;

    char comment[128];
    snprintf(comment, sizeof(comment),
             "你最常阅读的时间段是 %02d:00-%02d:00", best*2, best*2+2);

    cairo_set_font_size(cr, 40);
    cairo_move_to(cr, left + 20, top + 40);
    cairo_show_text(cr, comment);

    char today_total_str[64];
    format_hms(g_stats.today_seconds, today_total_str, sizeof(today_total_str));

    char today_title[128];
    snprintf(today_title, sizeof(today_title), "今日总时长: %s", today_total_str);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_font_size(cr, 50);
    cairo_move_to(cr, left, top - 10);
    cairo_show_text(cr, today_title);

    cairo_destroy(cr);
    return FALSE;
}

static GtkWidget* create_today_page() {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 800, 500);
    g_signal_connect(G_OBJECT(da), "expose-event",
                     G_CALLBACK(draw_today_dist), NULL);
    return da;
}


// —— 本周分布绘图（柱状图） ——
static gboolean draw_week_dist(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    int w = widget->allocation.width;
    int h = widget->allocation.height;

    int left = 60, right = 20, top = 60, bottom = 60;

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);

    cairo_move_to(cr, left, top);
    cairo_line_to(cr, left, h - bottom);
    cairo_line_to(cr, w - right, h - bottom);
    cairo_stroke(cr);

    long maxv = 7200;
    for (int i = 0; i < 7; i++)
        if (g_stats.week_days[i] > maxv) maxv = g_stats.week_days[i];

    int chart_w = w - left - right;
    int chart_h = h - top - bottom - 100;

    double bar_space = chart_w / 7.0;
    double bar_w = bar_space * 0.8;

    const char *names[7] = {"周一","周二","周三","周四","周五","周六","周日"};

    for (int i = 0; i < 7; i++) {
        double x = left + bar_space * i + (bar_space - bar_w)/2;
        double val = g_stats.week_days[i];
        double bh = (val / (double)maxv) * chart_h;
        double y = h - bottom - bh;

        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_rectangle(cr, x, y, bar_w, bh);
        cairo_fill(cr);

        long mins = val / 60;
        if (mins > 0) {
            char buf[32];
            cairo_set_font_size(cr, 45);
            snprintf(buf, sizeof(buf), "%ldm", mins);
            cairo_move_to(cr, x -25 + bar_w/4, y - 5);
            cairo_show_text(cr, buf);
        }

        cairo_set_font_size(cr, 40);
        cairo_move_to(cr, x+18, h - bottom + 40);
        cairo_show_text(cr, names[i]);
    }

    int best = 0;
    for (int i = 1; i < 7; i++)
        if (g_stats.week_days[i] > g_stats.week_days[best]) best = i;

    char comment[128];
    snprintf(comment, sizeof(comment),
             "本周你读得最多的一天是 %s", names[best]);

    cairo_set_font_size(cr, 40);
    cairo_move_to(cr, left + 20, top + 40);
    cairo_show_text(cr, comment);

    char week_total_str[64];
    format_hms(g_stats.week_seconds, week_total_str, sizeof(week_total_str));

    char week_title[128];
    snprintf(week_title, sizeof(week_title), "本周总时长: %s", week_total_str);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_font_size(cr, 50);
    cairo_move_to(cr, left, top - 10);
    cairo_show_text(cr, week_title);

    cairo_destroy(cr);
    return FALSE;
}

static GtkWidget* create_week_page() {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 800, 500);
    g_signal_connect(G_OBJECT(da), "expose-event",
                     G_CALLBACK(draw_week_dist), NULL);
    return da;
}


// —— 月视图（与前段一致） ——
typedef struct {
    GtkWidget *drawing_area;
    GtkWidget *label_title;
} MonthViewWidgets;

// 将比例值（0.0-1.0）映射到16阶灰度值（0.0-1.0）
// ratio=0.0 → 灰度=1.0（白色）
// ratio=1.0 → 灰度=0.0（黑色）
static double get_gray_level_16(double ratio) {
    if (ratio <= 0.0) return 1.0; // 完全白色
    if (ratio >= 1.0) return 0.0; // 完全黑色
    
    // 将0-1的比例映射到8-15的阶梯
    int level = (int)(ratio * 7.99); // 0-15
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    level += 8;
    
    // 转换为灰度值：level=0→白(1.0), level=15→黑(0.0)
    return 1.0 - (level / 15.0);
}

static gboolean draw_month_view(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    int w = widget->allocation.width;
    int h = widget->allocation.height;

    int left = 20, top = 10, right = 20, bottom = 10;

    int grid_w = w - left - right;
    int grid_h = h - top - bottom;

    int rows = 7, cols = 7;
    double cw = grid_w / (double)cols;
    double ch = grid_h / (double)rows;

    const char *weeknames[7] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    cairo_set_source_rgb(cr, 1, 1, 1); 
    cairo_rectangle(cr, left, top, grid_w, ch);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0, 0, 0); 
    cairo_set_font_size(cr, 40);
    for (int i = 0; i < 7; i++) {
        double x = left + i * cw + cw/2 - 40; 
        double y = top + ch/2 + 15; 
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, weeknames[i]);
    }

    int year = g_stats.month_year;
    int month = g_stats.month_month;
    int days = g_stats.month_day_seconds.size();

    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = 1;
    time_t t0 = mktime(&tmv);
    localtime_r(&t0, &tmv);

    int wday = tmv.tm_wday;
    int first_col = (wday == 0 ? 6 : wday - 1);

    // 找出本月阅读时间最长的一天作为基准
    long basic_sec = g_daily_target_minutes * 60; // 最小基准值
    long max_seconds = g_daily_target_minutes * 60; //默认最长值
    for (int i = 0; i < days; i++) {
        if (g_stats.month_day_seconds[i] > max_seconds) {
            max_seconds = g_stats.month_day_seconds[i];
        }
    }

    for (int d = 1; d <= days; d++) {
        int idx = d - 1;
        int off = first_col + idx;
        int r = off / cols + 1;
        int c = off % cols;

        double x = left + c * cw;
        double y = top + r * ch;

        long sec = g_stats.month_day_seconds[idx];
        
        // 计算该天相对于最大值的比例
        double ratio = 0.0;
        if ( (double)sec > (double)basic_sec ) {
            ratio = (double)sec / (double)max_seconds;
        }
        
        // 获取对应的16阶灰度值（0.0=黑，1.0=白）
        double gray = get_gray_level_16(ratio);
        
        // 设置背景色（灰度值）
        cairo_set_source_rgb(cr, gray, gray, gray);
        cairo_rectangle(cr, x, y, cw, ch);
        cairo_fill(cr);
        
        // 根据背景灰度决定文字颜色
        // 灰度 < 0.5（较暗）用白字，>= 0.5（较亮）用黑字
        if (gray < 0.5) {
            cairo_set_source_rgb(cr, 1, 1, 1); // 白字
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0); // 黑字
        }
        
        // 绘制边框（黑色）
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_rectangle(cr, x, y, cw, ch);
        cairo_stroke(cr);
        
        // 恢复文字颜色
        if (gray < 0.5) {
            cairo_set_source_rgb(cr, 1, 1, 1);
        } else {
            cairo_set_source_rgb(cr, 0, 0, 0);
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "%d", d);
        cairo_set_font_size(cr, 40);
        cairo_move_to(cr, x + 10, y + 50);
        cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "%ldH:%02ldm", sec/3600, sec/60%60);
        cairo_set_font_size(cr, 32);
        cairo_move_to(cr, x + 10, y + 90);
        cairo_show_text(cr, buf);
    }

    char month_total_str[64];
    long month_total_seconds = 0;
    for (size_t i = 0; i < g_stats.month_day_seconds.size(); i++) {
        month_total_seconds += g_stats.month_day_seconds[i];
    }
    format_hms(month_total_seconds, month_total_str, sizeof(month_total_str));

    char month_title[128];
    snprintf(month_title, sizeof(month_title), "本月总时长: %s", month_total_str);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_font_size(cr, 50);
    cairo_move_to(cr, left, h - 40); 
    cairo_show_text(cr, month_title);

    cairo_destroy(cr);
    return FALSE;
}

static void update_month_title(MonthViewWidgets *mv) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d年%02d月", g_view_year, g_view_month);
    gtk_label_set_text(GTK_LABEL(mv->label_title), buf);
}

static void month_prev(GtkButton *b, gpointer data) {
    MonthViewWidgets *mv = (MonthViewWidgets*)data;
    g_view_month--;
    if (g_view_month < 1) {
        g_view_month = 12;
        g_view_year--;
    }
    // [优化] 只查 Map
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, false);
    update_month_title(mv);
    gtk_widget_queue_draw(mv->drawing_area);
}

static void month_next(GtkButton *b, gpointer data) {
    MonthViewWidgets *mv = (MonthViewWidgets*)data;
    g_view_month++;
    if (g_view_month > 12) {
        g_view_month = 1;
        g_view_year++;
    }
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, false);
    update_month_title(mv);
    gtk_widget_queue_draw(mv->drawing_area);
}

static GtkWidget* create_month_page() {
    GtkWidget *vbox = gtk_vbox_new(FALSE, 5);

    MonthViewWidgets *mv = (MonthViewWidgets*)g_malloc0(sizeof(MonthViewWidgets));

    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    GtkWidget *btn_prev = gtk_button_new_with_label("<-上个月");
    GtkWidget *btn_next = gtk_button_new_with_label("下个月->");
    GtkWidget *lbl = gtk_label_new("");

    mv->label_title = lbl;

    gtk_box_pack_start(GTK_BOX(hbox), btn_prev, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_next, FALSE, FALSE, 5);

    GtkWidget *da = gtk_drawing_area_new();

    mv->drawing_area = da;
    gtk_widget_set_size_request(da, 800, 500);

    g_signal_connect(G_OBJECT(da), "expose-event",
                     G_CALLBACK(draw_month_view), NULL);

    g_signal_connect(G_OBJECT(btn_prev), "clicked",
                     G_CALLBACK(month_prev), mv);
    g_signal_connect(G_OBJECT(btn_next), "clicked",
                     G_CALLBACK(month_next), mv);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), da, TRUE, TRUE, 5);

    update_month_title(mv);

    return vbox;
}


// —— 概览页 ——
static GtkWidget* create_overview_page() {
    // 最外层白底
    GtkWidget *eventbox = gtk_event_box_new();
    gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

    // 居中用的对齐控件
    GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(eventbox), align);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 20);
    gtk_container_add(GTK_CONTAINER(align), vbox);

    bool today_target_met = g_stats.today_seconds >= (g_daily_target_minutes * 60);
    char target_status[128];
    
    if (today_target_met) {
        snprintf(target_status, sizeof(target_status), "今天的阅读目标已完成！");
    } else {
        long remaining = (g_daily_target_minutes * 60) - g_stats.today_seconds;
        snprintf(target_status, sizeof(target_status), "还差 %ld 分钟达成今日阅读目标！", 
                 remaining/60 + (remaining%60 > 0 ? 1 : 0));
    }

    // 计算连续达成天数
    int consecutive_days = 0;
    long target_sec = g_daily_target_minutes * 60;
    time_t now = time(NULL);
    time_t loop_day = get_day_start(now);
    
    // 如果今天已经达标，从今天开始算；如果今天还没达标，从昨天开始算
    if (g_stats.history_map[loop_day] < target_sec) {
        loop_day -= 24 * 3600; // 回退到昨天
    }

    // 向前回溯统计
    while (true) {
        // 查找该日期是否有记录且达标
        if (g_stats.history_map.count(loop_day) && g_stats.history_map[loop_day] >= target_sec) {
            consecutive_days++;
            loop_day -= 24 * 3600; // 前一天
        } else {
            break; // 中断
        }
    }

    //计算本月达成天数
    int month_target_days = 0;
    time_t m_start;
    int m_year, m_mon;
    get_month_start(m_start, m_year, m_mon); // 获取本月1号0点
    
    int days_in_cur_month = days_in_month(m_year, m_mon);
    time_t m_end_bound = m_start + days_in_cur_month * 24 * 3600;

    // 遍历本月每一天
    for (time_t d = m_start; d < m_end_bound; d += 24 * 3600) {
        if (d > now) break; // 未来的日子不算
        if (g_stats.history_map.count(d) && g_stats.history_map[d] >= target_sec) {
            month_target_days++;
        }
    }
    
    char consecutive_str[64];
    char month_target_str[64];
    snprintf(consecutive_str, sizeof(consecutive_str), "连续达成目标 %d 天", consecutive_days);
    snprintf(month_target_str, sizeof(month_target_str), "本月目标达成 %d 天", month_target_days);

    // 创建标签
    GtkWidget *label_target_status = gtk_label_new(target_status);
    GtkWidget *label_consecutive = gtk_label_new(consecutive_str);
    GtkWidget *label_month_target = gtk_label_new(month_target_str);
    
    // 设置字体大小
    PangoFontDescription *font_small = pango_font_description_from_string("Sans 16");
    gtk_widget_modify_font(label_target_status, font_small);
    gtk_widget_modify_font(label_consecutive, font_small);
    gtk_widget_modify_font(label_month_target, font_small);


    char buf_today[64], buf_total[64];
    format_hms(g_stats.today_seconds, buf_today, sizeof(buf_today));
    format_hms(g_stats.total_seconds, buf_total, sizeof(buf_total));

    // 今日时长
    GtkWidget *label_today_title = gtk_label_new("今日时长");
    GtkWidget *label_today_time  = gtk_label_new(buf_today);

    // 总计阅读
    GtkWidget *label_total_title = gtk_label_new("总计阅读");
    GtkWidget *label_total_time  = gtk_label_new(buf_total);

    // 标题字体
    PangoFontDescription *font_title = pango_font_description_from_string("Sans 22");
    gtk_widget_modify_font(label_today_title, font_title);
    gtk_widget_modify_font(label_total_title, font_title);
    pango_font_description_free(font_title);

    // 时间字体
    PangoFontDescription *font_time = pango_font_description_from_string("Sans 32");
    gtk_widget_modify_font(label_today_time, font_time);
    gtk_widget_modify_font(label_total_time, font_time);
    pango_font_description_free(font_time);

    // 文字居中
    gtk_label_set_justify(GTK_LABEL(label_today_title), GTK_JUSTIFY_CENTER);
    gtk_label_set_justify(GTK_LABEL(label_today_time),  GTK_JUSTIFY_CENTER);
    gtk_label_set_justify(GTK_LABEL(label_total_title), GTK_JUSTIFY_CENTER);
    gtk_label_set_justify(GTK_LABEL(label_total_time),  GTK_JUSTIFY_CENTER);
    gtk_misc_set_alignment(GTK_MISC(label_today_title), 0.5, 0.5);
    gtk_misc_set_alignment(GTK_MISC(label_today_time),  0.5, 0.5);
    gtk_misc_set_alignment(GTK_MISC(label_total_title), 0.5, 0.5);
    gtk_misc_set_alignment(GTK_MISC(label_total_time),  0.5, 0.5);
    gtk_label_set_justify(GTK_LABEL(label_target_status), GTK_JUSTIFY_CENTER);
    gtk_label_set_justify(GTK_LABEL(label_consecutive), GTK_JUSTIFY_CENTER);
    gtk_label_set_justify(GTK_LABEL(label_month_target), GTK_JUSTIFY_CENTER);
    gtk_misc_set_alignment(GTK_MISC(label_target_status), 0.5, 0.5);
    gtk_misc_set_alignment(GTK_MISC(label_consecutive), 0.5, 0.5);
    gtk_misc_set_alignment(GTK_MISC(label_month_target), 0.5, 0.5);

    // 添加到vbox
    gtk_box_pack_start(GTK_BOX(vbox), label_target_status, FALSE, FALSE, 5);
    
    GtkWidget *sep1 = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vbox), sep1, FALSE, FALSE, 16);

    gtk_box_pack_start(GTK_BOX(vbox), label_today_title, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), label_today_time,  FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), label_total_title, FALSE, FALSE, 20);
    gtk_box_pack_start(GTK_BOX(vbox), label_total_time,  FALSE, FALSE, 5);
    
    GtkWidget *sep2 = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vbox), sep2, FALSE, FALSE, 16);

    gtk_box_pack_start(GTK_BOX(vbox), label_consecutive, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(vbox), label_month_target, FALSE, FALSE, 5);

    pango_font_description_free(font_small);

    return eventbox;
}

// —— 设置页辅助逻辑 ——

static void style_button(GtkWidget *btn, const char *text) {
    // 设置按钮文字
    gtk_button_set_label(GTK_BUTTON(btn), text);
    
    // 设置按钮大小
    gtk_widget_set_size_request(btn, 150, 75);
}

// 获取日志目录占用空间 (KiB)
static long get_log_dir_size_kib() {
    long total_bytes = 0;
    DIR *d = opendir(LOG_DIR.c_str());
    if (d) {
        struct dirent *p;
        while ((p = readdir(d))) {
            if (p->d_name[0] == '.') continue;
            std::string path = LOG_DIR + p->d_name;
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                total_bytes += st.st_size;
            }
        }
        closedir(d);
    }
    return total_bytes / 1024;
}

// 检查是否启用了统计 (文件是否存在)
static bool is_metrics_enabled() {
    struct stat st;
    return (stat(ETC_ENABLE_FILE.c_str(), &st) == 0);
}

// 开关回调

static void on_toggle_enable_button(GtkButton *btn, gpointer data) {
    // 获取当前按钮文字
    const char *current = gtk_button_get_label(btn);
    bool active = (strcmp(current, "已启用") == 0);
    
    // 切换状态
    active = !active;
    
    // 执行命令
    std::string cmd = SETUP_SCRIPT + (active ? " enable" : " disable");
    system(cmd.c_str());
    
    // 更新按钮显示
    gtk_button_set_label(btn, active ? "已启用" : "已停用");
}

// 清除数据回调 (需要传入 Label 指针以更新显示的占用空间)
static void on_reset_data(GtkButton *btn, gpointer user_data) {
    GtkWidget *size_label = GTK_WIDGET(user_data);
    
    // 创建确认对话框
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog),
        "L:D_N:dialog_PC:T_ID:net.tqhyg.reading.dialog");
    //gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 300);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 20);
    
    // 设置对话框为模态
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    
    // 设置对话框背景色
    GdkColor white = {0, 0xffff, 0xffff, 0xffff};
    gtk_widget_modify_bg(dialog, GTK_STATE_NORMAL, &white);
    
    // 创建对话框内容区域
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // 创建消息标签
    GtkWidget *label = gtk_label_new("! 警告 !\n\n确定要清除所有统计数据吗？\n此操作不可撤销！\n");
    PangoFontDescription *font = pango_font_description_from_string("Sans 16");
    gtk_widget_modify_font(label, font);
    pango_font_description_free(font);
    
    // 设置标签对齐
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    
    // 添加标签到内容区域
    gtk_box_pack_start(GTK_BOX(content_area), label, TRUE, TRUE, 20);
    
    // 添加按钮
    GtkWidget *btn_yes = gtk_button_new_with_label("确定");
    GtkWidget *btn_no = gtk_button_new_with_label("取消");
    
    // 设置按钮样式
    gtk_widget_set_size_request(btn_yes, 120, 80);
    gtk_widget_set_size_request(btn_no, 120, 80);
    
    // 添加按钮到对话框动作区域
    GtkWidget *action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(action_area), btn_yes, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(action_area), btn_no, FALSE, FALSE, 10);
    
    // 显示所有控件
    gtk_widget_show_all(dialog);
    
    // 连接按钮信号
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), btn_yes, GTK_RESPONSE_YES);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), btn_no, GTK_RESPONSE_NO);
    
    // 运行对话框
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    // 处理响应
    if (response == GTK_RESPONSE_YES) {
        // 用户确认清除数据
        std::string cmd = SETUP_SCRIPT + " reset";
        system(cmd.c_str());
        
        // 更新界面显示为 0 KiB
        gtk_label_set_text(GTK_LABEL(size_label), "0 KiB");
        
        // 安全清理
        read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, true);
    }
    
    // 销毁对话框
    gtk_widget_destroy(dialog);
}

// 设置目标函数
static void on_target_change(GtkButton *btn, gpointer data) {
    int change = GPOINTER_TO_INT(data);
    int new_target = g_daily_target_minutes + change;
    
    // 限制范围
    if (new_target < MIN_TARGET_MINUTES) new_target = MIN_TARGET_MINUTES;
    if (new_target > MAX_TARGET_MINUTES) new_target = MAX_TARGET_MINUTES;
    
    if (new_target != g_daily_target_minutes) {
        g_daily_target_minutes = new_target;
        save_target_config();
        
        // 更新显示
        GtkWidget *label = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "target_label"));
        char buf[32];
        snprintf(buf, sizeof(buf), "%d 分钟", g_daily_target_minutes);
        gtk_label_set_text(GTK_LABEL(label), buf);
    }
}

static GtkWidget* create_settings_page() {
    // 使用 VBox 垂直排列三行
    GtkWidget *vbox = gtk_vbox_new(FALSE, 20); // 间距 20
    GtkWidget *align_box = gtk_alignment_new(0.5, 0.1, 0.8, 0); // 居中靠上，宽度占80%
    gtk_container_add(GTK_CONTAINER(align_box), vbox);

    // 统一字体设置
    PangoFontDescription *row_font = pango_font_description_from_string("Sans 14");

    // --- 第一行：每日阅读目标 ---
    GtkWidget *hbox_target = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_target = gtk_label_new("每日阅读目标");
    gtk_widget_modify_font(lbl_target, row_font);
    
    // 创建控制按钮
    GtkWidget *btn_minus10 = gtk_button_new_with_label("-10");
    GtkWidget *btn_minus1 = gtk_button_new_with_label("-1");
    GtkWidget *label_target_val = gtk_label_new("");
    GtkWidget *btn_plus1 = gtk_button_new_with_label("+1");
    GtkWidget *btn_plus10 = gtk_button_new_with_label("+10");
    
    // 设置按钮大小
    gtk_widget_set_size_request(btn_minus10, 80, 50);
    gtk_widget_set_size_request(btn_minus1, 80, 50);
    gtk_widget_set_size_request(btn_plus1, 80, 50);
    gtk_widget_set_size_request(btn_plus10, 80, 50);
    
    // 更新标签显示
    char target_buf[32];
    snprintf(target_buf, sizeof(target_buf), "%d 分钟", g_daily_target_minutes);
    gtk_label_set_text(GTK_LABEL(label_target_val), target_buf);
    gtk_widget_modify_font(label_target_val, row_font);
    
    // 连接信号
    g_signal_connect(G_OBJECT(btn_minus10), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(-10));
    g_signal_connect(G_OBJECT(btn_minus1), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(-1));
    g_signal_connect(G_OBJECT(btn_plus1), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(1));
    g_signal_connect(G_OBJECT(btn_plus10), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(10));
    
    // 存储标签指针以便更新
    g_object_set_data(G_OBJECT(btn_minus10), "target_label", label_target_val);
    g_object_set_data(G_OBJECT(btn_minus1), "target_label", label_target_val);
    g_object_set_data(G_OBJECT(btn_plus1), "target_label", label_target_val);
    g_object_set_data(G_OBJECT(btn_plus10), "target_label", label_target_val);
    
    // 布局
    gtk_box_pack_start(GTK_BOX(hbox_target), lbl_target, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_plus10, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_plus1, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(hbox_target), label_target_val, FALSE, FALSE, 15);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_minus1, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_minus10, FALSE, FALSE, 5);

    // --- 第二行：启用统计功能 ---
    GtkWidget *hbox1 = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_enable = gtk_label_new("启用阅读统计");
    gtk_widget_modify_font(lbl_enable, row_font);
    
    // 使用按钮模拟开关
    GtkWidget *btn_enable = gtk_button_new();
    bool enabled = is_metrics_enabled();
    style_button(btn_enable, enabled ? "已启用" : "已停用");
    g_signal_connect(G_OBJECT(btn_enable), "clicked", 
                     G_CALLBACK(on_toggle_enable_button), NULL);

    // 布局第二行: Label 靠左，按钮靠右
    gtk_box_pack_start(GTK_BOX(hbox1), lbl_enable, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox1), btn_enable, FALSE, FALSE, 0);


    // --- 第四行：占用空间---
    GtkWidget *hbox3 = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_size_title = gtk_label_new("统计数据已占用");
    gtk_widget_modify_font(lbl_size_title, row_font);

    char size_buf[64];
    snprintf(size_buf, sizeof(size_buf), "%ld KiB", get_log_dir_size_kib());
    GtkWidget *lbl_size_val = gtk_label_new(size_buf);
    gtk_widget_modify_font(lbl_size_val, row_font);

    gtk_box_pack_start(GTK_BOX(hbox3), lbl_size_title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox3), lbl_size_val, FALSE, FALSE, 0);


    // --- 第三行：清除统计数据 ---
    GtkWidget *hbox2 = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_clear = gtk_label_new("清除统计数据");
    gtk_widget_modify_font(lbl_clear, row_font);

    GtkWidget *btn_clear = gtk_button_new_with_label("清除");
    style_button(btn_clear, "清除");
    // 传入 lbl_size_val 指针，以便清除后更新数字
    g_signal_connect(G_OBJECT(btn_clear), "clicked", G_CALLBACK(on_reset_data), lbl_size_val);

    gtk_box_pack_start(GTK_BOX(hbox2), lbl_clear, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox2), btn_clear, FALSE, FALSE, 0);


    // --- 将所有行加入 VBox ---
    gtk_box_pack_start(GTK_BOX(vbox), hbox_target, FALSE, FALSE, 10);
    GtkWidget *hsep_target = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vbox), hsep_target, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(vbox), hbox1, FALSE, FALSE, 10);
    GtkWidget *hsep1 = gtk_hseparator_new(); // 分割线
    gtk_box_pack_start(GTK_BOX(vbox), hsep1, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 10);
    GtkWidget *hsep2 = gtk_hseparator_new(); // 分割线
    gtk_box_pack_start(GTK_BOX(vbox), hsep2, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(vbox), hbox3, FALSE, FALSE, 10);

    pango_font_description_free(row_font);
    
    return align_box;
}



// —— 退出页 ——
static GtkWidget* create_exit_page() {
    // 创建一个标签，背景设为白色
    GtkWidget *label = gtk_label_new("正在退出...");
    gtk_widget_modify_bg(label, GTK_STATE_NORMAL, &white);
    
    // 设置字体大小
    PangoFontDescription *font = pango_font_description_from_string("Sans 22");
    gtk_widget_modify_font(label, font);
    pango_font_description_free(font);
    
    return label;
}

static void on_notebook_switch_page(GtkNotebook *notebook, 
                                    GtkWidget *page, 
                                    guint page_num, 
                                    gpointer user_data) {
    guint n_pages = gtk_notebook_get_n_pages(notebook);
    if (page_num == n_pages - 1) {
        gtk_main_quit();
    }
}

// —— 主函数 —— 
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // 1. 初始化窗口和主容器
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win),
        "L:A_N:application_PC:T_ID:net.tqhyg.reading");
    gtk_window_set_default_size(GTK_WINDOW(win), 1072, 1200);

    g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_modify_bg(win, GTK_STATE_NORMAL, &white);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    gtk_widget_modify_bg(vbox, GTK_STATE_NORMAL, &white);

    // 2. 创建等待界面 (居中的 Label)
    GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0, 0);
    GtkWidget *wait_label = gtk_label_new("正在处理数据...");
    PangoFontDescription *wait_font = pango_font_description_from_string("Sans 22");
    gtk_widget_modify_font(wait_label, wait_font);
    pango_font_description_free(wait_font);

    gtk_container_add(GTK_CONTAINER(align), wait_label);
    gtk_box_pack_start(GTK_BOX(vbox), align, TRUE, TRUE, 0);

    gtk_widget_show_all(win);

    // 强制 UI 渲染等待界面
    while (gtk_events_pending()) gtk_main_iteration();
    usleep(800000); //让用户觉得程序在干活

    // 3. 执行耗时的预处理和数据收集
    preprocess_data();

    load_target_config(); 

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    g_view_year = tmv.tm_year + 1900;
    g_view_month = tmv.tm_mon + 1;

    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, true);

    // 5. 销毁等待界面并切换到正式内容
    gtk_widget_destroy(align); 

    GtkWidget *nb = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(nb), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 0);

    gtk_widget_modify_bg(nb, GTK_STATE_NORMAL, &white);

    PangoFontDescription *tab_font = pango_font_description_from_string("Sans Bold 15");

    auto add_tab = [&](GtkWidget *page, const char *name) {
        GtkWidget *tab = gtk_label_new(name);
        gtk_widget_modify_font(tab, tab_font);

        if (strcmp(name, " X ") != 0) {
            gtk_widget_modify_fg(tab, GTK_STATE_ACTIVE, &white);
        }

        gtk_notebook_append_page(GTK_NOTEBOOK(nb), page, tab);
    };

    add_tab(create_overview_page(), "概览");
    add_tab(create_today_page(), "今日分布");
    add_tab(create_week_page(), "周分布");
    add_tab(create_month_page(), "阅读日历");
    add_tab(create_settings_page(), "设置");
    add_tab(create_exit_page(), " X ");

    pango_font_description_free(tab_font);

    g_signal_connect(G_OBJECT(nb), "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);

    gtk_widget_show_all(win);
    gtk_main();

    // --- 清理临时文件 ---
    unlink(TEMP_LOG_FILE);
    return 0;
}
