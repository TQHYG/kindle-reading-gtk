#include <gtk/gtk.h>

#include "types.hpp"
#include "utils.hpp"
#include "network.hpp"
#include "overview.hpp"

// —— 概览页 ——
GtkWidget* create_overview_page() {
    // 最外层白底
    GtkWidget *eventbox = gtk_event_box_new();
    gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

    GtkWidget *align_top = gtk_alignment_new(1, 0, 0, 0); // 右上对齐
    KykkyNetwork &net = KykkyNetwork::instance();
    std::string top_text = net.get_user_info().is_logged_in ? 
                           "已同步: " + net.get_last_sync_text() : 
                           "未登录";
    
    GtkWidget *lbl_top_status = gtk_label_new(top_text.c_str());
    PangoFontDescription *tiny_font = pango_font_description_from_string("Sans 8");
    gtk_widget_modify_font(lbl_top_status, tiny_font);
    g_ui_handles.lbl_overview_sync_time = lbl_top_status;

    gtk_container_add(GTK_CONTAINER(align_top), lbl_top_status);

    // 居中用的对齐控件
    GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(eventbox), align);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 20);
    gtk_container_add(GTK_CONTAINER(align), vbox);

    gtk_box_pack_start(GTK_BOX(vbox), align_top, FALSE, FALSE, 0);

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