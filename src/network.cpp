#include "network.hpp"
#include "types.hpp"
#include "utils.hpp"
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdint>

#define MY_USER_AGENT "Mozilla/5.0 (Linux; Android 4.4.2; Kindle Fire Build/KOT49H) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/30.0.0.0 Safari/537.36"

// 辅助函数：解码 JSON 中的义序列
std::string unescape_unicode(const std::string& input) {
    std::string output;
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '\\' && i + 5 < input.length() && input[i + 1] == 'u') {
            // 提取 4 位十六进制数
            std::string hex = input.substr(i + 2, 4);
            std::istringstream iss(hex);
            uint32_t cp;
            iss >> std::hex >> cp;

            // 将 Unicode 码点转换为 UTF-8 编码
            if (cp <= 0x7F) {
                output += (char)cp;
            } else if (cp <= 0x7FF) {
                output += (char)(0xC0 | (cp >> 6));
                output += (char)(0x80 | (cp & 0x3F));
            } else if (cp <= 0xFFFF) {
                output += (char)(0xE0 | (cp >> 12));
                output += (char)(0x80 | ((cp >> 6) & 0x3F));
                output += (char)(0x80 | (cp & 0x3F));
            }
            i += 5; 
        } else {
            output += input[i];
        }
    }
    return output;
}

// --- 持久化存储实现 ---

// 保存状态：sync_time, nickname, device_name
void KykkyNetwork::save_state() {
    std::ofstream outfile(STATE_FILE);
    if (outfile.good()) {
        outfile << "last_sync=" << last_sync_time << "\n";
        outfile << "nickname=" << current_user.nickname << "\n";
        outfile << "devicename=" << current_user.device_name << "\n";
    }
}

// 加载状态
void KykkyNetwork::load_state() {
    std::ifstream infile(STATE_FILE);
    std::string line;
    while (std::getline(infile, line)) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (!val.empty() && val.back() == '\r') val.pop_back(); // 去除 Windows 换行

            if (key == "last_sync") last_sync_time = std::atol(val.c_str());
            else if (key == "nickname") current_user.nickname = val;
            else if (key == "devicename") current_user.device_name = val;
        }
    }
}

// --- 实现 set_last_sync_time ---

void KykkyNetwork::set_last_sync_time(time_t t) {
    last_sync_time = t;
    save_state(); // 保存到文件
}

time_t KykkyNetwork::get_last_sync_time() const {
    return last_sync_time;
}

// --- 实现 fetch_user_profile ---

bool KykkyNetwork::fetch_user_profile() {
    if (this->device_code.empty()) return false;

    // 利用 check_status 接口获取信息
    std::string url = "https://" + domain + "/api/auth.php?action=check_status&device_code=" + this->device_code;
    
    std::cout << "[Network] Fetching profile: " << url << std::endl;
    std::string json = http_get(url);

    if (json.empty()) return false;

    std::string status = extract_json_value(json, "status");
    if (status == "success") {
        std::string nick = unescape_unicode(extract_json_value(json, "nickname"));
        std::string dname = unescape_unicode(extract_json_value(json, "device_name"));
        
        if (!nick.empty()) current_user.nickname = nick;
        if (!dname.empty()) current_user.device_name = dname;
        
        current_user.is_logged_in = true;
        save_state(); // 获取成功后立即保存
        return true;
    }
    return false;
}

// Curl 回调
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

KykkyNetwork& KykkyNetwork::instance() {
    static KykkyNetwork inst;
    return inst;
}

void KykkyNetwork::init() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (g_share_domain.empty()) {
        g_share_domain = "reading.tqhyg.net";
    }
    if (g_share_domain.find("://") != std::string::npos) {
        g_share_domain = g_share_domain.substr(g_share_domain.find("://") + 3);
    }
    this->domain = g_share_domain;
    
    load_token(); // 加载认证信息
    load_state(); // 加载昵称、同步时间等缓存
}

// 简易 JSON 提取，不依赖大库
std::string KykkyNetwork::extract_json_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.length();

    // 跳过空白
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':')) pos++;

    char quote = json[pos];
    if (quote == '"') {
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    } else {
        // 数字或布尔值
        size_t end = pos;
        while (end < json.length() && (json[end] != ',' && json[end] != '}')) end++;
        return json.substr(pos, end - pos);
    }
}

std::string KykkyNetwork::http_get(const std::string& url) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;
    last_error_ = "";

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, MY_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kykky-Kindle-Client/1.0");

        // 带上认证 header
        struct curl_slist *headers = NULL;
        if (!access_token.empty()) {
            std::string auth = "Authorization: Bearer " + access_token;
            headers = curl_slist_append(headers, auth.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            last_error_ = curl_easy_strerror(res);
            return "";
        }
    }
    return readBuffer;
}

std::string KykkyNetwork::http_post(const std::string& url, const std::string& data) {
    // 仅用于简单 POST
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, MY_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // Header needed for auth
        struct curl_slist *headers = NULL;
        if (!access_token.empty()) {
            std::string auth = "Authorization: Bearer " + access_token;
            headers = curl_slist_append(headers, auth.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

// 复杂上传
std::string KykkyNetwork::http_post_files(const std::string& url, const std::vector<std::string>& file_paths, long today, long month) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;

    // 添加文件
    for (const auto& path : file_paths) {
        // 检查文件是否存在且不为空
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && st.st_size > 0) {
            curl_formadd(&formpost, &lastptr,
                         CURLFORM_COPYNAME, "logs[]",
                         CURLFORM_FILE, path.c_str(),
                         CURLFORM_END);
        }
    }

    curl = curl_easy_init();
    if(curl) {
        std::string full_url = url + "?today_seconds=" + std::to_string(today) + "&month_seconds=" + std::to_string(month);

        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, MY_USER_AGENT);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // 上传超时

        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Expect:"); // 禁用 Expect: 100-continue
        if (!access_token.empty()) {
            std::string auth = "Authorization: Bearer " + access_token;
            headers = curl_slist_append(headers, auth.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_formfree(formpost);

        if (res != CURLE_OK) return "Network Error: " + std::string(curl_easy_strerror(res));
    } else {
        return "Curl init failed";
    }

    // 检查服务端返回
    std::string status = extract_json_value(readBuffer, "status");
    if (status == "success") return ""; // 成功

    std::string msg = extract_json_value(readBuffer, "msg");
    return msg.empty() ? "Unknown server error" : msg;
}

bool KykkyNetwork::check_internet() {
    CURL *curl;
    CURLcode res;
    last_error_ = "";

    curl = curl_easy_init();
    if(curl) {
        std::string url = "http://" + domain + "/style.css";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD 请求，更快
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kykky-Kindle-Client/1.0");

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            last_error_ = curl_easy_strerror(res);
        }
        return (res == CURLE_OK);
    }
    return false;
}

void KykkyNetwork::ensure_wifi_on() {
    // Kindle 命令开启 WiFi
    system("lipc-set-prop com.lab126.cmd wirelessEnable 1");
    // 等待一会
    sleep(2);
}

// —— Token 管理 ——

const std::string TOKEN_FILE_PATH = BASE_DIR + "etc/token";

bool KykkyNetwork::load_token() {
    std::ifstream infile(TOKEN_FILE_PATH);
    if (infile.good()) {
        std::getline(infile, device_code);
        std::getline(infile, access_token);

        // 去除可能的换行符
        if (!device_code.empty() && device_code.back() == '\r') device_code.pop_back();
        if (!access_token.empty() && access_token.back() == '\r') access_token.pop_back();

        if (!access_token.empty()) {
            // 有 Token 就算登陆，昵称等信息由 load_state() 从缓存加载
            // 不在初始化时做网络请求，避免阻塞启动
            current_user.is_logged_in = true;
            return true;
        }
    }
    current_user.is_logged_in = false;
    return false;
}

// 辅助私有函数：统一清理本地文件和内存
void KykkyNetwork::clear_local_auth() {
    access_token = "";
    current_user.is_logged_in = false;
    current_user.nickname = "";
    current_user.device_name = "";
    
    unlink(TOKEN_FILE_PATH.c_str());
    std::string state_path = BASE_DIR + "etc/state";
    unlink(state_path.c_str());
}


bool KykkyNetwork::logout() {
    // 1. 本地状态预检查
    if (access_token.empty()) {
        clear_local_auth();
        return true;
    }

    // 2. 必须有网才能调接口注销
    if (!check_internet()) return false;

    // 3. 构造请求 (带上 device_code 以便服务端识别设备)
    std::string url = "https://" + domain + "/api/auth.php?action=logout&device_code=" + this->device_code;
    
    std::string resp = http_get(url);
    std::string status = extract_json_value(resp, "status");

    // 4. 处理结果
    if (status == "success" || resp.find("invalid") != std::string::npos) {
        clear_local_auth();
        return true;
    }
    return false;
}

void save_token_file(const std::string& dc, const std::string& at) {
    std::ofstream outfile(TOKEN_FILE_PATH);
    if (outfile.good()) {
        outfile << dc << "\n";
        outfile << at << "\n";
    }
}

// —— 业务逻辑 ——

std::pair<std::string, std::string> KykkyNetwork::fetch_login_qr() {
    std::string url = "https://" + domain + "/api/auth.php?action=get_url";
    std::cout << "[Network] QR Req: " << url << std::endl; // 调试输出
    
    std::string json = http_get(url);
    std::cout << "[Network] QR Resp: " << json << std::endl; // 调试输出，看服务端返回了什么

    if (json.empty()) {
        std::cerr << "[Network Error] Empty response from server" << std::endl;
        return {"", ""};
    }

    std::string dc = extract_json_value(json, "device_code");
    std::string lurl = extract_json_value(json, "login_url");
    
    // 调试解析结果
    if (dc.empty() || lurl.empty()) {
        std::cerr << "[Network Error] JSON parse failed. dc=" << dc << " url=" << lurl << std::endl;
        return {"", ""};
    }

    // 解决 JSON 中的转义斜杠问题 (https:\/\/...)
    size_t pos = 0;
    while((pos = lurl.find("\\/", pos)) != std::string::npos) {
        lurl.replace(pos, 2, "/");
        pos += 1;
    }

    if (!dc.empty()) {
        this->device_code = dc;
        // 保存 device_code 到 token 文件
        save_token_file(dc, "");
    }
    
    return {lurl, dc};
}

std::string KykkyNetwork::poll_login_status(const std::string& d_code) {
    std::string url = "https://" + domain + "/api/auth.php?action=check_status&device_code=" + d_code;
    std::string json = http_get(url);
    
    std::string status = extract_json_value(json, "status");
    if (status == "success") {
        std::string token = extract_json_value(json, "access_token");
        if (!token.empty()) {
            this->access_token = token;
            
            // 提取用户信息
            std::string nick = extract_json_value(json, "nickname");
            std::string dname = extract_json_value(json, "device_name");
            
            this->current_user.nickname = unescape_unicode(extract_json_value(json, "nickname"));
            this->current_user.device_name = unescape_unicode(extract_json_value(json, "device_name"));
            this->current_user.is_logged_in = true;
            
            save_token_file(this->device_code, this->access_token);
            save_state(); // 保存用户信息
            
            return token;
        }
    }
    return "";
}


std::string KykkyNetwork::upload_data(long today, long month) {
    // ---上传前的身份有效性预检 ---
    if (!this->access_token.empty()) {
        std::string check_url = "https://" + domain + "/api/auth.php?action=check_status&device_code=" + this->device_code;
        std::string resp = http_get(check_url);
        
        // 如果服务端返回 expired，说明设备已在后台被删
        if (resp.find("expired") != std::string::npos) {
            std::cout << "[Network] Device expired, skipping upload and clearing local auth." << std::endl;
            clear_local_auth(); // 清除本地 state 和 token
            refresh_all_status_labels(); // 刷新 UI 小字
            return "device_expired"; 
        }
    }

    std::string url = "https://" + domain + "/api/upload.php";
    std::vector<std::string> files;

    // 1. metrics_reader_xxxx (当前)
    DIR *d = opendir(LOG_DIR.c_str());
    if (d) {
        struct dirent *p;
        while ((p = readdir(d))) {
            if (strncmp(p->d_name, LOG_PREFIX, strlen(LOG_PREFIX)) == 0) {
                files.push_back(LOG_DIR + p->d_name);
            }
        }
        closedir(d);
    }

    // 2. history.gz
    if (access(ARCHIVE_FILE.c_str(), F_OK) != -1) {
        files.push_back(ARCHIVE_FILE);
    }

    std::string err = http_post_files(url, files, today, month);
    if (err.empty()) {
        set_last_sync_time(time(NULL));
    }
    return err;
}


std::string KykkyNetwork::get_last_sync_text() const {
    if (last_sync_time == 0) return "未同步过";
    time_t now = time(NULL);
    double diff = difftime(now, last_sync_time);

    if (diff < 60) return "刚刚";
    if (diff < 3600) return std::to_string((int)(diff/60)) + "分钟前";
    if (diff < 86400) return std::to_string((int)(diff/3600)) + "小时前";
    return std::to_string((int)(diff/86400)) + "天前";
}
