#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <ctime>
#include <algorithm>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "qrcodegen.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "dataprocess.hpp"
#include "share.hpp"
#include "network.hpp"
#include "settingsui.hpp"

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

static void set_font(GtkWidget *widget, const char *font_desc)
{
    PangoFontDescription *font = pango_font_description_from_string(font_desc);
    gtk_widget_modify_font(widget, font);
    pango_font_description_free(font);
}

static void clear_box(GtkWidget *box)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *item = children; item != NULL; item = item->next) {
        gtk_widget_destroy(GTK_WIDGET(item->data));
    }
    g_list_free(children);
}

static std::string format_seconds_text(long seconds)
{
    if (seconds <= 0) return "0 分钟";
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;
    char buf[64];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%ld 小时 %ld 分钟", h, m);
    } else if (m > 0) {
        snprintf(buf, sizeof(buf), "%ld 分钟", m);
    } else {
        snprintf(buf, sizeof(buf), "不足 1 分钟");
    }
    return buf;
}

static GtkWidget* make_value_label(const char *text, const char *font_desc)
{
    GtkWidget *label = gtk_label_new(text);
    set_font(label, font_desc);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
    return label;
}

static GtkWidget* make_info_row(const char *title, GtkWidget **value_label)
{
    GtkWidget *hbox = gtk_hbox_new(FALSE, 12);

    GtkWidget *label = gtk_label_new(title);
    set_font(label, "Sans Bold 13");
    gtk_widget_set_size_request(label, 130, -1);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);

    GtkWidget *value = make_value_label("-", "Sans 13");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), value, TRUE, TRUE, 0);
    *value_label = value;
    return hbox;
}

static void set_button_text(GtkWidget *button, const char *text)
{
    if (button == NULL) return;
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(button));
    if (child != NULL && GTK_IS_LABEL(child)) {
        gtk_label_set_text(GTK_LABEL(child), text);
        gtk_widget_show(child);
    } else if (child == NULL) {
        GtkWidget *label = gtk_label_new(text);
        gtk_container_add(GTK_CONTAINER(button), label);
        gtk_widget_show(label);
    } else {
        gtk_button_set_label(GTK_BUTTON(button), text);
    }
}

static std::string get_button_text(GtkWidget *button)
{
    if (button == NULL) return "";
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(button));
    if (child != NULL && GTK_IS_LABEL(child)) {
        const gchar *text = gtk_label_get_text(GTK_LABEL(child));
        return text != NULL ? text : "";
    }
    const gchar *label = gtk_button_get_label(GTK_BUTTON(button));
    return label != NULL ? label : "";
}

static void set_info_text(GtkWidget *label, const std::string& text, bool error)
{
    if (label == NULL) return;
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
    if (error) {
        GdkColor red = {0, 0xcc00, 0x0000, 0x0000};
        gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &red);
    } else {
        gtk_widget_modify_fg(label, GTK_STATE_NORMAL, NULL);
    }
}

// ---------------------------------------------------------------------------
// 二维码
// ---------------------------------------------------------------------------

GtkWidget* create_qr_widget(const std::string& data, int scale = 6)
{
    using namespace qrcodegen;

    if (scale < 2) scale = 2;
    QrCode qr = QrCode::encodeText(data.c_str(), QrCode::Ecc::LOW);

    int module_size = qr.getSize();
    int border = 2;
    int real_size = (module_size + border * 2) * scale;

    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, real_size, real_size);
    if (!pixbuf) return gtk_label_new("二维码生成失败");

    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);

    for (int y = 0; y < real_size; y++) {
        for (int x = 0; x < real_size; x++) {
            int qr_x = (x / scale) - border;
            int qr_y = (y / scale) - border;
            guchar color = 0xFF;
            if (qr_x >= 0 && qr_x < module_size && qr_y >= 0 && qr_y < module_size) {
                if (qr.getModule(qr_x, qr_y)) color = 0x00;
            }
            guchar *p = pixels + y * rowstride + x * n_channels;
            p[0] = color;
            p[1] = color;
            p[2] = color;
        }
    }

    GtkWidget *img = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    return img;
}

// ---------------------------------------------------------------------------
// 用户中心
// ---------------------------------------------------------------------------

struct UserCenterWidgets {
    GtkWidget *lbl_login_status;
    GtkWidget *lbl_username;
    GtkWidget *lbl_device;
    GtkWidget *lbl_sync;
    GtkWidget *lbl_info;
    GtkWidget *btn_auth;
    GtkWidget *btn_sync;
    GtkWidget *btn_fast;
    GtkWidget *btn_share;
    GtkWidget *qr_container;
    GtkWidget *qr_child;
    guint poll_timer;
    std::string pending_code;
    bool poll_in_progress;
    bool auth_busy;
    bool sync_busy;

    UserCenterWidgets()
        : lbl_login_status(NULL), lbl_username(NULL), lbl_device(NULL), lbl_sync(NULL),
          lbl_info(NULL), btn_auth(NULL), btn_sync(NULL), btn_fast(NULL), btn_share(NULL),
          qr_container(NULL), qr_child(NULL), poll_timer(0), poll_in_progress(false),
          auth_busy(false), sync_busy(false) {}
};

static UserCenterWidgets *g_user_center = NULL;
static GtkWidget *g_rank_page = NULL;

static void user_center_set_info(const std::string& text, bool error = false)
{
    if (g_user_center != NULL) {
        set_info_text(g_user_center->lbl_info, text, error);
    }
}

static void user_center_clear_qr()
{
    if (g_user_center == NULL || g_user_center->qr_container == NULL) return;
    if (g_user_center->qr_child != NULL) {
        gtk_widget_destroy(g_user_center->qr_child);
        g_user_center->qr_child = NULL;
    }
    gtk_widget_set_no_show_all(g_user_center->qr_container, TRUE);
    gtk_widget_hide(g_user_center->qr_container);
}

static void user_center_show_qr(const std::string& data, int scale)
{
    if (g_user_center == NULL || g_user_center->qr_container == NULL) return;
    user_center_clear_qr();

    GtkWidget *image = create_qr_widget(data, scale);
    GtkWidget *align = gtk_alignment_new(0.5f, 0.5f, 0.0f, 0.0f);
    gtk_container_add(GTK_CONTAINER(align), image);
    gtk_box_pack_start(GTK_BOX(g_user_center->qr_container), align, FALSE, FALSE, 0);
    g_user_center->qr_child = align;
    gtk_widget_set_no_show_all(g_user_center->qr_container, FALSE);
    gtk_widget_show_all(g_user_center->qr_container);
}

static void stop_login_poll()
{
    if (g_user_center == NULL) return;
    if (g_user_center->poll_timer != 0) {
        g_source_remove(g_user_center->poll_timer);
        g_user_center->poll_timer = 0;
    }
    g_user_center->poll_in_progress = false;
    g_user_center->pending_code.clear();
}

static void user_center_refresh()
{
    if (g_user_center == NULL) return;
    KykkyNetwork &net = KykkyNetwork::instance();
    UserInfo info = net.get_user_info();
    bool logged = info.is_logged_in;

    gtk_label_set_text(GTK_LABEL(g_user_center->lbl_login_status), logged ? "已登录" : "未登录");

    std::string name = logged ? (info.nickname.empty() ? "未获取" : info.nickname) : "-";
    std::string device = logged ? (info.device_name.empty() ? "未命名设备" : info.device_name) : "-";
    std::string sync = logged ? ("上次同步: " + net.get_last_sync_text()) : "未同步";

    gtk_label_set_text(GTK_LABEL(g_user_center->lbl_username), name.c_str());
    gtk_label_set_text(GTK_LABEL(g_user_center->lbl_device), device.c_str());
    gtk_label_set_text(GTK_LABEL(g_user_center->lbl_sync), sync.c_str());

    set_button_text(g_user_center->btn_auth, logged ? "退出登录" : "扫码登录");
    gtk_widget_set_sensitive(g_user_center->btn_auth, !g_user_center->auth_busy);
    gtk_widget_set_sensitive(g_user_center->btn_sync, logged && !g_user_center->sync_busy);
    gtk_widget_set_sensitive(g_user_center->btn_fast, logged);
    gtk_widget_set_sensitive(g_user_center->btn_share, TRUE);

    // 登录状态变化后，排行榜的“我的排名”需要重新获取。
    if (g_rank_page != NULL) {
        g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(0));
    }
}

struct AuthFetchData {
    bool ok;
    std::string login_url;
    std::string device_code;
    std::string error;
};

static gboolean auth_fetch_done(gpointer data)
{
    AuthFetchData *ad = static_cast<AuthFetchData *>(data);
    if (g_user_center != NULL) {
        g_user_center->auth_busy = false;
        gtk_widget_set_sensitive(g_user_center->btn_auth, TRUE);

        if (ad->ok && !ad->login_url.empty()) {
            g_user_center->pending_code = ad->device_code;
            user_center_show_qr(ad->login_url, 6);
            user_center_set_info("请使用手机浏览器扫描二维码登录");
            stop_login_poll();
            g_user_center->pending_code = ad->device_code;
            g_user_center->poll_timer = g_timeout_add(3000, [](gpointer) -> gboolean {
                if (g_user_center == NULL || g_user_center->pending_code.empty()) {
                    if (g_user_center != NULL) g_user_center->poll_timer = 0;
                    return FALSE;
                }
                if (g_user_center->poll_in_progress) return TRUE;

                struct PollStart {
                    std::string code;
                };
                PollStart *ps = new PollStart();
                ps->code = g_user_center->pending_code;
                g_user_center->poll_in_progress = true;
                pthread_t tid;
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                pthread_create(&tid, &attr, [](gpointer p) -> gpointer {
                    PollStart *start = static_cast<PollStart *>(p);
                    std::string code = start->code;
                    delete start;
                    std::string token = KykkyNetwork::instance().poll_login_status(code);

                    struct PollResult {
                        std::string token;
                        std::string error;
                    };
                    PollResult *pr = new PollResult();
                    pr->token = token;
                    pr->error = KykkyNetwork::instance().get_last_error();
                    g_idle_add([](gpointer q) -> gboolean {
                        PollResult *result = static_cast<PollResult *>(q);
                        if (g_user_center != NULL) {
                            g_user_center->poll_in_progress = false;
                            if (!result->token.empty()) {
                                stop_login_poll();
                                user_center_clear_qr();
                                user_center_refresh();
                                user_center_set_info("登录成功，正在同步数据...");
                                refresh_all_status_labels();
                                // 自动同步
                                struct AutoSync {
                                    long today, week, month, total;
                                };
                                AutoSync *sync = new AutoSync();
                                sync->today = g_stats.today_seconds;
                                sync->week = g_stats.week_seconds;
                                sync->month = g_stats.month_seconds;
                                sync->total = g_stats.total_seconds;
                                pthread_t sync_tid;
                                pthread_attr_t sync_attr;
                                pthread_attr_init(&sync_attr);
                                pthread_attr_setdetachstate(&sync_attr, PTHREAD_CREATE_DETACHED);
                                pthread_create(&sync_tid, &sync_attr, [](gpointer s) -> gpointer {
                                    AutoSync *data = static_cast<AutoSync *>(s);
                                    KykkyNetwork &net = KykkyNetwork::instance();
                                    std::string error;
                                    if (!net.check_internet()) {
                                        error = "no_network";
                                    } else {
                                        error = net.upload_data(data->today, data->week, data->month, data->total);
                                    }
                                    long today = data->today, week = data->week, month = data->month, total = data->total;
                                    delete data;

                                    struct SyncResult {
                                        std::string error;
                                    };
                                    SyncResult *sr = new SyncResult();
                                    sr->error = error;
                                    g_idle_add([](gpointer r) -> gboolean {
                                        SyncResult *res = static_cast<SyncResult *>(r);
                                        if (g_user_center != NULL) {
                                            if (res->error.empty()) {
                                                user_center_set_info("登录成功，数据已同步");
                                            } else {
                                                user_center_set_info("登录成功，但同步失败: " +
                                                    (res->error == "no_network" ? std::string("网络连接失败") : res->error), true);
                                            }
                                            user_center_refresh();
                                            refresh_all_status_labels();
                                        }
                                        delete res;
                                        return FALSE;
                                    }, sr);
                                    return NULL;
                                }, sync);
                                pthread_attr_destroy(&sync_attr);
                            } else if (!result->error.empty()) {
                                user_center_set_info("登录状态查询失败: " + result->error, true);
                                if (result->error.find("过期") != std::string::npos) {
                                    stop_login_poll();
                                    user_center_clear_qr();
                                    gtk_widget_set_sensitive(g_user_center->btn_auth, TRUE);
                                }
                            } else {
                                user_center_set_info("请使用手机浏览器扫描二维码登录");
                            }
                        }
                        delete result;
                        return FALSE;
                    }, pr);
                    return NULL;
                }, ps);
                pthread_attr_destroy(&attr);
                return TRUE;
            }, NULL);
            gtk_widget_set_sensitive(g_user_center->btn_auth, TRUE);
        } else {
            std::string msg = ad->error.empty() ? "获取登录二维码失败" : ad->error;
            user_center_set_info(msg, true);
        }
    }
    delete ad;
    return FALSE;
}

static void start_login_qr()
{
    if (g_user_center == NULL || g_user_center->auth_busy) return;
    stop_login_poll();
    user_center_clear_qr();
    g_user_center->auth_busy = true;
    gtk_widget_set_sensitive(g_user_center->btn_auth, FALSE);
    user_center_set_info("正在获取登录二维码...");

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, [](gpointer) -> gpointer {
        KykkyNetwork &net = KykkyNetwork::instance();
        auto qr = net.fetch_login_qr();
        AuthFetchData *ad = new AuthFetchData();
        ad->ok = !qr.first.empty() && !qr.second.empty();
        ad->login_url = qr.first;
        ad->device_code = qr.second;
        ad->error = ad->ok ? "" : net.get_last_error();
        g_idle_add(auth_fetch_done, ad);
        return NULL;
    }, NULL);
    pthread_attr_destroy(&attr);
}

struct LogoutData {
    bool success;
    std::string error;
};

static gboolean logout_done(gpointer data)
{
    LogoutData *ld = static_cast<LogoutData *>(data);
    if (g_user_center != NULL) {
        g_user_center->auth_busy = false;
        if (ld->success) {
            stop_login_poll();
            user_center_clear_qr();
            user_center_set_info("已退出登录");
            user_center_refresh();
            refresh_all_status_labels();
        } else {
            user_center_set_info(ld->error.empty() ? "退出登录失败" : ("退出登录失败: " + ld->error), true);
            gtk_widget_set_sensitive(g_user_center->btn_auth, TRUE);
        }
    }
    delete ld;
    return FALSE;
}

static void start_logout()
{
    if (g_user_center == NULL || g_user_center->auth_busy) return;
    stop_login_poll();
    user_center_clear_qr();
    g_user_center->auth_busy = true;
    gtk_widget_set_sensitive(g_user_center->btn_auth, FALSE);
    user_center_set_info("正在退出登录...");

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, [](gpointer) -> gpointer {
        KykkyNetwork &net = KykkyNetwork::instance();
        LogoutData *ld = new LogoutData();
        ld->success = net.logout();
        ld->error = ld->success ? "" : net.get_last_error();
        g_idle_add(logout_done, ld);
        return NULL;
    }, NULL);
    pthread_attr_destroy(&attr);
}

struct ManualSyncData {
    long today;
    long week;
    long month;
    long total;
    std::string error;
};

static std::string friendly_sync_error(const std::string& error)
{
    if (error.empty()) return "";
    if (error == "no_network") return "网络连接失败，请检查 WiFi";
    if (error == "device_expired") return "设备授权已过期，请重新登录";
    if (error == "not_logged_in") return "尚未登录，请重新扫码登录";
    if (error == "device_not_bound") return "设备尚未完成绑定，请重新扫码";
    if (error.rfind("Network Error", 0) == 0 || error.rfind("网络请求失败", 0) == 0) {
        return "网络异常: " + error;
    }
    return error;
}

static gboolean manual_sync_done(gpointer data)
{
    ManualSyncData *sd = static_cast<ManualSyncData *>(data);
    if (g_user_center != NULL) {
        g_user_center->sync_busy = false;
        if (sd->error.empty()) {
            user_center_set_info("同步成功");
        } else {
            user_center_set_info("同步失败: " + friendly_sync_error(sd->error), true);
        }
        user_center_refresh();
        refresh_all_status_labels();
    }
    delete sd;
    return FALSE;
}

static void start_manual_sync()
{
    if (g_user_center == NULL || g_user_center->sync_busy) return;
    KykkyNetwork &net = KykkyNetwork::instance();
    if (!net.get_user_info().is_logged_in) {
        user_center_set_info("请先登录", true);
        return;
    }

    g_user_center->sync_busy = true;
    gtk_widget_set_sensitive(g_user_center->btn_sync, FALSE);
    user_center_set_info("正在同步数据...");

    ManualSyncData *sd = new ManualSyncData();
    sd->today = g_stats.today_seconds;
    sd->week = g_stats.week_seconds;
    sd->month = g_stats.month_seconds;
    sd->total = g_stats.total_seconds;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, [](gpointer p) -> gpointer {
        ManualSyncData *data = static_cast<ManualSyncData *>(p);
        KykkyNetwork &net = KykkyNetwork::instance();
        if (!net.check_internet()) {
            net.ensure_wifi_on();
            bool connected = false;
            for (int i = 0; i < 5; i++) {
                sleep(1);
                if (net.check_internet()) {
                    connected = true;
                    break;
                }
            }
            if (!connected) {
                data->error = "no_network";
                g_idle_add(manual_sync_done, data);
                return NULL;
            }
        }
        data->error = net.upload_data(data->today, data->week, data->month, data->total);
        g_idle_add(manual_sync_done, data);
        return NULL;
    }, sd);
    pthread_attr_destroy(&attr);
}

static void on_user_auth_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    if (KykkyNetwork::instance().get_user_info().is_logged_in) {
        start_logout();
    } else {
        start_login_qr();
    }
}

static void on_user_sync_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    start_manual_sync();
}

static void on_user_fast_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    KykkyNetwork &net = KykkyNetwork::instance();
    if (!net.get_user_info().is_logged_in) {
        user_center_set_info("请先登录", true);
        return;
    }

    std::string device_code = net.get_device_code();
    if (device_code.empty()) {
        user_center_set_info("设备标识为空，请重新登录", true);
        return;
    }

    std::string json = "{\"did\":\"" + device_code + "\",\"today\":"
        + std::to_string(g_stats.today_seconds) + ",\"week\":"
        + std::to_string(g_stats.week_seconds) + ",\"month\":"
        + std::to_string(g_stats.month_seconds) + ",\"total\":"
        + std::to_string(g_stats.total_seconds) + "}";
    std::string url = share_base_url() + "/fastsync.html?data=" + base64_encode(json);
    user_center_show_qr(url, 6);
    user_center_set_info("快捷同步二维码已生成，可用手机扫码同步");
}

static void on_user_share_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    std::string url = generate_share_url();
    user_center_show_qr(url, 5);
    user_center_set_info("本月分享二维码已生成，可用手机扫码查看");
}

static GtkWidget* create_user_center_page()
{
    g_user_center = new UserCenterWidgets();

    GtkWidget *eventbox = gtk_event_box_new();
    gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(eventbox), scroll);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), vbox);

    GtkWidget *title = gtk_label_new("用户中心");
    set_font(title, "Sans Bold 18");
    gtk_misc_set_alignment(GTK_MISC(title), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *row_status = make_info_row("登录状态", &g_user_center->lbl_login_status);
    GtkWidget *row_user = make_info_row("用户名", &g_user_center->lbl_username);
    GtkWidget *row_device = make_info_row("设备名称", &g_user_center->lbl_device);
    GtkWidget *row_sync = make_info_row("同步状态", &g_user_center->lbl_sync);

    gtk_box_pack_start(GTK_BOX(vbox), row_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), row_user, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), row_device, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), row_sync, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 4);

    GtkWidget *buttons = gtk_hbox_new(TRUE, 10);
    g_user_center->btn_auth = gtk_button_new_with_label("扫码登录");
    g_user_center->btn_sync = gtk_button_new_with_label("立即同步");
    g_user_center->btn_fast = gtk_button_new_with_label("快速同步");
    g_user_center->btn_share = gtk_button_new_with_label("分享本月数据");

    GtkWidget *buttons_array[] = {
        g_user_center->btn_auth, g_user_center->btn_sync,
        g_user_center->btn_fast, g_user_center->btn_share
    };
    for (int i = 0; i < 4; i++) {
        gtk_widget_set_size_request(buttons_array[i], 150, 58);
        gtk_box_pack_start(GTK_BOX(buttons), buttons_array[i], TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vbox), buttons, FALSE, FALSE, 4);

    g_user_center->qr_container = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), g_user_center->qr_container, FALSE, FALSE, 4);
    gtk_widget_set_no_show_all(g_user_center->qr_container, TRUE);
    gtk_widget_hide(g_user_center->qr_container);

    GtkWidget *info_title = gtk_label_new("状态 / 错误信息");
    set_font(info_title, "Sans Bold 11");
    gtk_misc_set_alignment(GTK_MISC(info_title), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), info_title, FALSE, FALSE, 2);

    g_user_center->lbl_info = gtk_label_new("就绪");
    gtk_label_set_line_wrap(GTK_LABEL(g_user_center->lbl_info), TRUE);
    gtk_label_set_justify(GTK_LABEL(g_user_center->lbl_info), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(g_user_center->lbl_info), 0.0f, 0.0f);
    gtk_widget_set_size_request(g_user_center->lbl_info, -1, 64);
    set_font(g_user_center->lbl_info, "Sans 11");
    gtk_box_pack_start(GTK_BOX(vbox), g_user_center->lbl_info, FALSE, FALSE, 0);

    g_signal_connect(G_OBJECT(g_user_center->btn_auth), "clicked",
                     G_CALLBACK(on_user_auth_clicked), NULL);
    g_signal_connect(G_OBJECT(g_user_center->btn_sync), "clicked",
                     G_CALLBACK(on_user_sync_clicked), NULL);
    g_signal_connect(G_OBJECT(g_user_center->btn_fast), "clicked",
                     G_CALLBACK(on_user_fast_clicked), NULL);
    g_signal_connect(G_OBJECT(g_user_center->btn_share), "clicked",
                     G_CALLBACK(on_user_share_clicked), NULL);

    user_center_refresh();
    return eventbox;
}

// ---------------------------------------------------------------------------
// 排行榜
// ---------------------------------------------------------------------------

struct RankWidgets {
    GtkWidget *message;
    GtkWidget *scroll;
    GtkWidget *body;
    GtkWidget *btn_refresh;
    GtkWidget *type_buttons[4];
    std::string type;
    bool loading;

    RankWidgets() : message(NULL), scroll(NULL), body(NULL), btn_refresh(NULL),
                    type("today"), loading(false) {
        for (int i = 0; i < 4; i++) type_buttons[i] = NULL;
    }
};

static RankWidgets *g_rank = NULL;

static void rank_set_buttons_sensitive(bool sensitive)
{
    if (g_rank == NULL) return;
    gtk_widget_set_sensitive(g_rank->btn_refresh, sensitive);
    for (int i = 0; i < 4; i++) {
        if (g_rank->type_buttons[i] != NULL) {
            gtk_widget_set_sensitive(g_rank->type_buttons[i], sensitive);
        }
    }
}

static void rank_show_message(const std::string& text)
{
    if (g_rank == NULL) return;
    gtk_label_set_text(GTK_LABEL(g_rank->message), text.c_str());
    gtk_widget_set_no_show_all(g_rank->scroll, TRUE);
    gtk_widget_hide(g_rank->scroll);
    gtk_widget_show(g_rank->message);
}

static void rank_show_list()
{
    if (g_rank == NULL) return;
    gtk_widget_hide(g_rank->message);
    gtk_widget_set_no_show_all(g_rank->scroll, FALSE);
    gtk_widget_show_all(g_rank->scroll);
}

static long local_seconds_for_type(const std::string& type)
{
    if (type == "week") return g_stats.week_seconds;
    if (type == "month") return g_stats.month_seconds;
    if (type == "total") return g_stats.total_seconds;
    return g_stats.today_seconds;
}

static std::string local_rank_text(const RankData& data)
{
    bool logged_in = KykkyNetwork::instance().get_user_info().is_logged_in;
    long local_seconds = local_seconds_for_type(data.type);
    if (local_seconds <= 0) {
        return logged_in
            ? "我的排名：暂无该维度阅读数据"
            : "未登录，本地暂无该维度阅读数据，无法估算排名";
    }

    int greater = 0;
    for (size_t i = 0; i < data.entries.size(); i++) {
        if (data.entries[i].seconds > local_seconds) greater++;
    }
    int estimate = greater + 1;
    char buf[160];
    if (logged_in) {
        snprintf(buf, sizeof(buf), "未获取到服务器排名，本地估算：第 %d 名", estimate);
    } else if (data.has_more) {
        snprintf(buf, sizeof(buf), "未登录，本地估算：约第 %d 名（基于前 %zu 名）", estimate, data.entries.size());
    } else {
        snprintf(buf, sizeof(buf), "未登录，本地估算：第 %d 名", estimate);
    }
    return buf;
}

static void render_rank_list(const RankData& data)
{
    if (g_rank == NULL) return;
    clear_box(g_rank->body);

    std::string mine;
    if (data.my_rank > 0) {
        mine = "我的排名：第 " + std::to_string(data.my_rank) + " 名 · "
            + format_seconds_text(data.my_seconds);
    } else {
        mine = local_rank_text(data);
    }

    GtkWidget *my_label = gtk_label_new(mine.c_str());
    set_font(my_label, "Sans Bold 12");
    gtk_misc_set_alignment(GTK_MISC(my_label), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(g_rank->body), my_label, FALSE, FALSE, 8);

    gtk_box_pack_start(GTK_BOX(g_rank->body), gtk_hseparator_new(), FALSE, FALSE, 4);

    if (data.entries.empty()) {
        GtkWidget *empty = gtk_label_new("暂无排行榜数据");
        set_font(empty, "Sans 13");
        gtk_misc_set_alignment(GTK_MISC(empty), 0.5f, 0.5f);
        gtk_box_pack_start(GTK_BOX(g_rank->body), empty, FALSE, FALSE, 30);
        rank_show_list();
        return;
    }

    for (size_t i = 0; i < data.entries.size(); i++) {
        const RankEntry &entry = data.entries[i];
        GtkWidget *row = gtk_hbox_new(FALSE, 12);

        char rank_buf[16];
        snprintf(rank_buf, sizeof(rank_buf), "%d", entry.rank);
        GtkWidget *rank_label = gtk_label_new(rank_buf);
        set_font(rank_label, entry.rank <= 3 ? "Sans Bold 15" : "Sans 14");
        gtk_widget_set_size_request(rank_label, 52, -1);
        gtk_misc_set_alignment(GTK_MISC(rank_label), 0.5f, 0.5f);

        GtkWidget *name_label = gtk_label_new(entry.nickname.c_str());
        set_font(name_label, entry.rank <= 3 ? "Sans Bold 13" : "Sans 13");
        gtk_misc_set_alignment(GTK_MISC(name_label), 0.0f, 0.5f);

        GtkWidget *time_label = gtk_label_new(format_seconds_text(entry.seconds).c_str());
        set_font(time_label, "Sans 12");
        gtk_misc_set_alignment(GTK_MISC(time_label), 1.0f, 0.5f);

        gtk_box_pack_start(GTK_BOX(row), rank_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), name_label, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), time_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(g_rank->body), row, FALSE, FALSE, 2);
    }

    if (data.has_more) {
        GtkWidget *more = gtk_label_new("仅显示前 50 名");
        set_font(more, "Sans 10");
        gtk_misc_set_alignment(GTK_MISC(more), 0.5f, 0.5f);
        gtk_box_pack_start(GTK_BOX(g_rank->body), more, FALSE, FALSE, 10);
    }

    rank_show_list();
}

struct RankFetchData {
    std::string type;
    bool online;
    RankData data;
};

static gboolean rank_fetch_done(gpointer data)
{
    RankFetchData *fd = static_cast<RankFetchData *>(data);
    if (g_rank != NULL && fd->type == g_rank->type) {
        g_rank->loading = false;
        rank_set_buttons_sensitive(true);

        if (!fd->online) {
            rank_show_message("未联网，请检查 WiFi 后点击刷新");
            g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(0));
        } else if (!fd->data.ok) {
            rank_show_message("排行榜加载失败：" + (fd->data.error.empty() ? std::string("未知错误") : fd->data.error));
            g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(0));
        } else {
            render_rank_list(fd->data);
            g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(1));
        }
    }
    delete fd;
    return FALSE;
}

static void rank_refresh_async()
{
    if (g_rank == NULL || g_rank->loading) return;

    g_rank->loading = true;
    rank_set_buttons_sensitive(false);
    rank_show_message("正在加载排行榜...");

    RankFetchData *fd = new RankFetchData();
    fd->type = g_rank->type;
    fd->online = false;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, [](gpointer p) -> gpointer {
        RankFetchData *data = static_cast<RankFetchData *>(p);
        KykkyNetwork &net = KykkyNetwork::instance();
        data->online = net.check_internet();
        if (data->online) {
            data->data = net.fetch_rank(data->type, 50);
        }
        g_idle_add(rank_fetch_done, data);
        return NULL;
    }, fd);
    pthread_attr_destroy(&attr);
}

static void on_rank_type_clicked(GtkButton *btn, gpointer data)
{
    (void)data;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
    const char *type = static_cast<const char *>(g_object_get_data(G_OBJECT(btn), "rank_type"));
    if (type == NULL || g_rank == NULL) return;
    if (g_rank->type == type && g_object_get_data(G_OBJECT(g_rank_page), "rank_loaded") != NULL) {
        return;
    }
    g_rank->type = type;
    g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(0));
    rank_refresh_async();
}

static void on_rank_refresh_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(0));
    rank_refresh_async();
}

static GtkWidget* create_rank_page()
{
    g_rank = new RankWidgets();

    GtkWidget *eventbox = gtk_event_box_new();
    gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);
    g_rank_page = eventbox;
    g_object_set_data(G_OBJECT(g_rank_page), "rank_loaded", GINT_TO_POINTER(0));

    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(eventbox), vbox);

    GtkWidget *header = gtk_hbox_new(FALSE, 10);
    gtk_container_set_border_width(GTK_CONTAINER(header), 12);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *left = gtk_hbox_new(FALSE, 6);
    GtkWidget *first = gtk_radio_button_new_with_label(NULL, "日榜");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(first), FALSE);
    g_object_set_data(G_OBJECT(first), "rank_type", (gpointer)"today");
    gtk_widget_set_size_request(first, 92, 48);

    GtkWidget *week = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(first), "周榜");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(week), FALSE);
    g_object_set_data(G_OBJECT(week), "rank_type", (gpointer)"week");
    gtk_widget_set_size_request(week, 92, 48);

    GtkWidget *month = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(first), "月榜");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(month), FALSE);
    g_object_set_data(G_OBJECT(month), "rank_type", (gpointer)"month");
    gtk_widget_set_size_request(month, 92, 48);

    GtkWidget *total = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(first), "总榜");
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(total), FALSE);
    g_object_set_data(G_OBJECT(total), "rank_type", (gpointer)"total");
    gtk_widget_set_size_request(total, 92, 48);

    g_rank->type_buttons[0] = first;
    g_rank->type_buttons[1] = week;
    g_rank->type_buttons[2] = month;
    g_rank->type_buttons[3] = total;

    GtkWidget *types[] = {first, week, month, total};
    for (int i = 0; i < 4; i++) {
        gtk_box_pack_start(GTK_BOX(left), types[i], FALSE, FALSE, 0);
        g_signal_connect(G_OBJECT(types[i]), "clicked", G_CALLBACK(on_rank_type_clicked), NULL);
    }
    gtk_box_pack_start(GTK_BOX(header), left, FALSE, FALSE, 0);

    g_rank->btn_refresh = gtk_button_new_with_label("刷新");
    gtk_widget_set_size_request(g_rank->btn_refresh, 96, 48);
    g_signal_connect(G_OBJECT(g_rank->btn_refresh), "clicked", G_CALLBACK(on_rank_refresh_clicked), NULL);
    gtk_box_pack_end(GTK_BOX(header), g_rank->btn_refresh, FALSE, FALSE, 0);

    g_rank->message = gtk_label_new("点击上方按钮加载排行榜");
    set_font(g_rank->message, "Sans 14");
    gtk_misc_set_alignment(GTK_MISC(g_rank->message), 0.5f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), g_rank->message, TRUE, TRUE, 0);

    g_rank->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g_rank->scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_modify_bg(g_rank->scroll, GTK_STATE_NORMAL, &white);

    g_rank->body = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(g_rank->body), 14);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(g_rank->scroll), g_rank->body);
    gtk_box_pack_start(GTK_BOX(vbox), g_rank->scroll, TRUE, TRUE, 0);
    gtk_widget_set_no_show_all(g_rank->scroll, TRUE);
    gtk_widget_hide(g_rank->scroll);

    gtk_widget_show_all(eventbox);
    gtk_widget_hide(g_rank->scroll);

    return eventbox;
}

// ---------------------------------------------------------------------------
// 设置
// ---------------------------------------------------------------------------

static GtkWidget *g_settings_info = NULL;
static GtkWidget *g_settings_size = NULL;
static GtkWidget *g_settings_clear_btn = NULL;
static bool g_settings_clear_pending = false;

static void settings_set_info(const std::string& text, bool error = false)
{
    set_info_text(g_settings_info, text, error);
}

void style_button(GtkWidget *btn, const char *text)
{
    set_button_text(btn, text);
    gtk_widget_set_size_request(btn, 150, 75);
}

long get_log_dir_size_kib()
{
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

bool is_metrics_enabled()
{
    struct stat st;
    return (stat(ETC_ENABLE_FILE.c_str(), &st) == 0);
}

void on_toggle_enable_button(GtkButton *btn, gpointer data)
{
    (void)data;
    std::string current = get_button_text(GTK_WIDGET(btn));
    bool active = (current == "已启用");
    active = !active;

    std::string cmd = SETUP_SCRIPT + (active ? " enable" : " disable");
    system(cmd.c_str());
    set_button_text(GTK_WIDGET(btn), active ? "已启用" : "已停用");
    settings_set_info(active ? "阅读统计已启用" : "阅读统计已停用");
}

void on_reset_data(GtkButton *btn, gpointer user_data)
{
    GtkWidget *size_label = GTK_WIDGET(user_data);

    if (!g_settings_clear_pending) {
        g_settings_clear_pending = true;
        set_button_text(GTK_WIDGET(btn), "确认清除");
        settings_set_info("再次点击「确认清除」将删除所有统计数据，此操作不可撤销");
        return;
    }

    g_settings_clear_pending = false;
    std::string cmd = SETUP_SCRIPT + " reset";
    system(cmd.c_str());
    set_button_text(GTK_WIDGET(btn), "清除");
    gtk_label_set_text(GTK_LABEL(size_label), "0 KiB");
    settings_set_info("统计数据已清除");
    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, true);
}

void on_target_change(GtkButton *btn, gpointer data)
{
    int change = GPOINTER_TO_INT(data);
    int new_target = g_daily_target_minutes + change;

    if (new_target < MIN_TARGET_MINUTES) new_target = MIN_TARGET_MINUTES;
    if (new_target > MAX_TARGET_MINUTES) new_target = MAX_TARGET_MINUTES;

    if (new_target != g_daily_target_minutes) {
        g_daily_target_minutes = new_target;
        save_target_config();

        GtkWidget *label = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "target_label"));
        char buf[32];
        snprintf(buf, sizeof(buf), "%d 分钟", g_daily_target_minutes);
        gtk_label_set_text(GTK_LABEL(label), buf);
        settings_set_info("每日阅读目标已更新为 " + std::to_string(g_daily_target_minutes) + " 分钟");
    }
}

static GtkWidget* create_app_settings_page()
{
    GtkWidget *eventbox = gtk_event_box_new();
    gtk_widget_modify_bg(eventbox, GTK_STATE_NORMAL, &white);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 16);
    GtkWidget *align_box = gtk_alignment_new(0.5f, 0.05f, 0.82f, 0.0f);
    gtk_container_add(GTK_CONTAINER(eventbox), align_box);
    gtk_container_add(GTK_CONTAINER(align_box), vbox);

    GtkWidget *title = gtk_label_new("设置");
    set_font(title, "Sans Bold 18");
    gtk_misc_set_alignment(GTK_MISC(title), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    // 每日阅读目标
    GtkWidget *hbox_target = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_target = gtk_label_new("每日阅读目标");
    set_font(lbl_target, "Sans 14");

    GtkWidget *btn_minus10 = gtk_button_new_with_label("-10");
    GtkWidget *btn_minus1 = gtk_button_new_with_label("-1");
    GtkWidget *label_target_val = gtk_label_new("");
    GtkWidget *btn_plus1 = gtk_button_new_with_label("+1");
    GtkWidget *btn_plus10 = gtk_button_new_with_label("+10");

    gtk_widget_set_size_request(btn_minus10, 80, 50);
    gtk_widget_set_size_request(btn_minus1, 80, 50);
    gtk_widget_set_size_request(btn_plus1, 80, 50);
    gtk_widget_set_size_request(btn_plus10, 80, 50);

    char target_buf[32];
    snprintf(target_buf, sizeof(target_buf), "%d 分钟", g_daily_target_minutes);
    gtk_label_set_text(GTK_LABEL(label_target_val), target_buf);
    set_font(label_target_val, "Sans 14");

    g_signal_connect(G_OBJECT(btn_minus10), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(-10));
    g_signal_connect(G_OBJECT(btn_minus1), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(-1));
    g_signal_connect(G_OBJECT(btn_plus1), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(1));
    g_signal_connect(G_OBJECT(btn_plus10), "clicked", G_CALLBACK(on_target_change), GINT_TO_POINTER(10));

    gtk_box_pack_start(GTK_BOX(hbox_target), lbl_target, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_plus10, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_plus1, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(hbox_target), label_target_val, FALSE, FALSE, 15);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_minus1, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(hbox_target), btn_minus10, FALSE, FALSE, 5);

    g_object_set_data(G_OBJECT(btn_minus10), "target_label", label_target_val);
    g_object_set_data(G_OBJECT(btn_minus1), "target_label", label_target_val);
    g_object_set_data(G_OBJECT(btn_plus1), "target_label", label_target_val);
    g_object_set_data(G_OBJECT(btn_plus10), "target_label", label_target_val);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_target, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 0);

    // 统计开关
    GtkWidget *hbox_enable = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_enable = gtk_label_new("启用阅读统计");
    set_font(lbl_enable, "Sans 14");
    GtkWidget *btn_enable = gtk_button_new();
    bool enabled = is_metrics_enabled();
    style_button(btn_enable, enabled ? "已启用" : "已停用");
    g_signal_connect(G_OBJECT(btn_enable), "clicked", G_CALLBACK(on_toggle_enable_button), NULL);
    gtk_box_pack_start(GTK_BOX(hbox_enable), lbl_enable, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_enable), btn_enable, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_enable, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 0);

    // 数据占用
    GtkWidget *hbox_size = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_size_title = gtk_label_new("统计数据已占用");
    set_font(lbl_size_title, "Sans 14");
    char size_buf[64];
    snprintf(size_buf, sizeof(size_buf), "%ld KiB", get_log_dir_size_kib());
    g_settings_size = gtk_label_new(size_buf);
    set_font(g_settings_size, "Sans 14");
    gtk_box_pack_start(GTK_BOX(hbox_size), lbl_size_title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_size), g_settings_size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 0);

    // 清除数据
    GtkWidget *hbox_clear = gtk_hbox_new(FALSE, 10);
    GtkWidget *lbl_clear = gtk_label_new("清除统计数据");
    set_font(lbl_clear, "Sans 14");
    g_settings_clear_btn = gtk_button_new_with_label("清除");
    style_button(g_settings_clear_btn, "清除");
    g_signal_connect(G_OBJECT(g_settings_clear_btn), "clicked",
                     G_CALLBACK(on_reset_data), g_settings_size);
    gtk_box_pack_start(GTK_BOX(hbox_clear), lbl_clear, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_clear), g_settings_clear_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_clear, FALSE, FALSE, 0);

    // 设置页状态
    GtkWidget *info_title = gtk_label_new("状态 / 提示信息");
    set_font(info_title, "Sans Bold 11");
    gtk_misc_set_alignment(GTK_MISC(info_title), 0.0f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), info_title, FALSE, FALSE, 10);

    g_settings_info = gtk_label_new("设置修改会立即生效");
    gtk_label_set_line_wrap(GTK_LABEL(g_settings_info), TRUE);
    gtk_label_set_justify(GTK_LABEL(g_settings_info), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(g_settings_info), 0.0f, 0.0f);
    gtk_widget_set_size_request(g_settings_info, -1, 56);
    set_font(g_settings_info, "Sans 11");
    gtk_box_pack_start(GTK_BOX(vbox), g_settings_info, FALSE, FALSE, 0);

    return eventbox;
}

// ---------------------------------------------------------------------------
// 更多页签
// ---------------------------------------------------------------------------

static void on_more_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data)
{
    (void)notebook;
    (void)page;
    (void)data;

    if (page_num == 0) {
        user_center_refresh();
    } else if (page_num == 1) {
        bool loaded = g_rank_page != NULL &&
            g_object_get_data(G_OBJECT(g_rank_page), "rank_loaded") != NULL;
        if (!loaded && g_rank != NULL && !g_rank->loading) {
            rank_refresh_async();
        }
    } else if (page_num == 2 && g_settings_size != NULL) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%ld KiB", get_log_dir_size_kib());
        gtk_label_set_text(GTK_LABEL(g_settings_size), buf);
    }
}

GtkWidget* create_settings_page()
{
    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
    gtk_widget_modify_bg(notebook, GTK_STATE_NORMAL, &white);

    PangoFontDescription *tab_font = pango_font_description_from_string("Sans Bold 13");

    GtkWidget *tab_user = gtk_label_new("用户中心");
    GtkWidget *tab_rank = gtk_label_new("排行榜");
    GtkWidget *tab_settings = gtk_label_new("设置");
    gtk_widget_modify_font(tab_user, tab_font);
    gtk_widget_modify_font(tab_rank, tab_font);
    gtk_widget_modify_font(tab_settings, tab_font);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), create_user_center_page(), tab_user);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), create_rank_page(), tab_rank);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), create_app_settings_page(), tab_settings);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);

    pango_font_description_free(tab_font);

    g_signal_connect(G_OBJECT(notebook), "switch-page",
                     G_CALLBACK(on_more_switch_page), NULL);

    // 确保窗口显示后默认停留在“用户中心”，而不是受构建顺序影响。
    g_idle_add([](gpointer widget) -> gboolean {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(widget), 0);
        return FALSE;
    }, notebook);

    return notebook;
}
