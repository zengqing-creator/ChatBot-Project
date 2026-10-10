#pragma once
#include "common.h"

class Auth {
public:
    static Auth& instance() {
        static Auth inst;
        return inst;
    }

    void init(const std::string& supabase_url, const std::string& anon_key) {
        supabase_url_ = supabase_url;
        anon_key_ = anon_key;
    }

    // 验证 token，返回 user_id，失败返回空
    std::string verifyToken(const std::string& auth_header) {
        if (auth_header.size() < 8 || auth_header.substr(0, 7) != "Bearer ")
            return "";
        std::string token = auth_header.substr(7);
        if (token.empty()) return "";

        // 缓存命中
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(token);
            if (it != cache_.end() && it->second.expire > std::time(nullptr))
                return it->second.user_id;
        }

        // 调 Supabase 验证
        try {
            httplib::Client cli(supabase_url_);
            cli.enable_server_certificate_verification(false);
            cli.set_read_timeout(10, 0);

            httplib::Headers headers = {
                {"Authorization", "Bearer " + token},
                {"apikey", anon_key_}
            };
            auto res = cli.Get("/auth/v1/user", headers);
            if (!res || res->status != 200) {
                std::cerr << "[Auth] Supabase 拒绝 token, status="
                          << (res ? res->status : 0) << std::endl;
                return "";
            }
            auto j = json::parse(res->body);
            if (!j.contains("id")) return "";
            std::string uid = j["id"].get<std::string>();

            // 缓存 5 分钟
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                cache_[token] = { uid, std::time(nullptr) + 300 };
            }
            return uid;
        } catch (const std::exception& e) {
            std::cerr << "[Auth] 验证异常: " << e.what() << std::endl;
            return "";
        }
    }

private:
    Auth() = default;
    std::string supabase_url_;
    std::string anon_key_;

    struct CacheEntry { std::string user_id; std::time_t expire; };
    std::map<std::string, CacheEntry> cache_;
    std::mutex cache_mutex_;
};