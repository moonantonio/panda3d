/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file gtkStatsMonitor.cxx
 * @author drose
 * @date 2006-01-16
 */

#include "gtkStatsMonitor.h"
#include "gtkStats.h"
#include "gtkStatsServer.h"
#include "gtkStatsStripChart.h"
#include "gtkStatsChartMenu.h"
#include "gtkStatsPianoRoll.h"
#include "gtkStatsFlameGraph.h"
#include "gtkStatsTimeline.h"
#include "gtkStatsMenuId.h"
#include "pStatGraph.h"
#include "pStatCollectorDef.h"

/**
 *
 */
GtkStatsMonitor::
GtkStatsMonitor(GtkStatsServer *server) : PStatMonitor(server) {
  _window = nullptr;

  // These will be filled in later when the menu is created.
  _time_units = 0;
  _scroll_speed = 0.0;
  _pause = false;

  _resolution = gdk_screen_get_resolution(gdk_screen_get_default());
}

/**
 *
 */
GtkStatsMonitor::
~GtkStatsMonitor() {
  shutdown();
}

/**
 * Should be redefined to return a descriptive name for the type of
 * PStatsMonitor this is.
 */
std::string GtkStatsMonitor::
get_monitor_name() {
  return "GtkStats";
}

/**
 * Called after the monitor has been fully set up.  At this time, it will have
 * a valid _client_data pointer, and things like is_alive() and close() will
 * be meaningful.  However, we may not yet know who we're connected to
 * (is_client_known() may return false), and we may not know anything about
 * the threads or collectors we're about to get data on.
 */
void GtkStatsMonitor::
initialized() {
}

/**
 * Called when the "hello" message has been received from the client.  At this
 * time, the client's hostname and program name will be known.
 */
void GtkStatsMonitor::
got_hello() {
  create_window();
  open_strip_chart(0, 0, false);
}

/**
 * Like got_hello(), this is called when the "hello" message has been received
 * from the client.  At this time, the client's hostname and program name will
 * be known.  However, the client appears to be an incompatible version and
 * the connection will be terminated; the monitor should issue a message to
 * that effect.
 */
void GtkStatsMonitor::
got_bad_version(int client_major, int client_minor,
                int server_major, int server_minor) {
  std::ostringstream str;
  str << "Unable to honor connection attempt from "
      << get_client_progname() << " on " << get_client_hostname()
      << ": unsupported PStats version "
      << client_major << "." << client_minor;

  if (server_minor == 0) {
    str << " (server understands version " << server_major
        << "." << server_minor << " only).";
  } else {
    str << " (server understands versions " << server_major
        << ".0 through " << server_major << "." << server_minor << ").";
  }

  std::string message = str.str();
  GtkWidget *dialog =
    gtk_message_dialog_new(GTK_WINDOW(main_window),
                           GTK_DIALOG_DESTROY_WITH_PARENT,
                           GTK_MESSAGE_ERROR,
                           GTK_BUTTONS_CLOSE,
                           "%s", message.c_str());
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

/**
 * Called whenever a new Collector definition is received from the client.
 * Generally, the client will send all of its collectors over shortly after
 * connecting, but there's no guarantee that they will all be received before
 * the first frames are received.  The monitor should be prepared to accept
 * new Collector definitions midstream.
 */
void GtkStatsMonitor::
new_collector(int collector_index) {
  Graphs::iterator gi;
  for (gi = _graphs.begin(); gi != _graphs.end(); ++gi) {
    GtkStatsGraph *graph = (*gi);
    graph->new_collector(collector_index);
  }

  // We might need to update our menus.
  ChartMenus::iterator mi;
  for (mi = _chart_menus.begin(); mi != _chart_menus.end(); ++mi) {
    (*mi)->do_update();
  }
}

/**
 * Called whenever a new Thread definition is received from the client.
 * Generally, the client will send all of its threads over shortly after
 * connecting, but there's no guarantee that they will all be received before
 * the first frames are received.  The monitor should be prepared to accept
 * new Thread definitions midstream.
 */
void GtkStatsMonitor::
new_thread(int thread_index) {
  GtkStatsChartMenu *chart_menu = new GtkStatsChartMenu(this, thread_index);
  chart_menu->add_to_menu_bar(_menu_bar, _next_chart_index);
  ++_next_chart_index;
  _chart_menus.push_back(chart_menu);
}

/**
 * Called as each frame's data is made available.  There is no guarantee the
 * frames will arrive in order, or that all of them will arrive at all.  The
 * monitor should be prepared to accept frames received out-of-order or
 * missing.
 */
void GtkStatsMonitor::
new_data(int thread_index, int frame_number) {
  for (GtkStatsGraph *graph : _graphs) {
    graph->new_data(thread_index, frame_number);
  }

  if (thread_index == 0) {
    update_status_bar();
  }
}

/**
 * Called whenever the connection to the client has been lost.  This is a
 * permanent state change.  The monitor should update its display to represent
 * this, and may choose to close down automatically.
 */
void GtkStatsMonitor::
lost_connection() {
  nout << "Lost connection to " << get_client_hostname() << "\n";

  shutdown();
}

/**
 * If has_idle() returns true, this will be called periodically to allow the
 * monitor to update its display or whatever it needs to do.
 */
void GtkStatsMonitor::
idle() {
  // Check if any of our chart menus need updating.
  ChartMenus::iterator mi;
  for (mi = _chart_menus.begin(); mi != _chart_menus.end(); ++mi) {
    (*mi)->check_update();
  }

  // Update the frame rate label from the main thread (thread 0).
  const PStatThreadData *thread_data = get_client_data()->get_thread_data(0);
  double frame_rate = thread_data->get_frame_rate();
  if (frame_rate != 0.0f) {
    char buffer[128];
    sprintf(buffer, "%0.1f ms / %0.1f Hz", 1000.0f / frame_rate, frame_rate);

    gtk_label_set_text(GTK_LABEL(_frame_rate_label), buffer);

    if (!_status_bar_labels.empty()) {
      gtk_label_set_text(GTK_LABEL(_status_bar_labels[0]), buffer);
    }
  }
}

/**
 * Should be redefined to return true if you want to redefine idle() and
 * expect it to be called.
 */
bool GtkStatsMonitor::
has_idle() {
  return true;
}

/**
 * Called when the user guide bars have been changed.
 */
void GtkStatsMonitor::
user_guide_bars_changed() {
  Graphs::iterator gi;
  for (gi = _graphs.begin(); gi != _graphs.end(); ++gi) {
    GtkStatsGraph *graph = (*gi);
    graph->user_guide_bars_changed();
  }
}

/**
 * Returns the window handle to the monitor's window.
 */
GtkWidget *GtkStatsMonitor::
get_window() const {
  return _window;
}

/**
 * Returns the screen DPI.
 */
double GtkStatsMonitor::
get_resolution() const {
  return _resolution;
}

/**
 * Opens a new strip chart showing the indicated data.
 */
void GtkStatsMonitor::
open_strip_chart(int thread_index, int collector_index, bool show_level) {
  GtkStatsStripChart *graph =
    new GtkStatsStripChart(this, thread_index, collector_index, show_level);
  add_graph(graph);

  graph->set_time_units(_time_units);
  graph->set_scroll_speed(_scroll_speed);
  graph->set_pause(_pause);
}

/**
 * Opens a new piano roll showing the indicated data.
 */
void GtkStatsMonitor::
open_piano_roll(int thread_index) {
  GtkStatsPianoRoll *graph = new GtkStatsPianoRoll(this, thread_index);
  add_graph(graph);

  graph->set_time_units(_time_units);
  graph->set_scroll_speed(_scroll_speed);
  graph->set_pause(_pause);
}

/**
 * Opens a new flame graph showing the indicated data.
 */
void GtkStatsMonitor::
open_flame_graph(int thread_index, int collector_index) {
  GtkStatsFlameGraph *graph = new GtkStatsFlameGraph(this, thread_index, collector_index);
  add_graph(graph);

  graph->set_time_units(_time_units);
  graph->set_scroll_speed(_scroll_speed);
  graph->set_pause(_pause);
}

/**
 * Opens a new timeline.
 */
void GtkStatsMonitor::
open_timeline() {
  GtkStatsTimeline *graph = new GtkStatsTimeline(this);
  add_graph(graph);

  graph->set_time_units(_time_units);
  graph->set_scroll_speed(_scroll_speed);
  graph->set_pause(_pause);
}

/**
 * Adds a new MenuDef to the monitor, or returns an existing one if there is
 * already one just like it.
 */
const GtkStatsMonitor::MenuDef *GtkStatsMonitor::
add_menu(const MenuDef &menu_def) {
  std::pair<Menus::iterator, bool> result = _menus.insert(menu_def);
  Menus::iterator mi = result.first;
  const GtkStatsMonitor::MenuDef &new_menu_def = (*mi);
  if (result.second) {
    // A new MenuDef was inserted.
    ((GtkStatsMonitor::MenuDef &)new_menu_def)._monitor = this;
  }
  return &new_menu_def;
}

/**
 * Called when the user selects a new time units from the monitor pulldown
 * menu, this should adjust the units for all graphs to the indicated mask if
 * it is a time-based graph.
 */
void GtkStatsMonitor::
set_time_units(int unit_mask) {
  _time_units = unit_mask;

  // First, change all of the open graphs appropriately.
  Graphs::iterator gi;
  for (gi = _graphs.begin(); gi != _graphs.end(); ++gi) {
    GtkStatsGraph *graph = (*gi);
    graph->set_time_units(_time_units);
  }
}

/**
 * Called when the user selects a new scroll speed from the monitor pulldown
 * menu, this should adjust the speeds for all graphs to the indicated value.
 */
void GtkStatsMonitor::
set_scroll_speed(double scroll_speed) {
  _scroll_speed = scroll_speed;

  // First, change all of the open graphs appropriately.
  Graphs::iterator gi;
  for (gi = _graphs.begin(); gi != _graphs.end(); ++gi) {
    GtkStatsGraph *graph = (*gi);
    graph->set_scroll_speed(_scroll_speed);
  }
}

/**
 * Called when the user selects a pause on or pause off option from the menu.
 */
void GtkStatsMonitor::
set_pause(bool pause) {
  _pause = pause;

  // First, change all of the open graphs appropriately.
  Graphs::iterator gi;
  for (gi = _graphs.begin(); gi != _graphs.end(); ++gi) {
    GtkStatsGraph *graph = (*gi);
    graph->set_pause(_pause);
  }
}

/**
 * Adds the newly-created graph to the list of managed graphs.
 */
void GtkStatsMonitor::
add_graph(GtkStatsGraph *graph) {
  _graphs.insert(graph);
}

/**
 * Deletes the indicated graph.
 */
void GtkStatsMonitor::
remove_graph(GtkStatsGraph *graph) {
  Graphs::iterator gi = _graphs.find(graph);
  if (gi != _graphs.end()) {
    _graphs.erase(gi);
    delete graph;
  }
}

/**
 * Creates the window for this monitor.
 */
void GtkStatsMonitor::
create_window() {
  if (_window != nullptr) {
    return;
  }

  _window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  g_signal_connect(G_OBJECT(_window), "delete_event",
       G_CALLBACK(window_delete_event), this);
  g_signal_connect(G_OBJECT(_window), "destroy",
       G_CALLBACK(window_destroy), this);

  _window_title = get_client_progname() + " on " + get_client_hostname();
  gtk_window_set_title(GTK_WINDOW(_window), _window_title.c_str());

  gtk_window_set_default_size(GTK_WINDOW(_window), 500, 360);

  // Set up the menu.
  GtkAccelGroup *accel_group = gtk_accel_group_new();
  gtk_window_add_accel_group(GTK_WINDOW(_window), accel_group);
  _menu_bar = gtk_menu_bar_new();
  _next_chart_index = 2;

  setup_options_menu();
  setup_speed_menu();
  setup_frame_rate_label();

  for (GtkStatsChartMenu *chart_menu : _chart_menus) {
    chart_menu->add_to_menu_bar(_menu_bar, _next_chart_index);
    ++_next_chart_index;
  }

  // Pack the menu into the window.
  GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  gtk_container_add(GTK_CONTAINER(_window), main_vbox);
  gtk_box_pack_start(GTK_BOX(main_vbox), _menu_bar, FALSE, TRUE, 0);

  // Create the status bar.
  _status_bar = gtk_flow_box_new();
  gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(_status_bar), FALSE);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(_status_bar), GTK_SELECTION_NONE);
  g_signal_connect(G_OBJECT(_status_bar), "button_press_event",
    G_CALLBACK(status_bar_button_event), this);
  gtk_box_pack_end(GTK_BOX(main_vbox), _status_bar, FALSE, FALSE, 0);
  update_status_bar();

  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_end(GTK_BOX(main_vbox), sep, FALSE, FALSE, 0);

  gtk_widget_show_all(_window);
  gtk_widget_show(_window);
  gtk_widget_realize(_window);
}

/**
 * Closes all the graphs associated with this monitor.
 */
void GtkStatsMonitor::
shutdown() {
  Graphs::iterator gi;
  for (gi = _graphs.begin(); gi != _graphs.end(); ++gi) {
    delete (*gi);
  }
  _graphs.clear();

  ChartMenus::iterator mi;
  for (mi = _chart_menus.begin(); mi != _chart_menus.end(); ++mi) {
    delete (*mi);
  }
  _chart_menus.clear();

  if (_window != nullptr) {
    gtk_widget_destroy(_window);
    _window = nullptr;
  }

#ifdef DEVELOP_GTKSTATS
  // For GtkStats developers, exit when the first monitor closes.
  gtk_main_quit();
#endif
}

/**
 * Callback when the window is closed by the user.
 */
gboolean GtkStatsMonitor::
window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
  // Returning FALSE to indicate we should destroy the window when the user
  // selects "close".
  return FALSE;
}

/**
 * Callback when the window is destroyed by the system (or by delete_event).
 */
void GtkStatsMonitor::
window_destroy(GtkWidget *widget, gpointer data) {
  GtkStatsMonitor *self = (GtkStatsMonitor *)data;
  self->close();
}


/**
 * Creates the "Options" pulldown menu.
 */
void GtkStatsMonitor::
setup_options_menu() {
  _options_menu = gtk_menu_new();

  GtkWidget *item = gtk_menu_item_new_with_label("Options");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), _options_menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(_menu_bar), item);

  GtkWidget *units_menu = gtk_menu_new();
  item = gtk_menu_item_new_with_label("Units");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), units_menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(_options_menu), item);

  item = gtk_radio_menu_item_new_with_label(nullptr, "ms");
  gtk_menu_shell_append(GTK_MENU_SHELL(units_menu), item);
  g_signal_connect(G_OBJECT(item), "activate",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      GtkStatsMonitor *self = (GtkStatsMonitor *)data;
      self->set_time_units(PStatGraph::GBU_ms);
    }), this);

  item = gtk_radio_menu_item_new_with_label(
    gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item)), "Hz");
  gtk_menu_shell_append(GTK_MENU_SHELL(units_menu), item);
  g_signal_connect(G_OBJECT(item), "activate",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      GtkStatsMonitor *self = (GtkStatsMonitor *)data;
      self->set_time_units(PStatGraph::GBU_hz);
    }), this);

  set_time_units(PStatGraph::GBU_ms);
}

/**
 * Creates the "Speed" pulldown menu.
 */
void GtkStatsMonitor::
setup_speed_menu() {
  _speed_menu = gtk_menu_new();

  GtkWidget *item = gtk_menu_item_new_with_label("Speed");
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), _speed_menu);
  gtk_menu_shell_append(GTK_MENU_SHELL(_menu_bar), item);

  GSList *group = nullptr;
  item = gtk_radio_menu_item_new_with_label(group, "1");
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);
  g_signal_connect(G_OBJECT(item), "toggled",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        GtkStatsMonitor *self = (GtkStatsMonitor *)data;
        self->set_scroll_speed(1);
      }
    }), this);
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));

  item = gtk_radio_menu_item_new_with_label(group, "2");
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);
  g_signal_connect(G_OBJECT(item), "toggled",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        GtkStatsMonitor *self = (GtkStatsMonitor *)data;
        self->set_scroll_speed(2);
      }
    }), this);
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));

  item = gtk_radio_menu_item_new_with_label(group, "3");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);
  g_signal_connect(G_OBJECT(item), "toggled",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        GtkStatsMonitor *self = (GtkStatsMonitor *)data;
        self->set_scroll_speed(3);
      }
    }), this);
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));

  item = gtk_radio_menu_item_new_with_label(group, "6");
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);
  g_signal_connect(G_OBJECT(item), "toggled",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        GtkStatsMonitor *self = (GtkStatsMonitor *)data;
        self->set_scroll_speed(6);
      }
    }), this);
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));

  item = gtk_radio_menu_item_new_with_label(group, "12");
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);
  g_signal_connect(G_OBJECT(item), "toggled",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
        GtkStatsMonitor *self = (GtkStatsMonitor *)data;
        self->set_scroll_speed(12);
      }
    }), this);
  group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));

  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);

  item = gtk_check_menu_item_new_with_label("pause");
  gtk_menu_shell_append(GTK_MENU_SHELL(_speed_menu), item);
  g_signal_connect(G_OBJECT(item), "toggled",
    G_CALLBACK(+[](GtkMenuItem *item, gpointer data) {
      GtkStatsMonitor *self = (GtkStatsMonitor *)data;
      self->set_pause(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)));
    }), this);

  set_scroll_speed(3);
  set_pause(false);
}

/**
 * Creates the frame rate label on the right end of the menu bar.  This is
 * used as a text label to display the main thread's frame rate to the user,
 * although it is implemented as a right-justified toplevel menu item that
 * doesn't open to anything.
 */
void GtkStatsMonitor::
setup_frame_rate_label() {
  _frame_rate_menu_item = gtk_menu_item_new();
  _frame_rate_label = gtk_label_new("");
  gtk_container_add(GTK_CONTAINER(_frame_rate_menu_item), _frame_rate_label);
  gtk_widget_set_sensitive(_frame_rate_menu_item, FALSE);

  gtk_widget_show(_frame_rate_menu_item);
  gtk_widget_show(_frame_rate_label);
  gtk_menu_item_set_right_justified(GTK_MENU_ITEM(_frame_rate_menu_item), TRUE);

  gtk_menu_shell_append(GTK_MENU_SHELL(_menu_bar), _frame_rate_menu_item);
}

/**
 * Updates the status bar.
 */
void GtkStatsMonitor::
update_status_bar() {
  const PStatClientData *client_data = get_client_data();
  if (client_data == nullptr) {
    return;
  }

  const PStatThreadData *thread_data = get_client_data()->get_thread_data(0);
  if (thread_data == nullptr || thread_data->is_empty()) {
    return;
  }
  const PStatFrameData &frame_data = thread_data->get_latest_frame();

  pvector<int> collectors;

  // The first label displays the frame rate.
  size_t li = 1;
  collectors.push_back(0);
  if (_status_bar_labels.empty()) {
    GtkWidget *label = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(_status_bar), label);
    _status_bar_labels.push_back(label);
  }

  // Gather the top-level collector list.
  int num_toplevel_collectors = client_data->get_num_toplevel_collectors();
  for (int tc = 0; tc < num_toplevel_collectors; tc++) {
    int collector = client_data->get_toplevel_collector(tc);
    if (client_data->has_collector(collector) &&
        client_data->get_collector_has_level(collector, 0)) {
      PStatView &view = get_level_view(collector, 0);
      view.set_to_frame(frame_data);
      double value = view.get_net_value();
      if (value == 0.0) {
        // Don't include it unless we've included it before.
        if (std::find(_status_bar_collectors.begin(), _status_bar_collectors.end(), collector) == _status_bar_collectors.end()) {
          continue;
        }
      }

      const PStatCollectorDef &def = client_data->get_collector_def(collector);
      std::string text = def._name;
      text += ": " + PStatGraph::format_number(value, PStatGraph::GBU_named | PStatGraph::GBU_show_units, def._level_units);

      GtkWidget *label;
      if (li < _status_bar_labels.size()) {
        label = _status_bar_labels[li++];
        gtk_label_set_text(GTK_LABEL(label), text.c_str());
      }
      else {
        label = gtk_label_new(text.c_str());
        gtk_container_add(GTK_CONTAINER(_status_bar), label);
        _status_bar_labels.push_back(label);
      }

      collectors.push_back(collector);
    }
  }

  _status_bar_collectors = std::move(collectors);

  gtk_widget_show_all(_status_bar);
}

/**
 * Handles clicks on a partion of the status bar.
 */
gboolean GtkStatsMonitor::
status_bar_button_event(GtkWidget *widget, GdkEventButton *event, gpointer data) {
  GtkStatsMonitor *monitor = (GtkStatsMonitor *)data;

  GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos(
    GTK_FLOW_BOX(monitor->_status_bar), event->x, event->y);
  if (child == nullptr) {
    return FALSE;
  }

  // Which child is this?
  GList *children = gtk_container_get_children(GTK_CONTAINER(monitor->_status_bar));
  int index = g_list_index(children, child);
  g_list_free(children);
  if (index < 0 || index >= monitor->_status_bar_labels.size()) {
    return FALSE;
  }

  const PStatClientData *client_data = monitor->get_client_data();
  if (client_data == nullptr) {
    return FALSE;
  }

  int collector = monitor->_status_bar_collectors[index];

  if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
    monitor->open_strip_chart(0, collector, collector != 0);
    return TRUE;
  }
  else if (event->type == GDK_BUTTON_PRESS && event->button == 3 && index > 0) {
    PStatView &level_view = monitor->get_level_view(collector, 0);
    const PStatViewLevel *view_level = level_view.get_top_level();
    int num_children = view_level->get_num_children();
    if (num_children == 0) {
      return FALSE;
    }

    GtkWidget *menu = gtk_menu_new();

    // Reverse the order since the menus are listed from the top down; we want
    // to be visually consistent with the graphs, which list these labels from
    // the bottom up.
    for (int c = num_children - 1; c >= 0; c--) {
      const PStatViewLevel *child_level = view_level->get_child(c);

      int child_collector = child_level->get_collector();
      const MenuDef *menu_def = monitor->add_menu({0, child_collector, CT_strip_chart, true});

      double value = child_level->get_net_value();

      const PStatCollectorDef &def = client_data->get_collector_def(child_collector);
      std::string text = def._name;
      text += ": " + PStatGraph::format_number(value, PStatGraph::GBU_named | PStatGraph::GBU_show_units, def._level_units);

      GtkWidget *menu_item = gtk_menu_item_new_with_label(text.c_str());
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

      g_signal_connect(G_OBJECT(menu_item), "activate",
                       G_CALLBACK(menu_activate),
                       (void *)menu_def);
    }

    gtk_widget_show_all(menu);

    GtkWidget *label = monitor->_status_bar_labels[index];
    gtk_menu_popup_at_widget(GTK_MENU(menu), label,
                             GDK_GRAVITY_NORTH_WEST,
                             GDK_GRAVITY_SOUTH_WEST, nullptr);
    return TRUE;
  }
  return FALSE;
}

/**
 * Callback when a menu item is selected.
 */
void GtkStatsMonitor::
menu_activate(GtkWidget *widget, gpointer data) {
  const MenuDef *menu_def = (const MenuDef *)data;
  GtkStatsMonitor *monitor = menu_def->_monitor;

  if (monitor == nullptr) {
    return;
  }

  switch (menu_def->_chart_type) {
  case CT_timeline:
    monitor->open_timeline();
    break;

  case CT_strip_chart:
    monitor->open_strip_chart(menu_def->_thread_index,
                              menu_def->_collector_index,
                              menu_def->_show_level);
    break;

  case CT_flame_graph:
    monitor->open_flame_graph(menu_def->_thread_index,
                              menu_def->_collector_index);
    break;

  case CT_piano_roll:
    monitor->open_piano_roll(menu_def->_thread_index);
    break;
  }
}
