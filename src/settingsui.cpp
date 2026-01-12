#include <dirent.h>
#include <cstring>
#include <sys/stat.h>

#include "types.hpp"
#include "utils.hpp"
#include "dataprocess.hpp"
#include "share.hpp"
#include "settingsui.hpp"

// —— 设置页辅助逻辑 ——

void style_button(GtkWidget *btn, const char *text) {
    // 设置按钮文字
    gtk_button_set_label(GTK_BUTTON(btn), text);
    
    // 设置按钮大小
    gtk_widget_set_size_request(btn, 150, 75);
}

// 获取日志目录占用空间 (KiB)
long get_log_dir_size_kib() {
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
bool is_metrics_enabled() {
    struct stat st;
    return (stat(ETC_ENABLE_FILE.c_str(), &st) == 0);
}

// 开关回调

void on_toggle_enable_button(GtkButton *btn, gpointer data) {
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
void on_reset_data(GtkButton *btn, gpointer user_data) {
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
void on_target_change(GtkButton *btn, gpointer data) {
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

GtkWidget* create_settings_page() {
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

    // --- 第五行：分享本月阅读数据 ---
    GtkWidget *hbox_share = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_share = gtk_label_new("分享本月阅读数据");
    gtk_widget_modify_font(lbl_share, row_font);

    GtkWidget *btn_share = gtk_button_new_with_label("分享");
    style_button(btn_share, "分享");
    g_signal_connect(G_OBJECT(btn_share), "clicked", 
                    G_CALLBACK(create_share_dialog), NULL);

    gtk_box_pack_start(GTK_BOX(hbox_share), lbl_share, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_share), btn_share, FALSE, FALSE, 0);

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
    GtkWidget *hsep3 = gtk_hseparator_new(); // 分割线
    gtk_box_pack_start(GTK_BOX(vbox), hsep3, FALSE, FALSE, 10);

    gtk_box_pack_start(GTK_BOX(vbox), hbox_share, FALSE, FALSE, 10);

    pango_font_description_free(row_font);
    
    return align_box;
}