#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <string>
#include <vector>
#include <functional>

struct UserInfo {
    std::string nickname;
    std::string device_name;
    std::string avatar_url;
    bool is_logged_in;
};

class KykkyNetwork {
public:
    static KykkyNetwork& instance();

    // 初始化（读取配置域名等）
    void init();

    // 网络控制
    bool check_internet();
    void ensure_wifi_on();

    // 认证相关
    bool load_token(); // 从文件加载
    bool logout();
    void clear_local_auth();
    std::string get_access_token() const { return access_token; }
    std::string get_device_code() const { return device_code; }
    UserInfo get_user_info() const { return current_user; }

    // API 请求
    // 返回 pair: <login_url, device_code>
    std::pair<std::string, std::string> fetch_login_qr();

    // 检查登陆状态，返回 access_token，空则未完成
    std::string poll_login_status(const std::string& d_code);

    // 登陆成功后获取用户信息
    bool fetch_user_profile();

    // 数据同步
    // 返回: 错误信息，为空则成功
    std::string upload_data(long today_sec, long month_sec);

    // 记录上次同步时间
    void set_last_sync_time(time_t t);
    time_t get_last_sync_time() const;
    std::string get_last_sync_text() const;

private:
    KykkyNetwork() {}

    void save_state();
    void load_state();
    std::string access_token;
    std::string device_code;
    std::string domain;
    UserInfo current_user;
    time_t last_sync_time = 0;

    // 简易 JSON 值提取
    std::string extract_json_value(const std::string& json, const std::string& key);
    // 基础 Curl 请求
    std::string http_get(const std::string& url);
    std::string http_post(const std::string& url, const std::string& data);
    std::string http_post_files(const std::string& url, const std::vector<std::string>& file_paths, long today, long month);
};

#endif
