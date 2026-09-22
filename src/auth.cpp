#include "auth.h"

#include <chrono>
#include <random>

#include "sha256.h"

namespace auth {

std::string random_hex(int bytes) {

    static std::mt19937_64 rng([] {
        std::random_device rd;
        auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::seed_seq seq{static_cast<uint64_t>(rd()),
                          static_cast<uint64_t>(rd()),
                          static_cast<uint64_t>(t)};
        return std::mt19937_64(seq);
    }());
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);

    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (int i = 0; i < bytes; ++i) {
        unsigned char b = static_cast<unsigned char>(rng() & 0xff);
        out.push_back(hexd[b >> 4]);
        out.push_back(hexd[b & 0xf]);
    }
    return out;
}

std::string hash_password(const std::string& salt, const std::string& password) {

    return sha256_hex(salt + password);
}

bool register_user(storage::Store& store, const std::string& username,
                   const std::string& password, const std::string& cf_handle,
                   const std::string& role, std::string* err) {
    if (username.empty() || password.empty()) {
        if (err) *err = "username and password are required";
        return false;
    }
    if (role != "admin" && role != "member") {
        if (err) *err = "role must be 'admin' or 'member'";
        return false;
    }

    std::lock_guard<std::mutex> lock(store.mtx);

    for (const auto& u : store.users) {
        if (u.value("username", "") == username) {
            if (err) *err = "username already exists";
            return false;
        }
    }

    std::string salt = random_hex(16);
    json user = {
        {"username",  username},
        {"pass_hash", hash_password(salt, password)},
        {"salt",      salt},
        {"cf_handle", cf_handle},
        {"role",      role},
    };
    store.users.push_back(user);
    store.save_users();
    return true;
}

bool check_login(storage::Store& store, const std::string& username,
                 const std::string& password) {
    std::lock_guard<std::mutex> lock(store.mtx);
    for (const auto& u : store.users) {
        if (u.value("username", "") == username) {
            std::string salt = u.value("salt", "");
            std::string want = u.value("pass_hash", "");
            return !want.empty() && hash_password(salt, password) == want;
        }
    }
    return false;
}

std::string create_session(storage::Store& store, const std::string& username) {
    std::string token = random_hex(24);
    std::lock_guard<std::mutex> lock(store.mtx);
    store.sessions[token] = username;
    store.save_sessions();
    return token;
}

void destroy_session(storage::Store& store, const std::string& token) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(store.mtx);
    if (store.sessions.contains(token)) {
        store.sessions.erase(token);
        store.save_sessions();
    }
}

std::string token_from_cookie(const std::string& cookie_header) {

    const std::string key = "session=";
    size_t pos = cookie_header.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    size_t end = cookie_header.find(';', pos);
    return cookie_header.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

std::string user_for_token(storage::Store& store, const std::string& token) {
    if (token.empty()) return "";
    std::lock_guard<std::mutex> lock(store.mtx);
    auto it = store.sessions.find(token);
    if (it == store.sessions.end()) return "";
    return it->get<std::string>();
}

json find_user(storage::Store& store, const std::string& username) {
    std::lock_guard<std::mutex> lock(store.mtx);
    for (const auto& u : store.users)
        if (u.value("username", "") == username) return u;
    return json(nullptr);
}

bool is_admin(storage::Store& store, const std::string& username) {
    json u = find_user(store, username);
    return u.is_object() && u.value("role", "") == "admin";
}

}
