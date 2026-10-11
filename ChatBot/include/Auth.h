#pragma once
#include "common.h"
#include <fstream>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

class Auth {
public:
    static Auth& instance() {
        static Auth inst;
        return inst;
    }

    bool loadJwks(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open()) {
            std::cerr << "[Auth] 无法打开 " << filepath << std::endl;
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        try {
            auto jwks = json::parse(content);
            if (!jwks.contains("keys") || jwks["keys"].empty()) {
                std::cerr << "[Auth] JWKS 缺少 keys" << std::endl;
                return false;
            }
            auto& key = jwks["keys"][0];
            std::string kty = key.value("kty", "");
            std::string crv = key.value("crv", "");
            if (kty != "EC" || crv != "P-256") {
                std::cerr << "[Auth] 仅支持 EC P-256，当前 kty=" << kty
                          << " crv=" << crv << std::endl;
                return false;
            }
            std::string x_b64 = key["x"];
            std::string y_b64 = key["y"];

            std::string x_bytes = base64url_decode(x_b64);
            std::string y_bytes = base64url_decode(y_b64);
            if (x_bytes.size() != 32 || y_bytes.size() != 32) {
                std::cerr << "[Auth] 公钥长度异常: x=" << x_bytes.size()
                          << " y=" << y_bytes.size() << std::endl;
                return false;
            }

            EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            if (!ec) return false;
            BIGNUM* bn_x = BN_bin2bn((const unsigned char*)x_bytes.data(), 32, nullptr);
            BIGNUM* bn_y = BN_bin2bn((const unsigned char*)y_bytes.data(), 32, nullptr);
            if (!EC_KEY_set_public_key_affine_coordinates(ec, bn_x, bn_y)) {
                BN_free(bn_x); BN_free(bn_y); EC_KEY_free(ec);
                std::cerr << "[Auth] 设置公钥失败" << std::endl;
                return false;
            }
            BN_free(bn_x); BN_free(bn_y);

            if (pubkey_) EC_KEY_free(pubkey_);
            pubkey_ = ec;
            std::cout << "[Auth] 已加载 JWKS 公钥 (kid="
                      << key.value("kid","") << ")" << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[Auth] JWKS 解析失败: " << e.what() << std::endl;
            return false;
        }
    }

    std::string verifyToken(const std::string& auth_header) {
        if (auth_header.size() < 8 || auth_header.substr(0, 7) != "Bearer ")
            return "";
        std::string token = auth_header.substr(7);
        if (token.empty() || !pubkey_) return "";

        // 缓存
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(token);
            if (it != cache_.end() && it->second.expire > std::time(nullptr))
                return it->second.user_id;
        }

        size_t d1 = token.find('.');
        size_t d2 = token.find('.', d1 + 1);
        if (d1 == std::string::npos || d2 == std::string::npos) return "";

        std::string header_b64 = token.substr(0, d1);
        std::string payload_b64 = token.substr(d1 + 1, d2 - d1 - 1);
        std::string sig_b64 = token.substr(d2 + 1);

        // 检查 header.alg
        try {
            std::string header_json = base64url_decode(header_b64);
            auto hdr = json::parse(header_json);
            std::string alg = hdr.value("alg", "");
            if (alg != "ES256") {
                std::cerr << "[Auth] 不支持的算法: " << alg << std::endl;
                return "";
            }
        } catch (...) { return ""; }

        // 验签
        std::string signing_input = header_b64 + "." + payload_b64;
        std::string sig_raw = base64url_decode(sig_b64);
        if (sig_raw.size() != 64) {
            std::cerr << "[Auth] 签名长度不是 64 字节，实际 " << sig_raw.size() << std::endl;
            return "";
        }

        unsigned char hash[32];
        SHA256((const unsigned char*)signing_input.data(), signing_input.size(), hash);

        // 将 JWS 的 r||s 原始格式转成 OpenSSL 需要的 DER
        BIGNUM* r = BN_bin2bn((const unsigned char*)sig_raw.data(), 32, nullptr);
        BIGNUM* s = BN_bin2bn((const unsigned char*)sig_raw.data() + 32, 32, nullptr);
        ECDSA_SIG* sig = ECDSA_SIG_new();
        ECDSA_SIG_set0(sig, r, s);  // 所有权转移到 sig

        unsigned char* der = nullptr;
        int der_len = i2d_ECDSA_SIG(sig, &der);

        int ok = ECDSA_verify(0, hash, 32, der, der_len, pubkey_);

        OPENSSL_free(der);
        ECDSA_SIG_free(sig);

        if (ok != 1) {
            std::cerr << "[Auth] 签名验证失败" << std::endl;
            return "";
        }

        // 解析 payload
        try {
            std::string payload_json = base64url_decode(payload_b64);
            auto j = json::parse(payload_json);

            if (j.contains("exp")) {
                int64_t exp = j["exp"].get<int64_t>();
                if ((int64_t)std::time(nullptr) > exp) {
                    std::cerr << "[Auth] token 已过期" << std::endl;
                    return "";
                }
            }
            if (!j.contains("sub")) return "";
            std::string uid = j["sub"].get<std::string>();

            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                cache_[token] = { uid, std::time(nullptr) + 300 };
            }
            return uid;
        } catch (const std::exception& e) {
            std::cerr << "[Auth] payload 解析失败: " << e.what() << std::endl;
            return "";
        }
    }

private:
    Auth() = default;
    EC_KEY* pubkey_ = nullptr;

    struct CacheEntry { std::string user_id; std::time_t expire; };
    std::map<std::string, CacheEntry> cache_;
    std::mutex cache_mutex_;

    static std::string base64url_decode(const std::string& input) {
        std::string b64 = input;
        std::replace(b64.begin(), b64.end(), '-', '+');
        std::replace(b64.begin(), b64.end(), '_', '/');
        while (b64.size() % 4 != 0) b64 += '=';
        BIO* bio = BIO_new_mem_buf(b64.data(), (int)b64.size());
        BIO* b64f = BIO_new(BIO_f_base64());
        BIO_set_flags(b64f, BIO_FLAGS_BASE64_NO_NL);
        bio = BIO_push(b64f, bio);
        std::string result;
        result.resize(b64.size());
        int n = BIO_read(bio, &result[0], (int)b64.size());
        BIO_free_all(bio);
        if (n <= 0) return "";
        result.resize(n);
        return result;
    }
};