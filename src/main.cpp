// main.cpp - Kindle GTK2 reading stats app 

// INCLUDE

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
#include <map>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "types.hpp"
#include "utils.hpp"
#include "dataprocess.hpp"
#include "share.hpp"
#include "daily.hpp"
#include "week.hpp"
#include "month.hpp"
#include "overview.hpp"
#include "network.hpp"
#include "settingsui.hpp"

// —— 常量定义 ——
// 基础路径
const std::string BASE_DIR = "/mnt/us/extensions/kykky/";

// 派生路径
const std::string LOG_DIR = BASE_DIR + "log/";
const std::string ETC_ENABLE_FILE = BASE_DIR + "etc/enable";
const std::string SETUP_SCRIPT = BASE_DIR + "bin/metrics_setup.sh";
const std::string ARCHIVE_FILE = BASE_DIR + "log/history.gz";
const std::string CONFIG_FILE = BASE_DIR + "etc/config.ini";
const std::string ETC_TOKEN_FILE = BASE_DIR + "etc/token";
const std::string STATE_FILE = BASE_DIR + "etc/state";

const char *LOG_PREFIX = "metrics_reader_"; 
const char *TEMP_LOG_FILE = "/tmp/kykky_history.log";

// 其他常量
const int DEFAULT_TARGET_MINUTES = 30;
const int MIN_TARGET_MINUTES = 10;
const int MAX_TARGET_MINUTES = 180;

const char* APP_TITLE = "L:A_N:application_PC:T_ID:net.tqhyg.reading";

// 全局变量
int g_daily_target_minutes = DEFAULT_TARGET_MINUTES;
std::string g_share_domain = "reading.tqhyg.net";

GdkColor white = {0, 0xffff, 0xffff, 0xffff};
GdkColor gray = {0, 0x8888, 0x8888, 0x8888};

Stats g_stats;
int g_view_year;
int g_view_month;
time_t g_view_daily_ts;

UIHandles g_ui_handles = {NULL, NULL};
GtkWidget *g_notebook = NULL;      // 全局笔记本控件指针
DailyViewWidgets *g_daily_widgets = NULL; // 全局日视图组件指针

bool check_and_raise_existing_instance(const char* window_title) {
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return false;

    Window root = DefaultRootWindow(dpy);
    Window parent;
    Window *children;
    unsigned int nchildren;
    bool found = false;

    // 遍历根窗口的子窗口
    if (XQueryTree(dpy, root, &root, &parent, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            char *name = NULL;
            // 获取窗口标题
            if (XFetchName(dpy, children[i], &name) && name) {
                if (strcmp(name, window_title) == 0) {
                    // 找到已运行的实例
                    
                    // 1. 将窗口置顶 (MapRaised)
                    XMapRaised(dpy, children[i]);
                    
                    // 2. 尝试给予输入焦点
                    XSetInputFocus(dpy, children[i], RevertToParent, CurrentTime);
                    
                    // 3. 刷新显示
                    XFlush(dpy);
                    
                    found = true;
                    XFree(name);
                    break;
                }
                XFree(name);
            }
        }
        if (children) XFree(children);
    }

    XCloseDisplay(dpy);
    return found;
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
    // 0. 单例检查
    
    if (check_and_raise_existing_instance(APP_TITLE)) {
        printf("Application is already running. Raised existing window.\n");
        return 0; // 退出新进程
    }

    gtk_init(&argc, &argv);

    // 1. 初始化窗口和主容器
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), APP_TITLE);
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
    KykkyNetwork::instance().init();

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    g_view_year = tmv.tm_year + 1900;
    g_view_month = tmv.tm_mon + 1;

    read_logs_and_compute_stats(g_stats, g_view_year, g_view_month, true);
    if (KykkyNetwork::instance().check_internet()) {
        KykkyNetwork::instance().upload_data(g_stats.today_seconds, g_stats.month_seconds);
    }

    // 5. 销毁等待界面并切换到正式内容
    gtk_widget_destroy(align); 

    GtkWidget *nb = gtk_notebook_new();
    g_notebook = nb;
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
    add_tab(create_today_page(), "时段详情");
    add_tab(create_week_page(), "周分布");
    add_tab(create_month_page(), "阅读日历");
    add_tab(create_settings_page(), "更多");
    add_tab(create_exit_page(), " X ");

    pango_font_description_free(tab_font);

    g_signal_connect(G_OBJECT(nb), "switch-page", G_CALLBACK(on_notebook_switch_page), NULL);

    gtk_widget_show_all(win);
    gtk_main();

    // --- 清理临时文件 ---
    unlink(TEMP_LOG_FILE);
    return 0;
}
