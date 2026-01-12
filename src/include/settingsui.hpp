#ifndef SETTINGSUI_HPP
#define SETTINGSUI_HPP
#include <gtk/gtk.h>

void style_button(GtkWidget *btn, const char *text);
long get_log_dir_size_kib();
bool is_metrics_enabled();
void on_toggle_enable_button(GtkButton *btn, gpointer data);
void on_reset_data(GtkButton *btn, gpointer user_data);
void on_target_change(GtkButton *btn, gpointer data);

GtkWidget* create_settings_page();

#endif