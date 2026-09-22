#pragma once
#include <mutex>
#include <string>
#include "json.hpp"

namespace storage {

using json = nlohmann::json;

json read_json_file(const std::string& path, const json& fallback);

bool write_json_file(const std::string& path, const json& j);

struct Store {
    std::string dir;

    json users;
    json contests;
    json logs;
    json bookmarks;
    json log_skips;
    json problemset_cache;
    json sessions;

    std::mutex mtx;

    void load(const std::string& data_dir);

    void save_users();
    void save_contests();
    void save_logs();
    void save_bookmarks();
    void save_log_skips();
    void save_problemset_cache();
    void save_sessions();
};

}
