#include "storage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace storage {

json read_json_file(const std::string& path, const json& fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fallback;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.empty()) return fallback;
    try {
        return json::parse(text);
    } catch (const std::exception&) {

        return fallback;
    }
}

bool write_json_file(const std::string& path, const json& j) {

    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << j.dump(2);
        out.flush();
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {

        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
    return !ec;
}

void Store::load(const std::string& data_dir) {
    dir = data_dir;
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::lock_guard<std::mutex> lock(mtx);
    users            = read_json_file(dir + "/users.json",             json::array());
    contests         = read_json_file(dir + "/contests.json",          json::array());
    logs             = read_json_file(dir + "/logs.json",              json::array());
    bookmarks        = read_json_file(dir + "/bookmarks.json",         json::object());
    log_skips        = read_json_file(dir + "/log_skips.json",         json::object());
    sessions         = read_json_file(dir + "/sessions.json",          json::object());
    problemset_cache = read_json_file(dir + "/problemset_cache.json",
                                      json{{"updated_at", 0}, {"problems", json::array()}});
}

void Store::save_users()            { write_json_file(dir + "/users.json",             users); }
void Store::save_contests()         { write_json_file(dir + "/contests.json",          contests); }
void Store::save_logs()             { write_json_file(dir + "/logs.json",              logs); }
void Store::save_bookmarks()        { write_json_file(dir + "/bookmarks.json",         bookmarks); }
void Store::save_log_skips()        { write_json_file(dir + "/log_skips.json",         log_skips); }
void Store::save_problemset_cache() { write_json_file(dir + "/problemset_cache.json",  problemset_cache); }
void Store::save_sessions()         { write_json_file(dir + "/sessions.json",          sessions); }

}
