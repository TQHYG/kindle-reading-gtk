#include "types.hpp"
#include "utils.hpp"
#include "week.hpp"

// —— 本周分布绘图（柱状图） ——
gboolean draw_week_dist(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
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

GtkWidget* create_week_page() {
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 800, 500);
    g_signal_connect(G_OBJECT(da), "expose-event",
                     G_CALLBACK(draw_week_dist), NULL);
    return da;
}