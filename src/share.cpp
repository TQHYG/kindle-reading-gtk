#include "types.hpp"
#include "utils.hpp"
#include "share.hpp"
#include "qrcodegen.hpp"

// 生成分享URL
std::string generate_share_url() {
    std::string url = "https://" + g_share_domain + "/share.php?";
    
    // 添加年月和目标
    char buf[64];
    snprintf(buf, sizeof(buf), "year=%d&month=%d&goal=%d", g_view_year, g_view_month, g_daily_target_minutes);
    url += buf;
    
    // 添加当月每日数据
    int days = g_stats.month_day_seconds.size();
    for (int i = 0; i < days; i++) {
        long minutes = (g_stats.month_day_seconds[i] + 59) / 60; // 秒转分钟（向上取整）
        snprintf(buf, sizeof(buf), "&%d=%ld", i + 1, minutes);
        url += buf;
    }
    
    // 添加分时数据（d1-d12）
    for (int i = 0; i < 12; i++) {
        long minutes = (g_stats.view_daily_buckets[i] + 59) / 60;
        snprintf(buf, sizeof(buf), "&d%d=%ld", i + 1, minutes);
        url += buf;
    }
    
    return url;
}

gboolean draw_qrcode(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    const std::string *url = static_cast<const std::string*>(data);
    if (!url || url->empty()) return FALSE;
    
    cairo_t *cr = gdk_cairo_create(widget->window);
    
    // 白色背景
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    
    int width = widget->allocation.width;
    int height = widget->allocation.height;
    
    // 使用 qrcodegen 生成 QR 码
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
        url->c_str(), 
        qrcodegen::QrCode::Ecc::LOW  // 使用低级纠错，可容纳更多数据
    );
    
    int qr_size = qr.getSize();  // QR 码的模块数
    int qr_pixel_size = std::min(width, height) * 0.8;  // 绘制区域大小
    double scale = qr_pixel_size / (double)qr_size;
    int offset_x = (width - qr_pixel_size) / 2;
    int offset_y = (height - qr_pixel_size) / 2;
    
    // 绘制二维码
    cairo_set_source_rgb(cr, 0, 0, 0);
    for (int y = 0; y < qr_size; y++) {
        for (int x = 0; x < qr_size; x++) {
            if (qr.getModule(x, y)) {
                cairo_rectangle(cr, 
                    offset_x + x * scale, 
                    offset_y + y * scale, 
                    scale, scale);
                cairo_fill(cr);
            }
        }
    }
    
    cairo_destroy(cr);
    return FALSE;
}

void create_share_dialog() {
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), 
        "L:D_N:dialog_PC:T_ID:net.tqhyg.reading.dialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 500);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 20);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    
    // 设置对话框背景色
    GdkColor white = {0, 0xffff, 0xffff, 0xffff};
    gtk_widget_modify_bg(dialog, GTK_STATE_NORMAL, &white);
    
    // 获取对话框内容区域
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    // 创建垂直布局
    GtkWidget *vbox = gtk_vbox_new(FALSE, 10);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);
    
    // 添加文字
    GtkWidget *label1 = gtk_label_new("分享");
    PangoFontDescription *font1 = pango_font_description_from_string("Sans Bold 20");
    gtk_widget_modify_font(label1, font1);
    gtk_misc_set_alignment(GTK_MISC(label1), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label1, FALSE, FALSE, 5);
    
    GtkWidget *label2 = gtk_label_new("请使用手机扫码查看");
    PangoFontDescription *font2 = pango_font_description_from_string("Sans 16");
    gtk_widget_modify_font(label2, font2);
    gtk_misc_set_alignment(GTK_MISC(label2), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label2, FALSE, FALSE, 10);
    
    // 生成分享URL
    static std::string share_url;  // 使用static确保指针有效
    share_url = generate_share_url();
    
    // 创建绘图区域显示二维码
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 500, 500);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, FALSE, FALSE, 20);
    
    // 连接绘制事件
    g_signal_connect(G_OBJECT(drawing_area), "expose-event",
                     G_CALLBACK(draw_qrcode), (gpointer)&share_url);
    
    // 添加关闭按钮
    GtkWidget *button_close = gtk_button_new_with_label("关闭");
    g_signal_connect_swapped(G_OBJECT(button_close), "clicked",
                             G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_box_pack_start(GTK_BOX(vbox), button_close, FALSE, FALSE, 10);
    
    gtk_widget_show_all(dialog);
}