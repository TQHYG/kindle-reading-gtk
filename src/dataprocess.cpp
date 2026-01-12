#include <cstring>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

#include "types.hpp"
#include "utils.hpp"
#include "dataprocess.hpp"

// 解析单行并更新 Stats
void parse_line_and_update(char *line, Stats &s, 
                                  time_t today_start, time_t tomorrow_start,
                                  time_t week_start, time_t week_end,
                                  time_t cur_month_start, time_t cur_month_end) 
{
    char *saveptr = NULL;
    char *token = strtok_r(line, ",", &saveptr);
    int idx = 0;
    long endt = 0;
    long dur_ms = 0;
    char type_field[256] = {0};

    while (token) {
        idx++;
        if (idx == 2) endt = strtol(token, NULL, 10);
        else if (idx == 6) strncpy(type_field, token, 255);
        else if (idx == 7) dur_ms = strtol(token, NULL, 10);
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (strncmp(type_field, "com.lab126.booklet.reader.activeDuration", 40) != 0)
        return;

    long dur = dur_ms / 1000;
    if (dur <= 0) return;

    time_t end_time = (time_t)endt;
    time_t start_time = end_time - dur;

    s.total_seconds += dur;

    // 1. 更新今日/本周/本月 总数 (保持原有逻辑，用于概览页快速显示)
    if (end_time > today_start && start_time < tomorrow_start) {
        time_t sclip = std::max(start_time, today_start);
        time_t eclip = std::min(end_time, tomorrow_start);
        if (eclip > sclip) s.today_seconds += (eclip - sclip);
    }

    if (end_time > week_start && start_time < week_end) {
        time_t sclip = std::max(start_time, week_start);
        time_t eclip = std::min(end_time, week_end);
        if (eclip > sclip) s.week_seconds += (eclip - sclip);
    }

    if (end_time > cur_month_start && start_time < cur_month_end) {
        time_t sclip = std::max(start_time, cur_month_start);
        time_t eclip = std::min(end_time, cur_month_end);
        if (eclip > sclip) s.month_seconds += (eclip - sclip);
    }

    // 2. 通用分桶逻辑：将这段阅读时间分配到对应的日期Map中
    time_t t_cursor = start_time;
    while (t_cursor < end_time) {
        // 获取当前游标所在的自然日 0点
        time_t day_start = get_day_start(t_cursor);
        time_t day_end = day_start + 24 * 3600;
        
        // 当前处理片段在这一天内的结束时间
        time_t seg_end = std::min(end_time, day_end);
        
        // 如果跨天，确保 Map 中该天的 vector 已初始化
        if (s.daily_detail_map[day_start].empty()) {
            s.daily_detail_map[day_start].resize(12, 0);
        }
        
        // 每日总数 Map 更新
        s.history_map[day_start] += (seg_end - t_cursor);

        // 处理当天的分桶 (2小时一桶)
        time_t bucket_cursor = t_cursor;
        while (bucket_cursor < seg_end) {
            int bi = (bucket_cursor - day_start) / 7200;
            if (bi < 0) bi = 0; 
            if (bi > 11) bi = 11;
            
            time_t bucket_end_time = day_start + (bi + 1) * 7200;
            // 桶结束时间不能超过当前片段结束时间
            if (bucket_end_time > seg_end) bucket_end_time = seg_end;
            
            long step_sec = bucket_end_time - bucket_cursor;
            if (step_sec > 0) {
                s.daily_detail_map[day_start][bi] += step_sec;
            }
            
            bucket_cursor = bucket_end_time;
        }

        t_cursor = seg_end; // 继续处理下一天（如果跨天阅读）
    }

    // 3. 本周分天 (保持不变)
    if (end_time > week_start && start_time < week_end) {
        time_t sclip = std::max(start_time, week_start);
        time_t eclip = std::min(end_time, week_end);
        time_t tcur = sclip;
        while (tcur < eclip) {
            int di = (tcur - week_start) / (24 * 3600);
            if (di < 0) di = 0; 
            if (di > 6) di = 6;
            time_t dend = week_start + (di + 1) * 24 * 3600;
            if (dend > eclip) dend = eclip;
            s.week_days[di] += (dend - tcur);
            tcur = dend;
        }
    }

}

// —— 数据预处理 ——
void preprocess_data() {
    // 1. 确定当月文件名后缀 (YYMM)
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char current_month_suffix[16];
    // tm_year 是从1900起算的年数，% 100 得到 YY
    snprintf(current_month_suffix, sizeof(current_month_suffix), "%02d%02d", 
             (tmv.tm_year + 1900) % 100, tmv.tm_mon + 1);

    char current_log_filename[128];
    snprintf(current_log_filename, sizeof(current_log_filename), "%s%s", LOG_PREFIX, current_month_suffix);

    // 2. 准备临时文件
    // 如果存在 history.gz，解压覆盖到 TEMP；否则创建空文件
    char cmd[512];
    struct stat st;
    if (stat(ARCHIVE_FILE.c_str(), &st) == 0) {
        snprintf(cmd, sizeof(cmd), "gunzip -c %s > %s", ARCHIVE_FILE.c_str(), TEMP_LOG_FILE);
        system(cmd);
    } else {
        // 创建空文件
        FILE *fp = fopen(TEMP_LOG_FILE, "w");
        if (fp) fclose(fp);
    }

    // 3. 扫描目录，追加旧日志
    DIR *dir = opendir(LOG_DIR.c_str());
    if (!dir) return;

    FILE *fp_temp = fopen(TEMP_LOG_FILE, "a"); // 追加模式
    if (!fp_temp) {
        closedir(dir);
        return;
    }

    struct dirent *ent;
    bool has_updates = false;
    char filepath[512];

    while ((ent = readdir(dir)) != NULL) {
        // 筛选 metrics_reader_ 开头
        if (strncmp(ent->d_name, LOG_PREFIX, strlen(LOG_PREFIX)) != 0) continue;
        
        // 跳过当月日志
        if (strcmp(ent->d_name, current_log_filename) == 0) continue;

        // 处理旧日志
        snprintf(filepath, sizeof(filepath), "%s%s", LOG_DIR.c_str(), ent->d_name);
        
        FILE *fp_old = fopen(filepath, "r");
        if (fp_old) {
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), fp_old)) {
                fputs(buffer, fp_temp);
            }
            fclose(fp_old);
            unlink(filepath); // 删除旧文件
            has_updates = true;
        }
    }
    fclose(fp_temp);
    closedir(dir);

    // 4. 如果有追加操作，重新压缩归档 (保存到 LOG_DIR)
    if (has_updates) {
        snprintf(cmd, sizeof(cmd), "gzip -c %s > %s", TEMP_LOG_FILE, ARCHIVE_FILE.c_str());
        system(cmd);
    }
}

// 从内存 Map 中提取指定日期的数据到 view_daily_buckets
void refresh_daily_view_data(Stats &s, time_t target_day_ts) {
    // 1. 重置当前视图数据
    std::fill(std::begin(s.view_daily_buckets), std::end(s.view_daily_buckets), 0);
    s.view_daily_seconds = 0;

    // 2. 查找 Map
    if (s.daily_detail_map.count(target_day_ts)) {
        const std::vector<long> &vec = s.daily_detail_map[target_day_ts];
        if (vec.size() >= 12) {
            for (int i = 0; i < 12; i++) {
                s.view_daily_buckets[i] = vec[i];
                s.view_daily_seconds += vec[i];
            }
        }
    }
}

// —— 读取日志与计算 ——
// 参数说明：
// force_reload: true=重新读取磁盘文件; false=仅重新生成视图数据(用于翻页)
void read_logs_and_compute_stats(Stats &s, int view_year, int view_month, bool force_reload) {
    
    // 1. 初始化/清理
    // 只有在强制重载时，才清空所有核心数据
    if (force_reload || !s.loaded) {
        s.total_seconds = 0;
        s.today_seconds = 0;
        s.week_seconds = 0;
        s.month_seconds = 0;
        s.view_daily_seconds = 0;
        
        // 使用 std::fill 初始化数组，安全且标准
        std::fill(std::begin(s.view_daily_buckets), std::end(s.view_daily_buckets), 0);
        std::fill(std::begin(s.week_days), std::end(s.week_days), 0);
        
        // 清空 Map
        s.history_map.clear();
        s.daily_detail_map.clear();
        s.loaded = true;

        // --- 准备时间边界 (用于 parse_line 里的判断) ---
        time_t today_start, tomorrow_start;
        get_today_bounds(today_start, tomorrow_start);

        time_t week_start;
        get_week_start(week_start);
        time_t week_end = week_start + 7 * 24 * 3600;

        time_t cur_month_start;
        int cur_year, cur_month;
        get_month_start(cur_month_start, cur_year, cur_month);
        time_t cur_month_end = cur_month_start + days_in_month(cur_year, cur_month) * 24 * 3600;

        // --- 读取文件 Lambda ---
        auto process_file = [&](const char* fpath) {
            FILE *fp = fopen(fpath, "r");
            if (!fp) return;
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                // 调用简化后的解析函数
                parse_line_and_update(line, s, 
                    today_start, tomorrow_start,
                    week_start, week_end,
                    cur_month_start, cur_month_end
                );
            }
            fclose(fp);
        };

        // 读取历史汇总
        process_file(TEMP_LOG_FILE);

        // 读取当月实时日志
        time_t now = time(NULL);
        struct tm now_tm;
        localtime_r(&now, &now_tm);
        char current_path[256];
        snprintf(current_path, sizeof(current_path), "%s%s%02d%02d", 
                 LOG_DIR.c_str(), LOG_PREFIX, (now_tm.tm_year + 1900) % 100, now_tm.tm_mon + 1);
        process_file(current_path);
    }

    // 2. 生成视图数据

    // 无论是否重读了文件，都根据全局的查看日期刷新一下分桶数据
    refresh_daily_view_data(s, g_view_daily_ts);

    // 无论是否重读文件，都根据 view_year/view_month 从 map 中提取数据
    s.month_year = view_year;
    s.month_month = view_month;

    int vdays = days_in_month(view_year, view_month);
    s.month_day_seconds.assign(vdays, 0);

    // 构造该月每一天的时间戳，去 Map 里查
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = view_year - 1900;
    tmv.tm_mon = view_month - 1;
    tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;

    for (int d = 1; d <= vdays; d++) {
        tmv.tm_mday = d;
        time_t day_ts = mktime(&tmv); // 获取该日0点时间戳
        
        // 如果 Map 里有记录，就填入 vector
        if (s.history_map.count(day_ts)) {
            s.month_day_seconds[d - 1] = s.history_map[day_ts];
        }
    }
}
