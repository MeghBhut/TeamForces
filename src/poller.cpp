#include "poller.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <thread>
#include <unordered_map>
#include <vector>

#include "contest.h"

namespace poller {

std::map<std::string, ProblemResult> evaluate(const std::set<std::string>& problem_keys,
                                              long long contest_start,
                                              long long contest_end,
                                              const std::vector<cf::Submission>& subs) {

    std::map<std::string, ProblemResult> results;
    for (const auto& k : problem_keys) results[k] = ProblemResult{};

    std::vector<cf::Submission> ordered = subs;
    std::sort(ordered.begin(), ordered.end(),
              [](const cf::Submission& a, const cf::Submission& b) {
                  return a.creationTimeSeconds < b.creationTimeSeconds;
              });

    for (const auto& s : ordered) {

        auto it = results.find(s.key());
        if (it == results.end()) continue;

        if (s.creationTimeSeconds < contest_start) continue;

        ProblemResult& r = it->second;

        if (r.solved) continue;

        if (s.verdict == "OK") {
            r.solved = true;

            long long secs_from_start = s.creationTimeSeconds - contest_start;
            r.solved_at_min = static_cast<int>(secs_from_start / 60);

            r.upsolved = (s.creationTimeSeconds >= contest_end);
        } else if (!s.verdict.empty() && s.verdict != "TESTING" &&
                   s.verdict != "COMPILATION_ERROR") {

            r.wrong_attempts += 1;
        }
    }

    {
        long long prev = contest_start;
        std::map<std::string, long long> effort_secs;
        std::set<std::string> solved_here;
        for (const auto& s : ordered) {
            if (s.creationTimeSeconds < contest_start) continue;
            if (s.creationTimeSeconds > contest_end) break;
            if (results.find(s.key()) == results.end()) continue;

            long long gap = s.creationTimeSeconds - prev;
            prev = s.creationTimeSeconds;
            if (solved_here.count(s.key())) continue;
            effort_secs[s.key()] += gap;
            if (s.verdict == "OK") solved_here.insert(s.key());
        }
        for (auto& kv : results) {
            ProblemResult& r = kv.second;

            if (r.solved && !r.upsolved && effort_secs.count(kv.first))
                r.effort_min = static_cast<int>(effort_secs[kv.first] / 60);
        }
    }

    return results;
}

using json = nlohmann::json;

static json result_to_json(const ProblemResult& r) {
    return {{"solved", r.solved},
            {"solved_at", r.solved_at_min},
            {"wrong_attempts", r.wrong_attempts},
            {"upsolved", r.upsolved},
            {"effort_min", r.effort_min}};
}

struct LiveSnapshot {
    bool found = false;
    std::string id;
    long long start = 0, end = 0;
    std::set<std::string> keys;
    std::vector<std::pair<std::string, std::string>> members;
};

static LiveSnapshot snapshot_live(storage::Store& store) {
    LiveSnapshot s;
    std::lock_guard<std::mutex> lock(store.mtx);
    for (const auto& c : store.contests) {
        if (c.value("status", "") != "live") continue;
        s.found = true;
        s.id = c.value("id", "");
        s.start = c.value("start", 0LL);
        s.end = contest::end_unix(c);
        for (const auto& p : c.value("problems", json::array()))
            s.keys.insert(std::to_string(p.value("contestId", 0LL)) + p.value("index", ""));
        break;
    }
    if (!s.found) return s;

    for (const auto& u : store.users)
        s.members.push_back({u.value("username", ""), u.value("cf_handle", "")});
    return s;
}

static bool poll_live_once(storage::Store& store, cf::Client& cf) {
    LiveSnapshot snap = snapshot_live(store);
    if (!snap.found) return false;

    for (const auto& m : snap.members) {
        const std::string& user = m.first;
        const std::string& handle = m.second;
        if (handle.empty()) continue;

        std::string err;
        auto subs = cf.fetch_user_status(handle, &err);

        if (!err.empty()) continue;
        auto results = evaluate(snap.keys, snap.start, snap.end, subs);

        std::lock_guard<std::mutex> lock(store.mtx);

        for (auto& c : store.contests) {
            if (c.value("id", "") != snap.id) continue;
            json rmap = json::object();
            for (const auto& kv : results) rmap[kv.first] = result_to_json(kv.second);
            c["results"][user] = rmap;
            break;
        }
    }

    std::lock_guard<std::mutex> lock(store.mtx);
    store.save_contests();
    return true;
}

static void finalize_if_ended(storage::Store& store) {
    std::lock_guard<std::mutex> lock(store.mtx);
    long long now = (long long)std::time(nullptr);

    for (auto& c : store.contests) {
        if (c.value("status", "") != "live") continue;
        if (now < contest::end_unix(c)) continue;

        c["status"] = "finished";

        std::vector<std::string> keys;
        for (const auto& p : c.value("problems", json::array()))
            keys.push_back(std::to_string(p.value("contestId", 0LL)) + p.value("index", ""));

        json queue = json::object();
        for (const auto& u : store.users) {
            std::string user = u.value("username", "");
            json unsolved = json::array();
            for (const auto& key : keys) {
                bool solved = false;
                if (c.contains("results") && c["results"].contains(user) &&
                    c["results"][user].contains(key))
                    solved = c["results"][user][key].value("solved", false);
                if (!solved) unsolved.push_back(key);
            }
            if (!unsolved.empty()) queue[user] = unsolved;
        }
        c["upsolve_queue"] = queue;
        store.save_contests();
    }
}

static void poll_upsolve_once(storage::Store& store, cf::Client& cf) {

    struct Work { std::string contest_id, user, handle; long long start, end;
                  std::set<std::string> keys; };
    std::vector<Work> work;
    std::unordered_map<std::string, std::string> user_handle;
    {
        std::lock_guard<std::mutex> lock(store.mtx);
        for (const auto& u : store.users)
            user_handle[u.value("username", "")] = u.value("cf_handle", "");

        for (const auto& c : store.contests) {
            if (c.value("status", "") != "finished") continue;
            if (!c.contains("upsolve_queue")) continue;
            for (auto it = c["upsolve_queue"].begin(); it != c["upsolve_queue"].end(); ++it) {
                std::string user = it.key();
                std::string handle = user_handle.count(user) ? user_handle[user] : "";
                if (handle.empty() || it.value().empty()) continue;
                Work w;
                w.contest_id = c.value("id", "");
                w.user = user;
                w.handle = handle;
                w.start = c.value("start", 0LL);
                w.end = contest::end_unix(c);
                for (const auto& k : it.value()) w.keys.insert(k.get<std::string>());
                work.push_back(std::move(w));
            }
        }
    }
    if (work.empty()) return;

    std::unordered_map<std::string, std::vector<cf::Submission>> subs_cache;
    for (const auto& w : work) {
        if (subs_cache.count(w.handle)) continue;
        std::string err;
        subs_cache[w.handle] = cf.fetch_user_status(w.handle, &err);
    }

    std::lock_guard<std::mutex> lock(store.mtx);
    bool changed = false;
    for (const auto& w : work) {
        auto results = evaluate(w.keys, w.start, w.end, subs_cache[w.handle]);
        for (auto& c : store.contests) {
            if (c.value("id", "") != w.contest_id) continue;
            for (const auto& kv : results) {
                if (!kv.second.solved) continue;

                c["results"][w.user][kv.first] = result_to_json(kv.second);

                auto& q = c["upsolve_queue"][w.user];
                for (auto qi = q.begin(); qi != q.end(); ++qi) {
                    if (qi->get<std::string>() == kv.first) { q.erase(qi); break; }
                }
                changed = true;
            }
            break;
        }
    }
    if (changed) store.save_contests();
}

void recompute_all(storage::Store& store, cf::Client& cf) {

    struct C { std::string id, status; long long start, end; std::set<std::string> keys; };
    std::vector<C> contests;
    std::vector<std::pair<std::string, std::string>> members;
    {
        std::lock_guard<std::mutex> lock(store.mtx);
        for (const auto& u : store.users)
            members.push_back({u.value("username", ""), u.value("cf_handle", "")});
        for (const auto& c : store.contests) {
            std::string s = c.value("status", "");
            if (s == "draft") continue;
            C cc; cc.id = c.value("id", ""); cc.status = s;
            cc.start = c.value("start", 0LL); cc.end = contest::end_unix(c);
            for (const auto& p : c.value("problems", json::array()))
                cc.keys.insert(std::to_string(p.value("contestId", 0LL)) + p.value("index", ""));
            contests.push_back(std::move(cc));
        }
    }

    std::unordered_map<std::string, std::vector<cf::Submission>> subs;
    for (const auto& m : members) {
        if (m.second.empty() || subs.count(m.second)) continue;
        std::string err;
        subs[m.second] = cf.fetch_user_status(m.second, &err);
    }

    std::lock_guard<std::mutex> lock(store.mtx);
    for (const auto& cc : contests) {
        for (auto& c : store.contests) {
            if (c.value("id", "") != cc.id) continue;
            for (const auto& m : members) {
                if (m.second.empty()) continue;
                auto results = evaluate(cc.keys, cc.start, cc.end, subs[m.second]);
                json rmap = json::object();
                for (const auto& kv : results) rmap[kv.first] = result_to_json(kv.second);
                c["results"][m.first] = rmap;

                if (cc.status == "finished") {
                    json unsolved = json::array();
                    for (const auto& key : cc.keys)
                        if (!results[key].solved) unsolved.push_back(key);
                    if (!unsolved.empty()) c["upsolve_queue"][m.first] = unsolved;
                    else if (c["upsolve_queue"].contains(m.first))
                        c["upsolve_queue"].erase(m.first);
                }
            }
            break;
        }
    }
    store.save_contests();
}

void run_loop(storage::Store& store, cf::Client& cf, const std::atomic<bool>& stop) {
    long long last_upsolve = 0;
    while (!stop.load()) {
        long long now = (long long)std::time(nullptr);

        contest::auto_start_due(store, now);

        if (poll_live_once(store, cf)) {
            finalize_if_ended(store);

            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }

        if (now - last_upsolve >= 300) {
            poll_upsolve_once(store, cf);
            last_upsolve = now;
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

}
