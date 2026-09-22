#include "stats.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <set>

#include "contest.h"

namespace stats {

bool valid_tag(const std::string& tag) {
    return tag == "misread" || tag == "wrong-approach" || tag == "edge-case" ||
           tag == "slow-start" || tag == "other";
}

static void split_key(const std::string& key, long long& contestId, std::string& index) {
    size_t i = 0;
    while (i < key.size() && key[i] >= '0' && key[i] <= '9') ++i;
    contestId = i > 0 ? std::stoll(key.substr(0, i)) : 0;
    index = key.substr(i);
}

bool add_log(storage::Store& store, const std::string& username,
             const std::string& problem_key, const std::string& contest_id,
             const std::string& text, const std::string& tag, std::string* err) {
    if (problem_key.empty()) { if (err) *err = "pick a problem"; return false; }
    if (text.empty())        { if (err) *err = "write something"; return false; }
    if (!valid_tag(tag))     { if (err) *err = "invalid tag"; return false; }

    std::lock_guard<std::mutex> lock(store.mtx);
    store.logs.push_back({{"problem_key", problem_key},
                          {"contest_id", contest_id},
                          {"username", username},
                          {"text", text},
                          {"tag", tag},
                          {"timestamp", (long long)std::time(nullptr)}});
    store.save_logs();
    return true;
}

json get_logs(storage::Store& store, const std::string& filter_problem,
              const std::string& filter_user) {
    std::lock_guard<std::mutex> lock(store.mtx);

    json out = json::array();
    for (const auto& l : store.logs) {
        if (!filter_problem.empty() && l.value("problem_key", "") != filter_problem) continue;
        if (!filter_user.empty()    && l.value("username", "")    != filter_user) continue;
        out.push_back(l);
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
        return a.value("timestamp", 0LL) > b.value("timestamp", 0LL);
    });

    json problems = json::array();
    std::set<std::string> seen;
    for (const auto& c : store.contests) {
        if (c.value("status", "") == "draft") continue;
        for (const auto& p : c.value("problems", json::array())) {
            long long cid = p.value("contestId", 0LL);
            std::string ix = p.value("index", "");
            std::string key = std::to_string(cid) + ix;
            if (seen.count(key)) continue;
            seen.insert(key);
            problems.push_back({{"key", key}, {"index", ix}, {"name", p.value("name", "")},
                                {"contest_id", c.value("id", "")},
                                {"url", contest::problem_url(cid, ix)}});
        }
    }

    json members = json::array();
    for (const auto& u : store.users) members.push_back(u.value("username", ""));

    return {{"ok", true}, {"logs", out}, {"problems", problems}, {"members", members}};
}

json get_members(storage::Store& store) {
    std::lock_guard<std::mutex> lock(store.mtx);
    json members = json::array();
    for (const auto& u : store.users)
        members.push_back({{"username", u.value("username", "")},
                           {"cf_handle", u.value("cf_handle", "")},
                           {"role", u.value("role", "member")}});

    std::sort(members.begin(), members.end(), [](const json& a, const json& b) {
        return a.value("username", "") < b.value("username", "");
    });
    return {{"ok", true}, {"members", members}};
}

bool skip_log(storage::Store& store, const std::string& username, const std::string& key) {
    if (key.empty()) return false;
    std::lock_guard<std::mutex> lock(store.mtx);
    if (!store.log_skips.contains(username)) store.log_skips[username] = json::array();
    auto& list = store.log_skips[username];
    for (const auto& k : list) if (k.get<std::string>() == key) return true;
    list.push_back(key);
    store.save_log_skips();
    return true;
}
bool unskip_log(storage::Store& store, const std::string& username, const std::string& key) {
    std::lock_guard<std::mutex> lock(store.mtx);
    if (!store.log_skips.contains(username)) return true;
    auto& list = store.log_skips[username];
    for (auto it = list.begin(); it != list.end(); ++it)
        if (it->get<std::string>() == key) { list.erase(it); break; }
    store.save_log_skips();
    return true;
}

json pending_logs(storage::Store& store, const std::string& username) {
    std::lock_guard<std::mutex> lock(store.mtx);

    std::set<std::string> logged, skipped;
    for (const auto& l : store.logs)
        if (l.value("username", "") == username) logged.insert(l.value("problem_key", ""));
    if (store.log_skips.contains(username))
        for (const auto& k : store.log_skips[username]) skipped.insert(k.get<std::string>());

    json items = json::array();
    for (const auto& c : store.contests) {
        if (c.value("status", "") != "finished") continue;
        for (const auto& p : c.value("problems", json::array())) {
            long long cid = p.value("contestId", 0LL);
            std::string ix = p.value("index", "");
            std::string key = std::to_string(cid) + ix;
            if (logged.count(key) || skipped.count(key)) continue;

            json r = nullptr;
            if (c.contains("results") && c["results"].contains(username) &&
                c["results"][username].contains(key))
                r = c["results"][username][key];

            items.push_back({{"key", key}, {"index", ix}, {"name", p.value("name", "")},
                             {"contest_id", c.value("id", "")},
                             {"url", contest::problem_url(cid, ix)},
                             {"result", r}});
        }
    }
    return {{"ok", true}, {"items", items}};
}

bool add_bookmark(storage::Store& store, const std::string& username,
                  const std::string& problem_key) {
    if (problem_key.empty()) return false;
    std::lock_guard<std::mutex> lock(store.mtx);
    if (!store.bookmarks.contains(username)) store.bookmarks[username] = json::array();
    auto& list = store.bookmarks[username];
    for (const auto& k : list) if (k.get<std::string>() == problem_key) return true;
    list.push_back(problem_key);
    store.save_bookmarks();
    return true;
}

bool remove_bookmark(storage::Store& store, const std::string& username,
                     const std::string& problem_key) {
    std::lock_guard<std::mutex> lock(store.mtx);
    if (!store.bookmarks.contains(username)) return true;
    auto& list = store.bookmarks[username];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->get<std::string>() == problem_key) { list.erase(it); break; }
    }
    store.save_bookmarks();
    return true;
}

json get_bookmarks(storage::Store& store, const std::string& username) {
    std::lock_guard<std::mutex> lock(store.mtx);

    std::map<std::string, std::pair<std::string, std::string>> meta;
    for (const auto& c : store.contests) {
        if (c.value("status", "") == "draft") continue;
        for (const auto& p : c.value("problems", json::array())) {
            std::string key = std::to_string(p.value("contestId", 0LL)) + p.value("index", "");
            meta[key] = {p.value("index", ""), p.value("name", "")};
        }
    }

    json items = json::array();
    if (store.bookmarks.contains(username)) {
        for (const auto& kj : store.bookmarks[username]) {
            std::string key = kj.get<std::string>();
            long long cid; std::string ix;
            split_key(key, cid, ix);
            std::string index = meta.count(key) ? meta[key].first : ix;
            std::string name  = meta.count(key) ? meta[key].second : "";
            items.push_back({{"key", key}, {"index", index}, {"name", name},
                             {"url", contest::problem_url(cid, ix)}});
        }
    }
    return {{"ok", true}, {"items", items}};
}

json my_stats(storage::Store& store, const std::string& username) {
    std::lock_guard<std::mutex> lock(store.mtx);

    struct Band {
        int attempted = 0, solved = 0;
        long long time_sum = 0;   int time_n = 0;
        long long effort_sum = 0; int effort_n = 0;
    };
    std::map<int, Band> bands;
    json timeline = json::array();

    for (const auto& c : store.contests) {
        std::string status = c.value("status", "");
        if (status == "draft") continue;

        int t_solved = 0, t_total = 0; long long t_time_sum = 0; int t_time_n = 0;

        for (const auto& p : c.value("problems", json::array())) {
            int rating = p.value("rating", 0);
            std::string key = std::to_string(p.value("contestId", 0LL)) + p.value("index", "");
            ++t_total;

            json r;
            if (c.contains("results") && c["results"].contains(username) &&
                c["results"][username].contains(key))
                r = c["results"][username][key];

            bool solved = r.is_object() && r.value("solved", false);
            int wrong   = r.is_object() ? r.value("wrong_attempts", 0) : 0;
            bool upsolved = r.is_object() && r.value("upsolved", false);
            int effort  = r.is_object() ? r.value("effort_min", -1) : -1;
            bool attempted = solved || wrong > 0;

            if (rating > 0) {
                Band& b = bands[rating];
                if (attempted) ++b.attempted;
                if (solved) {
                    ++b.solved;
                    if (!upsolved) {
                        b.time_sum += r.value("solved_at", 0); ++b.time_n;
                        if (effort >= 0) { b.effort_sum += effort; ++b.effort_n; }
                    }
                }
            }
            if (solved) {
                ++t_solved;
                if (!upsolved) { t_time_sum += r.value("solved_at", 0); ++t_time_n; }
            }
        }

        timeline.push_back({{"contest_id", c.value("id", "")},
                            {"date", c.value("start", 0LL)},
                            {"solved", t_solved},
                            {"total", t_total},
                            {"avg_time_min", t_time_n ? (double)t_time_sum / t_time_n : 0.0}});
    }

    json bands_out = json::array();
    for (auto& kv : bands) {
        const Band& b = kv.second;
        double acc = b.attempted ? (100.0 * b.solved / b.attempted) : 0.0;
        double avg_elapsed = b.time_n ? (double)b.time_sum / b.time_n : 0.0;
        double avg_effort  = b.effort_n ? (double)b.effort_sum / b.effort_n : 0.0;
        bands_out.push_back({{"rating", kv.first}, {"attempted", b.attempted},
                             {"solved", b.solved}, {"accuracy", acc},
                             {"avg_time_min", avg_elapsed},
                             {"avg_effort_min", avg_effort}});
    }

    std::sort(timeline.begin(), timeline.end(), [](const json& a, const json& b) {
        return a.value("date", 0LL) < b.value("date", 0LL);
    });

    return {{"ok", true}, {"bands", bands_out}, {"timeline", timeline}};
}

}
