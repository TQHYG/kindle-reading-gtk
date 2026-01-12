#include <string.h>

#include "types.hpp"
#include "utils.hpp"
#include "daily.hpp"
#include "dataprocess.hpp"
#include "month.hpp"

// 将比例值（0.0-1.0）映射到16阶灰度值（0.0-1.0）
// ratio=0.0 → 灰度=1.0（白色）
// ratio=1.0 → 灰度=0.0（黑色）
double get_gray_level_16(double ratio) {
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

gboolean draw_month_view(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
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
    cairo_move_to(cr, left + 360, h - 40); 
    cairo_show_text(cr, month_title);

    cairo_destroy(cr);
    return FALSE;
}

void update_month_title(MonthViewWidgets *mv) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d年%02d月", g_view_year, g_view_month);
    gtk_label_set_text(GTK_LABEL(mv->label_title), buf);
}

void month_prev(GtkButton *b, gpointer data) {
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

void month_next(GtkButton *b, gpointer data) {
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

gboolean on_month_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS) return FALSE;

    // 1. 获取和绘图函数完全一致的布局参数
    int w = widget->allocation.width;
    int h = widget->allocation.height;

    int left = 20, top = 10, right = 20, bottom = 10;
    int grid_w = w - left - right;
    int grid_h = h - top - bottom;

    // 如果点击在边距外，直接忽略
    if (event->x < left || event->x > w - right ||
        event->y < top  || event->y > h - bottom) {
        return FALSE;
    }

    // 2. 计算单元格大小 (和绘图函数保持一致)
    int rows = 7; // 1行标题 + 6行日期
    int cols = 7;
    double cw = grid_w / (double)cols;
    double ch = grid_h / (double)rows;

    // 3. 计算点击的行列索引
    // x 减去左边距，除以宽
    int col = (int)((event->x - left) / cw);
    // y 减去上边距，除以高
    int row = (int)((event->y - top) / ch);

    // 4. 过滤无效区域
    // row == 0 是标题栏 ("Mon", "Tue"...), 点击无效
    if (row < 1 || row >= rows) return FALSE;
    if (col < 0 || col >= cols) return FALSE;

    // 5. 核心：将行列映射回日期
    // 注意：row 0 是标题，row 1 才是第一行日期，所以实际的日期行索引是 row - 1
    int effective_row = row - 1; 

    // 计算当月1号是星期几 (0=Mon, ... 6=Sun)
    // 你的绘图逻辑是: wday==0?6:wday-1，说明是周一开头
    struct tm tm_first = {0};
    tm_first.tm_year = g_view_year - 1900;
    tm_first.tm_mon = g_view_month - 1;
    tm_first.tm_mday = 1;
    mktime(&tm_first); // 标准库 mktime: Sun=0, Mon=1...
    
    // 转换成 Mon=0, Sun=6 的格式，必须和绘图函数完全一致
    int start_wday = (tm_first.tm_wday + 6) % 7; 

    // 6. 算出点击的是第几个格子 (相对于第一个日期格)
    int cell_index = effective_row * 7 + col;
    
    // 7. 算出具体几号
    int day = cell_index - start_wday + 1;

    // 8. 校验日期有效性
    int days_in_this_month = days_in_month(g_view_year, g_view_month);

    if (day > 0 && day <= days_in_this_month) {
        // --- 下面是跳转逻辑，保持你原来的不变 ---
        
        // A. 构造目标日期
        struct tm target_tm = tm_first;
        target_tm.tm_mday = day;
        time_t target_ts = mktime(&target_tm);
        
        // B. 更新全局日期
        g_view_daily_ts = target_ts;

        // C. 刷新数据
        refresh_daily_view_data(g_stats, g_view_daily_ts);
        
        // D. 更新日视图 UI
        if (g_daily_widgets) {
            update_daily_view_ui(g_daily_widgets);
            gtk_widget_queue_draw(g_daily_widgets->drawing_area);
        }

        // E. 切换 Tab
        if (g_notebook) {
            // 请确认 "今日分布" 确实是第 2 个 tab (索引 1)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), 1);
        }
    }

    return TRUE;
}

GtkWidget* create_month_page() {
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

    // 允许画布接收点击事件
    gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(da), "button-press-event", 
                     G_CALLBACK(on_month_click), NULL);

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