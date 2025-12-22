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

static const char *LOG_DIR = "/mnt/us/extensions/kykky/log/";
static const char *LOG_PREFIX = "metrics_reader_";

static GdkColor black_color = {0, 0x0000, 0x0000, 0x0000};
static GdkColor white_color = {0, 0xffff, 0xffff, 0xffff};

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

// —— 遍历读取 metrics_reader_* 文件 ——
static void read_logs_and_compute_stats(Stats &s, int view_year, int view_month) {
    memset(&s, 0, sizeof(Stats));

    time_t today_start, tomorrow_start;
    get_today_bounds(today_start, tomorrow_start);

    time_t week_start;
    get_week_start(week_start);
    time_t week_end = week_start + 7 * 24 * 3600;

    time_t cur_month_start;
    int cur_year, cur_month;
    get_month_start(cur_month_start, cur_year, cur_month);
    time_t cur_month_end = cur_month_start + days_in_month(cur_year, cur_month) * 24 * 3600;

    s.month_year = view_year;
    s.month_month = view_month;
    int vdays = days_in_month(view_year, view_month);
    s.month_day_seconds.assign(vdays, 0);

    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = view_year - 1900;
    tmv.tm_mon = view_month - 1;
    tmv.tm_mday = 1;
    time_t view_month_start = mktime(&tmv);
    time_t view_month_end = view_month_start + vdays * 24 * 3600;

    memset(s.today_buckets, 0, sizeof(s.today_buckets));
    memset(s.week_days, 0, sizeof(s.week_days));

    DIR *dir = opendir(LOG_DIR);
    if (!dir) return;

    struct dirent *ent;
    char filepath[512];

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, LOG_PREFIX, strlen(LOG_PREFIX)) != 0)
            continue;

        snprintf(filepath, sizeof(filepath), "%s%s", LOG_DIR, ent->d_name);
        FILE *fp = fopen(filepath, "r");
        if (!fp) continue;

        char line[512];
        while (fgets(line, sizeof(line), fp)) {
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
                continue;

            long dur = dur_ms / 1000;
            if (dur <= 0) continue;

            time_t end_time = (time_t)endt;
            time_t start_time = end_time - dur;

            s.total_seconds += dur;

            // 今日
            if (end_time > today_start && start_time < tomorrow_start) {
                time_t sclip = std::max(start_time, today_start);
                time_t eclip = std::min(end_time, tomorrow_start);
                if (eclip > sclip) s.today_seconds += (eclip - sclip);
            }

            // 本周
            if (end_time > week_start && start_time < week_end) {
                time_t sclip = std::max(start_time, week_start);
                time_t eclip = std::min(end_time, week_end);
                if (eclip > sclip) s.week_seconds += (eclip - sclip);
            }

            // 本月
            if (end_time > cur_month_start && start_time < cur_month_end) {
                time_t sclip = std::max(start_time, cur_month_start);
                time_t eclip = std::min(end_time, cur_month_end);
                if (eclip > sclip) s.month_seconds += (eclip - sclip);
            }

            // 今日分桶
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

            // 本周每天
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

            // 月视图
            if (end_time > view_month_start && start_time < view_month_end) {
                time_t sclip = std::max(start_time, view_month_start);
                time_t eclip = std::min(end_time, view_month_end);
                time_t tcur = sclip;
                while (tcur < eclip) {
                    int di = (tcur - view_month_start) / (24 * 3600);
                    if (di < 0) di = 0;
                    if (di >= vdays) di = vdays - 1;
                    time_t dend = view_month_start + (di + 1) * 24 * 3600;
                    if (dend > eclip) dend = eclip;
                    s.month_day_seconds[di] += (dend - tcur);
                    tcur = dend;
                }
            }
        }

        fclose(fp);
    }

    closedir(dir);
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

    long maxv = 1;
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

    long maxv = 1;
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

    for (int d = 1; d <= days; d++) {
        int idx = d - 1;
        int off = first_col + idx;
        int r = off / cols + 1;
        int c = off % cols;

        double x = left + c * cw;
        double y = top + r * ch;

        long sec = g_stats.month_day_seconds[idx];
        bool dark = (sec >= 30 * 60);

        if (dark) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, x, y, cw, ch);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
        } else {
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_rectangle(cr, x, y, cw, ch);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
        }

        cairo_rectangle(cr, x, y, cw, ch);
        cairo_stroke(cr);

        char buf[32];
        snprintf(buf, sizeof(buf), "%d", d);
        cairo_set_font_size(cr, 40);
        cairo_move_to(cr, x + 10, y + 50);
        cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "%dH:%02dm", sec/3600, sec/60%60);
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
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month);
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
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month);
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
    GdkColor white = {0, 0xffff, 0xffff, 0xffff};
    gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

    // 居中用的对齐控件
    GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(eventbox), align);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 20);
    gtk_container_add(GTK_CONTAINER(align), vbox);

    char buf_today[64], buf_total[64];
    format_hms(g_stats.today_seconds, buf_today, sizeof(buf_today));
    format_hms(g_stats.total_seconds, buf_total, sizeof(buf_total));

    // 今日时长
    GtkWidget *label_today_title = gtk_label_new("今日时长");
    GtkWidget *label_today_time  = gtk_label_new(buf_today);

    // 总计阅读
    GtkWidget *label_total_title = gtk_label_new("总计阅读");
    GtkWidget *label_total_time  = gtk_label_new(buf_total);

    // 标题字体稍大
    PangoFontDescription *font_title = pango_font_description_from_string("Sans 22");
    gtk_widget_modify_font(label_today_title, font_title);
    gtk_widget_modify_font(label_total_title, font_title);
    pango_font_description_free(font_title);

    // 时间字体更大
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

    gtk_box_pack_start(GTK_BOX(vbox), label_today_title, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), label_today_time,  FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), label_total_title, FALSE, FALSE, 20);
    gtk_box_pack_start(GTK_BOX(vbox), label_total_time,  FALSE, FALSE, 5);

    return eventbox;
}



// —— 退出页 ——
static GtkWidget* create_exit_page() {
    // 创建一个标签，背景设为白色
    GtkWidget *label = gtk_label_new("正在退出...");
    GdkColor white = {0, 0xffff, 0xffff, 0xffff};
    gtk_widget_modify_bg(label, GTK_STATE_NORMAL, &white);
    
    // 设置字体大小
    PangoFontDescription *font = pango_font_description_from_string("Sans 30");
    gtk_widget_modify_font(label, font);
    pango_font_description_free(font);
    
    return label;
}

static void on_notebook_switch_page(GtkNotebook *notebook, 
                                    GtkWidget *page, 
                                    guint page_num, 
                                    gpointer user_data) {
    gint n_pages = gtk_notebook_get_n_pages(notebook);
    if (page_num == n_pages - 1) {
        gtk_main_quit();
    }
}

// —— 主函数 —— 
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    g_view_year = tmv.tm_year + 1900;
    g_view_month = tmv.tm_mon + 1;

    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win),
        "L:A_N:application_PC:T_ID:net.tqhyg.reading");
    gtk_window_set_default_size(GTK_WINDOW(win), 1024, 758);

    g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GdkColor white = {0, 0xffff, 0xffff, 0xffff};
    gtk_widget_modify_bg(win, GTK_STATE_NORMAL, &white);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);
    
    GdkColor white2 = {0, 0xffff, 0xffff, 0xffff};
    gtk_widget_modify_bg(vbox, GTK_STATE_NORMAL, &white2);

    GtkWidget *nb = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(nb), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 0);

    gtk_widget_modify_bg(nb, GTK_STATE_NORMAL, &white_color);

    PangoFontDescription *tab_font = pango_font_description_from_string("Sans Bold 16");

    auto add_tab = [&](GtkWidget *page, const char *name) {
        GtkWidget *tab = gtk_label_new(name);
        gtk_widget_modify_font(tab, tab_font);

         // 设置非激活标签：白底黑字
        gtk_widget_modify_bg(tab, GTK_STATE_NORMAL, &white_color);
        gtk_widget_modify_fg(tab, GTK_STATE_NORMAL, &black_color);
        gtk_widget_modify_bg(tab, GTK_STATE_PRELIGHT, &white_color);
        gtk_widget_modify_fg(tab, GTK_STATE_PRELIGHT, &black_color);
        
        // 设置激活标签：黑底白字
        gtk_widget_modify_bg(tab, GTK_STATE_ACTIVE, &black_color);
        gtk_widget_modify_fg(tab, GTK_STATE_ACTIVE, &white_color);
        gtk_widget_modify_bg(tab, GTK_STATE_SELECTED, &black_color);
        gtk_widget_modify_fg(tab, GTK_STATE_SELECTED, &white_color);

        gtk_notebook_append_page(GTK_NOTEBOOK(nb), page, tab);
    };

    add_tab(create_overview_page(), "概览");
    add_tab(create_today_page(), "今日分布");
    add_tab(create_week_page(), "本周分布");
    add_tab(create_month_page(), "月视图");
    add_tab(create_exit_page(), "退出");

    pango_font_description_free(tab_font);

    g_signal_connect(G_OBJECT(nb), "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}
