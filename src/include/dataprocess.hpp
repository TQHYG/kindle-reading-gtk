#ifndef DATAPROCESS_HPP
#define DATAPROCESS_HPP

#include <string>
#include <ctime>

#include "types.hpp"

void parse_line_and_update(char *line, Stats &s, 
                                  time_t today_start, time_t tomorrow_start,
                                  time_t week_start, time_t week_end,
                                  time_t cur_month_start, time_t cur_month_end);


void preprocess_data();
void refresh_daily_view_data(Stats &s, time_t target_day_ts);
void read_logs_and_compute_stats(Stats &s, int view_year, int view_month, bool force_reload);

#endif