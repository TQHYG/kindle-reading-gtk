#include <dirent.h>
#include <cstring>
#include <sys/stat.h>
#include <vector>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "qrcodegen.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "dataprocess.hpp"
#include "share.hpp"
#include "network.hpp"
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

// —— 辅助函数：生成二维码图片 ——
GtkWidget* create_qr_widget(const std::string& data) {
    using namespace qrcodegen;

    // 1. 生成二维码数据
    // Ecc::LOW 对于屏幕扫描足够了，生成的码点较少，更容易识别
    QrCode qr = QrCode::encodeText(data.c_str(), QrCode::Ecc::LOW);
    
    // 2. 计算尺寸
    int module_size = qr.getSize(); // 二维码矩阵大小 (例如 21x21)
    int scale = 8; 
    int border = 2; 
    
    // 最终图片宽高
    int real_size = (module_size + border * 2) * scale;

    // 3. 创建 GdkPixbuf (RGB 格式, 无 Alpha 通道, 8位色深)
    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, real_size, real_size);
    
    if (!pixbuf) return gtk_label_new("QR Error"); // 防御性检查

    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);

    // 4. 填充像素数据
    for (int y = 0; y < real_size; y++) {
        for (int x = 0; x < real_size; x++) {
            // 将像素坐标映射回二维码模块坐标
            int qr_x = (x / scale) - border;
            int qr_y = (y / scale) - border;

            // 默认白色 (0xFF)
            guchar color = 0xFF; 

            // 如果坐标在二维码范围内，且该模块是黑色的
            if (qr_x >= 0 && qr_x < module_size && qr_y >= 0 && qr_y < module_size) {
                if (qr.getModule(qr_x, qr_y)) {
                    color = 0x00; // 黑色
                }
            }

            // 写入 RGB 数据
            guchar *p = pixels + y * rowstride + x * n_channels;
            p[0] = color; // R
            p[1] = color; // G
            p[2] = color; // B
        }
    }

    // 5. 从 Pixbuf 创建 GtkImage
    GtkWidget *img = gtk_image_new_from_pixbuf(pixbuf);
    
    // 释放 pixbuf 引用 (GtkImage 已经接管了)
    g_object_unref(pixbuf);

    return img;
}

// —— 云同步弹窗逻辑 ——

struct SyncDialogData {
    GtkWidget *dialog;
    GtkWidget *content_area;
    guint poll_timer_id;
    std::string pending_device_code;
    GtkWidget *lbl_status;
};

// 轮询回调
gboolean on_poll_login(gpointer user_data) {
    SyncDialogData *ddata = (SyncDialogData*)user_data;
    if (!ddata->pending_device_code.empty()) {
        std::string token = KykkyNetwork::instance().poll_login_status(ddata->pending_device_code);
        if (!token.empty()) {
            // 登陆成功！
            gtk_label_set_text(GTK_LABEL(ddata->lbl_status), "登陆成功！");
            // 停止轮询
            ddata->poll_timer_id = 0;
            
            // 可以在这里触发一次自动同步
            refresh_all_status_labels();
            KykkyNetwork::instance().upload_data(g_stats.today_seconds, g_stats.month_seconds);
            
            return FALSE; 
        }
    }
    return TRUE; // 继续轮询
}

// 弹窗销毁时清理定时器
void on_dialog_destroy(GtkWidget *widget, gpointer user_data) {
    SyncDialogData *ddata = (SyncDialogData*)user_data;
    if (ddata->poll_timer_id > 0) {
        g_source_remove(ddata->poll_timer_id);
    }
    delete ddata;
}

void on_do_sync(GtkButton *btn, gpointer user_data) {
    GtkWidget *lbl = (GtkWidget*)user_data;
    gtk_label_set_text(GTK_LABEL(lbl), "正在连接服务器...");
    
    // 强制刷新 UI
    while (gtk_events_pending()) gtk_main_iteration();

    KykkyNetwork &net = KykkyNetwork::instance();
    if (!net.check_internet()) {
        gtk_label_set_text(GTK_LABEL(lbl), "正在尝试开启网络...");
        while (gtk_events_pending()) gtk_main_iteration();
        net.ensure_wifi_on();
        
        // 简单重试等待
        int retries = 5;
        bool connected = false;
        while(retries-- > 0) {
            sleep(1);
            if(net.check_internet()) {
                connected = true;
                break;
            }
        }
        
        if (!connected) {
            gtk_label_set_text(GTK_LABEL(lbl), "网络连接失败，请检查 WiFi");
            return;
        }
    }

    gtk_label_set_text(GTK_LABEL(lbl), "正在上传数据...");
    while (gtk_events_pending()) gtk_main_iteration();

    std::string err = net.upload_data(g_stats.today_seconds, g_stats.month_seconds);
    if (err.empty()) {
        gtk_label_set_text(GTK_LABEL(lbl), "同步成功！");
        refresh_all_status_labels(); 
    } else {
        std::string msg = "同步失败: " + err;
        gtk_label_set_text(GTK_LABEL(lbl), msg.c_str());
    }
}

static void on_logout_confirm(GtkWidget *widget, gpointer dialog_ptr) {
    GtkWidget *dialog = GTK_WIDGET(dialog_ptr);
    KykkyNetwork &net = KykkyNetwork::instance();
    
    gtk_widget_set_sensitive(widget, FALSE);
    
    if (net.logout()) {
        refresh_all_status_labels();
        gtk_widget_destroy(dialog);
    } else {
        gtk_widget_set_sensitive(widget, TRUE);
        GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(dialog),
                            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                            "注销失败，请检查网络连接并重试");
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
    }
}

void on_cloud_sync_clicked(GtkButton *btn, gpointer data) {
    SyncDialogData *ddata = new SyncDialogData();
    ddata->poll_timer_id = 0;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "L:D_N:dialog_PC:T_ID:net.tqhyg.reading.dialog", 
        NULL, 
        GTK_DIALOG_MODAL, 
        "关闭", GTK_RESPONSE_CLOSE, 
        NULL
    );
    ddata->dialog = dialog;
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    KykkyNetwork &net = KykkyNetwork::instance();
    bool logged_in = net.get_user_info().is_logged_in;

    GtkWidget *vbox = gtk_vbox_new(FALSE, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    // 状态标签
    ddata->lbl_status = gtk_label_new("");
    
    if (!logged_in) {
        // --- 未登录界面 ---
        GtkWidget *lbl_title = gtk_label_new("    请扫描下方二维码登录    ");
        PangoFontDescription *font_lg = pango_font_description_from_string("Sans Bold 12");
        gtk_widget_modify_font(lbl_title, font_lg);
        gtk_box_pack_start(GTK_BOX(vbox), lbl_title, FALSE, FALSE, 10);

        // 获取二维码
        std::pair<std::string, std::string> qr_info = net.fetch_login_qr();
        if (qr_info.first.empty()) {
             gtk_label_set_text(GTK_LABEL(ddata->lbl_status), "  获取二维码失败，请检查网络  ");
        } else {
            ddata->pending_device_code = qr_info.second;
            
            GtkWidget *qr_img = create_qr_widget(qr_info.first);
            gtk_box_pack_start(GTK_BOX(vbox), qr_img, TRUE, TRUE, 10);
            
            gtk_label_set_text(GTK_LABEL(ddata->lbl_status), "    请使用手机浏览器扫描    ");
            
            // 启动轮询 (3秒一次)
            ddata->poll_timer_id = g_timeout_add(3000, on_poll_login, ddata);
        }
    } else {
        // --- 已登录界面 ---
        std::string user_info_str = "    账号: " + (net.get_user_info().nickname.empty() ? "User" : net.get_user_info().nickname);
        if (!net.get_user_info().device_name.empty()) {
            user_info_str += "\n    设备: " + net.get_user_info().device_name;
        }
        GtkWidget *lbl_user = gtk_label_new(user_info_str.c_str());
        // 设置为左对齐
        gtk_misc_set_alignment(GTK_MISC(lbl_user), 0, 0.5); 
        
        PangoFontDescription *font_lg = pango_font_description_from_string("Sans Bold 12");
        gtk_widget_modify_font(lbl_user, font_lg);
        gtk_box_pack_start(GTK_BOX(vbox), lbl_user, FALSE, FALSE, 10);

        // 上次同步时间
        std::string sync_text = "上次同步: " + net.get_last_sync_text();
        GtkWidget *lbl_last_sync = gtk_label_new(sync_text.c_str());
        gtk_box_pack_start(GTK_BOX(vbox), lbl_last_sync, FALSE, FALSE, 5);

        // 按钮vbox
        GtkWidget *outer_align = gtk_alignment_new(0.5, 0, 1.0, 0);
        gtk_box_pack_start(GTK_BOX(vbox), outer_align, FALSE, FALSE, 0);

        GtkWidget *vbtn_box = gtk_vbox_new(FALSE, 10);
        gtk_container_add(GTK_CONTAINER(outer_align), vbtn_box);
        gtk_alignment_set_padding(GTK_ALIGNMENT(outer_align), 0, 0, 100, 100);
        gtk_box_pack_start(GTK_BOX(vbox), vbtn_box, FALSE, FALSE, 0);

        auto make_halfwidth_button = [&](const char *text) {
            GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0, 0);  

            GtkWidget *btn = gtk_button_new_with_label(text);
            gtk_widget_set_size_request(btn, 300, 60); 

            gtk_container_add(GTK_CONTAINER(align), btn);
            return align;
        };

        // 立即同步
        GtkWidget *sync_align = make_halfwidth_button("立即同步");
        g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(sync_align))), 
                        "clicked", G_CALLBACK(on_do_sync), ddata->lbl_status);
        gtk_box_pack_start(GTK_BOX(vbtn_box), sync_align, FALSE, FALSE, 0);

        // 注销登录
        GtkWidget *logout_align = make_halfwidth_button("注销登录");
        g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(logout_align))), 
                        "clicked", G_CALLBACK(on_logout_confirm), dialog);
        gtk_box_pack_start(GTK_BOX(vbtn_box), logout_align, FALSE, FALSE, 0);

        GtkWidget *sep = gtk_hseparator_new();
        gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 10);

        // 离线/FastSync 二维码
        // 逻辑：如果没网，显示快速同步码。
        if (!net.check_internet()) {
            GtkWidget *lbl_offline = gtk_label_new("当前无网络，可扫码快速同步");
            gtk_box_pack_start(GTK_BOX(vbox), lbl_offline, FALSE, FALSE, 5);
            
            // 构建 JSON 并 Base64
            std::string json_data = "{\"did\":\"" + net.get_device_code() + "\",\"today\":" 
                                + std::to_string(g_stats.today_seconds) + ",\"month\":" 
                                + std::to_string(g_stats.month_seconds) + "}";
            
            std::string b64 = base64_encode(json_data);
            std::string fast_url = "https://" + g_share_domain + "/fastsync.php?data=" + b64;
            
            GtkWidget *fast_qr = create_qr_widget(fast_url); 
            gtk_box_pack_start(GTK_BOX(vbox), fast_qr, FALSE, FALSE, 5);
        }
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), ddata->lbl_status, FALSE, FALSE, 10);
    
    g_signal_connect(G_OBJECT(dialog), "destroy", G_CALLBACK(on_dialog_destroy), ddata);
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
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

    // --- 云同步 ---
    GtkWidget *hbox_cloud = gtk_hbox_new(FALSE, 10);
    GtkWidget *vbox_cloud_text = gtk_vbox_new(FALSE, 2);
    
    GtkWidget *lbl_cloud_title = gtk_label_new("云同步");
    // 左对齐
    gtk_misc_set_alignment(GTK_MISC(lbl_cloud_title), 0, 0.5);
    gtk_widget_modify_font(lbl_cloud_title, row_font);

    KykkyNetwork &net = KykkyNetwork::instance();
    std::string status_str = net.get_user_info().is_logged_in ? 
                             ("已登录 - 上次同步: " + net.get_last_sync_text()) : 
                             "未登录";
    GtkWidget *lbl_cloud_status = gtk_label_new(status_str.c_str());
    gtk_misc_set_alignment(GTK_MISC(lbl_cloud_status), 0, 0.5);
    // 小字
    PangoFontDescription *small_font = pango_font_description_from_string("Sans 10");
    gtk_widget_modify_font(lbl_cloud_status, small_font);
    gtk_widget_modify_fg(lbl_cloud_status, GTK_STATE_NORMAL, &gray);
    g_ui_handles.lbl_settings_cloud_status = lbl_cloud_status;

    gtk_box_pack_start(GTK_BOX(vbox_cloud_text), lbl_cloud_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_cloud_text), lbl_cloud_status, FALSE, FALSE, 0);

    GtkWidget *btn_cloud = gtk_button_new_with_label("管理");
    style_button(btn_cloud, "管理");
    g_signal_connect(G_OBJECT(btn_cloud), "clicked", G_CALLBACK(on_cloud_sync_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(hbox_cloud), vbox_cloud_text, TRUE, TRUE, 0); 
    gtk_box_pack_end(GTK_BOX(hbox_cloud), btn_cloud, FALSE, FALSE, 0);

    // --- 将所有行加入 VBox ---
    gtk_box_pack_start(GTK_BOX(vbox), hbox_cloud, FALSE, FALSE, 10);
    GtkWidget *hsep_cloud = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vbox), hsep_cloud, FALSE, FALSE, 10);

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