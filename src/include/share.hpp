#ifndef SHARE_HPP
#define SHARE_HPP

#include <gtk/gtk.h>
#include <string>
#include <ctime>

std::string generate_share_url();
gboolean draw_qrcode(GtkWidget *widget, GdkEventExpose *event, gpointer data);

void create_share_dialog();

#endif