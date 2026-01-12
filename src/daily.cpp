#include <gtk/gtk.h>

#include "types.hpp"
#include "utils.hpp"
#include "daily.hpp"
#include "dataprocess.hpp"

// —— 今日分布绘图 ——
gboolean draw_today_dist(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    cairo_t *cr = gdk_cairo_create(widget->window);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    int w = widget->allocation.width;
    int h = widget->allocation.height;

    int left = 50, right = 20, top = 40, bottom = 60;

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);

    cairo_move_to(cr, left, top);
    cairo_line_to(cr, left, h - bottom);
    cairo_line_to(cr, w - right, h - bottom);
    cairo_stroke(cr);

    long maxv = 3600;
    for (int i = 0; i < 12; i++)
        if (g_stats.view_daily_buckets[i] > maxv) maxv = g_stats.view_daily_buckets[i];

    int chart_w = w - left - right;
    int chart_h = h - top - bottom - 100;

    double bar_space = chart_w / 12.0;
    double bar_w = bar_space * 0.8;

    for (int i = 0; i < 12; i++) {
        double x = left + bar_space * i + (bar_space - bar_w) / 2;
        double val = g_stats.view_daily_buckets[i];
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
        if (g_stats.view_daily_buckets[i] > g_stats.view_daily_buckets[best]) best = i;

    char comment[128];
    snprintf(comment, sizeof(comment),
             "你最常阅读的时间段是 %02d:00-%02d:00", best*2, best*2+2);

    cairo_set_font_size(cr, 40);
    cairo_move_to(cr, left + 20, top + 40);
    cairo_show_text(cr, comment);

    cairo_destroy(cr);
    return FALSE;
}

// 辅助函数：更新日视图的文本内容
void update_daily_view_ui(DailyViewWidgets *dv) {
    // 1. 更新日期显示
    struct tm tmv;
    localtime_r(&g_view_daily_ts, &tmv);
    
    char buf_year[16], buf_date[32];
    snprintf(buf_year, sizeof(buf_year), "%d", tmv.tm_year + 1900);
    snprintf(buf_date, sizeof(buf_date), "%02d月%02d日", tmv.tm_mon + 1, tmv.tm_mday);
    
    gtk_label_set_text(GTK_LABEL(dv->label_year), buf_year);
    gtk_label_set_text(GTK_LABEL(dv->label_date), buf_date);

    // 2. 更新总时长显示
    char time_str[64];
    format_hms(g_stats.view_daily_seconds, time_str, sizeof(time_str));
    
    char total_label_str[128];
    snprintf(total_label_str, sizeof(total_label_str), "当日时长: %s", time_str);
    gtk_label_set_text(GTK_LABEL(dv->label_total_time), total_label_str);
}

// 前一天/后一天 按钮回调
void on_daily_change(GtkButton *btn, gpointer data) {
    DailyViewWidgets *dv = (DailyViewWidgets*)data;
    int offset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "offset"));
    
    // 调整日期
    g_view_daily_ts += (offset * 24 * 3600);
    
    // 重新计算数据
    // 注意：force_reload=true 会重读文件，虽然略慢但最准确
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, false);
    
    // 更新UI和重绘
    update_daily_view_ui(dv);
    gtk_widget_queue_draw(dv->drawing_area);
}

GtkWidget* create_today_page() {
    // 初始化查看日期为今天
    time_t now = time(NULL);
    g_view_daily_ts = get_day_start(now);

    DailyViewWidgets *dv = (DailyViewWidgets*)g_malloc0(sizeof(DailyViewWidgets));
    g_daily_widgets = dv;
    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);

    // --- 顶部控制栏 (HBox) ---
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);

    // 1. 左侧：总时长 (占用主要空间)
    dv->label_total_time = gtk_label_new("");
    PangoFontDescription *font_big = pango_font_description_from_string("Sans 14");
    gtk_widget_modify_font(dv->label_total_time, font_big);
    // 靠左对齐
    gtk_misc_set_alignment(GTK_MISC(dv->label_total_time), 0.0, 0.5); 
    gtk_box_pack_start(GTK_BOX(hbox), dv->label_total_time, TRUE, TRUE, 5);

    // 2. 右侧区域：[<] [日期] [>]
    // 按钮 <
    GtkWidget *btn_prev = gtk_button_new_with_label("<");
    gtk_widget_set_size_request(btn_prev, 60, 60);
    g_object_set_data(G_OBJECT(btn_prev), "offset", GINT_TO_POINTER(-1));
    g_signal_connect(G_OBJECT(btn_prev), "clicked", G_CALLBACK(on_daily_change), dv);

    // 日期显示 (垂直排列年和月日)
    GtkWidget *vbox_date = gtk_vbox_new(FALSE, 0);
    dv->label_year = gtk_label_new("");
    dv->label_date = gtk_label_new("");
    
    PangoFontDescription *font_small = pango_font_description_from_string("Sans 10");
    PangoFontDescription *font_med = pango_font_description_from_string("Sans Bold 12");
    gtk_widget_modify_font(dv->label_year, font_small);
    gtk_widget_modify_font(dv->label_date, font_med);
    
    gtk_box_pack_start(GTK_BOX(vbox_date), dv->label_year, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_date), dv->label_date, TRUE, TRUE, 0);

    // 按钮 >
    GtkWidget *btn_next = gtk_button_new_with_label(">");
    gtk_widget_set_size_request(btn_next, 60, 60);
    g_object_set_data(G_OBJECT(btn_next), "offset", GINT_TO_POINTER(1));
    g_signal_connect(G_OBJECT(btn_next), "clicked", G_CALLBACK(on_daily_change), dv);

    // 组装右侧
    gtk_box_pack_start(GTK_BOX(hbox), btn_prev, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), vbox_date, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), btn_next, FALSE, FALSE, 5);

    // --- 底部图表 ---
    dv->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(dv->drawing_area, 800, 500); // 这里的宽度根据实际情况调整
    g_signal_connect(G_OBJECT(dv->drawing_area), "expose-event",
                     G_CALLBACK(draw_today_dist), NULL);

    // 组装整体
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), dv->drawing_area, TRUE, TRUE, 0);

    // 初始显示更新
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, true);
    update_daily_view_ui(dv);

    pango_font_description_free(font_big);
    pango_font_description_free(font_small);
    pango_font_description_free(font_med);

    return vbox;
}
