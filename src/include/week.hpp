#ifndef WEEK_HPP
#define WEEK_HPP

#include <string>
#include <ctime>
#include <gtk/gtk.h>

gboolean draw_week_dist(GtkWidget *widget, GdkEventExpose *event, gpointer data);

GtkWidget* create_week_page();


#endif