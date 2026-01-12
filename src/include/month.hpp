#ifndef MONTH_HPP
#define MONTH_HPP

#include <gtk/gtk.h>
#include "types.hpp"
#include "utils.hpp"

double get_gray_level_16(double ratio);
gboolean draw_month_view(GtkWidget *widget, GdkEventExpose *event, gpointer data);
void update_month_title(MonthViewWidgets *mv);

void month_prev(GtkButton *b, gpointer data);
void month_next(GtkButton *b, gpointer data);
gboolean on_month_click(GtkWidget *widget, GdkEventButton *event, gpointer data);

GtkWidget* create_month_page();

#endif