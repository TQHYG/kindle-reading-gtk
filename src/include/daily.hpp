#ifndef DAILY_HPP
#define DAILY_HPP
#include <gtk/gtk.h>
#include "types.hpp"

gboolean draw_today_dist(GtkWidget *widget, GdkEventExpose *event, gpointer data);
void update_daily_view_ui(DailyViewWidgets *dv);
void on_daily_change(GtkButton *btn, gpointer data);

GtkWidget* create_today_page();


#endif