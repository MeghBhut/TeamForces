#include "contest.h"

#include <algorithm>
#include <ctime>
#include <random>
#include <set>
#include <unordered_map>

namespace contest {

long long end_unix(const json& c) {

    long long ended = c.value("ended_at", 0LL);
    if (ended > 0) return ended;

    if (!c.contains("start")) return 0;
    long long start = c.value("start", 0LL);
    long long dur   = c.value("duration_min", 0LL);

    long long paused = c.value("paused_secs", 0LL);
    return start + dur * 60 + paused;
}

bool is_live(const json& c, long long now) {
    if (c.value("status", "") != "live") return false;
    long long start = c.value("start", 0LL);
    return now >= start && now < end_unix(c);
}

std::string problem_url(long long contestId, const std::string& index) {
    return "https://codeforces.com/problemset/problem/" + std::to_string(contestId) +
           "/" + index;
}

static std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

static bool has_active_locked(storage::Store& store) {
    for (const auto& c : store.contests) {
        std::string s = c.value("status", "");
        if (s == "draft" || s == "scheduled" || s == "live" || s == "paused") return true;
    }
    return false;
}

std::string round_name(int round) {
    std::string n = std::to_string(round);
    if (n.size() < 2) n = "0" + n;
    return "C#" + n;
}

void ensure_rounds(storage::Store& store) {
    std::lock_guard<std::mutex> lock(store.mtx);
    int maxr = 0;
    for (const auto& c : store.contests) maxr = std::max(maxr, c.value("round", 0));
    bool changed = false;
    for (auto& c : store.contests) {
        if (c.value("round", 0) <= 0) { c["round"] = ++maxr; changed = true; }
    }
    if (changed) store.save_contests();
}

static int next_round_locked(storage::Store& store) {
    int maxr = 0;
    for (const auto& c : store.contests) maxr = std::max(maxr, c.value("round", 0));
    return maxr + 1;
}

bool create_contest(storage::Store& store, cf::Client& cf,
                    const std::vector<int>& ratings, int duration_min,
                    long long scheduled_at, json* out, std::string* err) {
    if (ratings.empty()) { if (err) *err = "give at least one rating"; return false; }
    if (duration_min <= 0) { if (err) *err = "duration must be positive"; return false; }

    std::vector<std::string> handles;
    json cache_problems;
    {
        std::lock_guard<std::mutex> lock(store.mtx);
        if (has_active_locked(store)) {
            if (err) *err = "a contest is already active; finish it first";
            return false;
        }
        for (const auto& u : store.users) {
            std::string h = u.value("cf_handle", "");
            if (!h.empty()) handles.push_back(h);
        }
        cache_problems = store.problemset_cache.value("problems", json::array());
    }
    if (cache_problems.empty()) {
        if (err) *err = "problemset cache is empty — refresh it first";
        return false;
    }

    std::set<std::string> excluded;
    for (const auto& h : handles) {
        std::string e2;
        auto subs = cf.fetch_user_status(h, &e2);
        for (const auto& s : subs)
            if (s.verdict == "OK") excluded.insert(s.key());

    }

    std::unordered_map<int, std::vector<const json*>> by_rating;
    for (const auto& p : cache_problems) {
        int r = p.value("rating", 0);
        if (r == 0) continue;
        by_rating[r].push_back(&p);
    }

    std::unordered_map<int, std::vector<const json*>> pool;
    std::set<std::string> chosen;
    json problems = json::array();

    for (int r : ratings) {
        auto pit = pool.find(r);
        if (pit == pool.end()) {
            std::vector<const json*> cands;
            auto bit = by_rating.find(r);
            if (bit != by_rating.end()) {
                for (const json* p : bit->second) {
                    std::string key = std::to_string(p->value("contestId", 0LL)) +
                                      p->value("index", "");
                    if (!excluded.count(key)) cands.push_back(p);
                }
            }
            std::shuffle(cands.begin(), cands.end(), rng());
            pit = pool.emplace(r, std::move(cands)).first;
        }

        const json* pick = nullptr;
        while (!pit->second.empty()) {
            const json* cand = pit->second.back();
            pit->second.pop_back();
            std::string key = std::to_string(cand->value("contestId", 0LL)) +
                              cand->value("index", "");
            if (chosen.count(key)) continue;
            pick = cand;
            chosen.insert(key);
            break;
        }
        if (!pick) {
            if (err) *err = "not enough unsolved problems available for rating " +
                            std::to_string(r);
            return false;
        }
        problems.push_back({{"contestId", pick->value("contestId", 0LL)},
                            {"index", pick->value("index", "")},
                            {"rating", r},
                            {"name", pick->value("name", "")}});
    }

    std::string id = "ct-" + std::to_string((long long)std::time(nullptr));
    {
        std::lock_guard<std::mutex> lock(store.mtx);
        if (has_active_locked(store)) {
            if (err) *err = "a contest was created meanwhile; try again";
            return false;
        }
        int round = next_round_locked(store);

        std::string status = scheduled_at > 0 ? "scheduled" : "draft";
        json c = {{"id", id},
                  {"round", round},
                  {"start", 0},
                  {"duration_min", duration_min},
                  {"scheduled_at", scheduled_at},
                  {"ended_at", 0},
                  {"status", status},
                  {"paused_secs", 0},
                  {"paused_at", 0},
                  {"problems", problems},
                  {"results", json::object()},
                  {"upsolve_queue", json::object()}};
        store.contests.push_back(c);
        store.save_contests();
        if (out) *out = {{"id", id}, {"count", problems.size()}, {"round", round},
                         {"name", round_name(round)}};
    }
    return true;
}

bool start_contest(storage::Store& store, const std::string& id, std::string* err) {
    std::lock_guard<std::mutex> lock(store.mtx);
    long long now = (long long)std::time(nullptr);

    for (auto& c : store.contests) {
        std::string s = c.value("status", "");
        if (s == "live" || s == "paused") {
            if (err) *err = "a contest is already running";
            return false;
        }
    }
    for (auto& c : store.contests) {
        std::string s = c.value("status", "");
        bool startable = (s == "draft" || s == "scheduled");
        bool match = id.empty() ? startable : (c.value("id", "") == id && startable);
        if (match) {
            c["status"] = "live";
            c["start"] = now;
            c["scheduled_at"] = 0;
            store.save_contests();
            if (err) err->clear();
            return true;
        }
    }
    if (err) *err = "no draft/scheduled contest to start";
    return false;
}

int auto_start_due(storage::Store& store, long long now) {
    std::lock_guard<std::mutex> lock(store.mtx);

    for (const auto& c : store.contests) {
        std::string s = c.value("status", "");
        if (s == "live" || s == "paused") return 0;
    }
    int started = 0;
    for (auto& c : store.contests) {
        if (c.value("status", "") != "scheduled") continue;
        long long at = c.value("scheduled_at", 0LL);
        if (at > 0 && now >= at) {
            c["status"] = "live";
            c["start"] = at;
            c["scheduled_at"] = 0;
            ++started;
        }
    }
    if (started) store.save_contests();
    return started;
}

bool pause_contest(storage::Store& store, const std::string& id, std::string* err) {
    std::lock_guard<std::mutex> lock(store.mtx);
    long long now = (long long)std::time(nullptr);
    for (auto& c : store.contests) {
        bool match = id.empty() ? (c.value("status", "") == "live")
                                : (c.value("id", "") == id);
        if (match && c.value("status", "") == "live") {
            c["status"] = "paused";
            c["paused_at"] = now;
            store.save_contests();
            return true;
        }
    }
    if (err) *err = "no live contest to pause";
    return false;
}

bool resume_contest(storage::Store& store, const std::string& id, std::string* err) {
    std::lock_guard<std::mutex> lock(store.mtx);
    long long now = (long long)std::time(nullptr);
    for (auto& c : store.contests) {
        bool match = id.empty() ? (c.value("status", "") == "paused")
                                : (c.value("id", "") == id);
        if (match && c.value("status", "") == "paused") {
            long long paused_at = c.value("paused_at", 0LL);
            long long gap = paused_at > 0 ? (now - paused_at) : 0;
            c["paused_secs"] = c.value("paused_secs", 0LL) + gap;
            c["paused_at"] = 0;
            c["status"] = "live";
            store.save_contests();
            return true;
        }
    }
    if (err) *err = "no paused contest to resume";
    return false;
}

bool finish_contest(storage::Store& store, const std::string& id, std::string* err) {
    std::lock_guard<std::mutex> lock(store.mtx);
    long long now = (long long)std::time(nullptr);

    for (auto& c : store.contests) {
        std::string s = c.value("status", "");
        bool running = (s == "live" || s == "paused");
        bool match = id.empty() ? running : (c.value("id", "") == id && running);
        if (!match) continue;

        c["ended_at"] = now;
        c["paused_at"] = 0;
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
        return true;
    }
    if (err) *err = "no running contest to finish";
    return false;
}

bool delete_contest(storage::Store& store, const std::string& id, std::string* err) {
    if (id.empty()) { if (err) *err = "contest id required"; return false; }
    std::lock_guard<std::mutex> lock(store.mtx);
    for (auto it = store.contests.begin(); it != store.contests.end(); ++it) {
        if (it->value("id", "") == id) {
            store.contests.erase(it);
            store.save_contests();
            return true;
        }
    }
    if (err) *err = "no contest with that id";
    return false;
}

static int current_index_locked(storage::Store& store) {
    int active = -1, pending = -1, finished = -1;
    long long finished_start = -1;
    for (size_t i = 0; i < store.contests.size(); ++i) {
        std::string s = store.contests[i].value("status", "");
        if (s == "live" || s == "paused") active = (int)i;
        else if (s == "draft" || s == "scheduled") pending = (int)i;
        else if (s == "finished") {
            long long st = store.contests[i].value("start", 0LL);
            if (st >= finished_start) { finished_start = st; finished = (int)i; }
        }
    }
    if (active >= 0) return active;
    if (pending >= 0) return pending;
    return finished;
}

static int index_by_id_locked(storage::Store& store, const std::string& id) {
    for (size_t i = 0; i < store.contests.size(); ++i)
        if (store.contests[i].value("id", "") == id) return (int)i;
    return -1;
}

static bool is_admin_locked(storage::Store& store, const std::string& me) {
    for (const auto& u : store.users)
        if (u.value("username", "") == me) return u.value("role", "") == "admin";
    return false;
}

json contest_view_member_results(const json& c, const std::string& username) {

    if (c.contains("results") && c["results"].contains(username))
        return c["results"][username];
    return json::object();
}

json state_view(storage::Store& store, const std::string& me, const std::string& id) {
    std::lock_guard<std::mutex> lock(store.mtx);
    long long now = (long long)std::time(nullptr);

    json meobj = nullptr;
    for (const auto& u : store.users)
        if (u.value("username", "") == me)
            meobj = {{"username", me}, {"role", u.value("role", "member")},
                     {"cf_handle", u.value("cf_handle", "")}};

    json out = {{"ok", true}, {"me", meobj}, {"server_now", now}, {"contest", nullptr}};

    int idx = id.empty() ? current_index_locked(store) : index_by_id_locked(store, id);
    if (idx < 0) return out;

    const json& c = store.contests[idx];
    std::string status = c.value("status", "");

    bool hidden = (status == "draft" || status == "scheduled");
    if (hidden && !is_admin_locked(store, me)) return out;

    json cv = {{"id", c.value("id", "")},
               {"round", c.value("round", 0)},
               {"name", round_name(c.value("round", 0))},
               {"status", status},
               {"start", c.value("start", 0LL)},
               {"duration_min", c.value("duration_min", 0LL)},
               {"scheduled_at", c.value("scheduled_at", 0LL)},
               {"paused_at", c.value("paused_at", 0LL)},
               {"end", end_unix(c)}};

    if (hidden) {
        cv["problem_count"] = c.value("problems", json::array()).size();
    } else {
        json plist = json::array();
        for (const auto& p : c.value("problems", json::array())) {
            long long cid = p.value("contestId", 0LL);
            std::string ix = p.value("index", "");
            plist.push_back({{"key", std::to_string(cid) + ix},
                             {"contestId", cid},
                             {"index", ix},
                             {"name", p.value("name", "")},
                             {"rating", p.value("rating", 0)},
                             {"url", problem_url(cid, ix)}});
        }
        cv["problems"] = plist;

        std::vector<std::string> names;
        for (const auto& u : store.users) names.push_back(u.value("username", ""));
        std::sort(names.begin(), names.end());

        json members = json::array();
        for (const auto& name : names) {
            json handle = "";
            for (const auto& u : store.users)
                if (u.value("username", "") == name) handle = u.value("cf_handle", "");
            members.push_back({{"username", name},
                               {"cf_handle", handle},
                               {"results", contest_view_member_results(c, name)}});
        }
        cv["members"] = members;
    }

    if (c.contains("upsolve_queue") && c["upsolve_queue"].contains(me))
        out["my_upsolve"] = c["upsolve_queue"][me];
    else
        out["my_upsolve"] = json::array();

    out["contest"] = cv;
    return out;
}

json upsolve_view(storage::Store& store, const std::string& me) {
    std::lock_guard<std::mutex> lock(store.mtx);
    json items = json::array();

    for (const auto& c : store.contests) {
        if (!c.contains("upsolve_queue") || !c["upsolve_queue"].contains(me)) continue;
        const auto& queue = c["upsolve_queue"][me];

        static const json kEmptyArr = json::array();
        const json& problems = c.contains("problems") ? c["problems"] : kEmptyArr;
        std::unordered_map<std::string, const json*> pmap;
        for (const auto& p : problems) {
            std::string key = std::to_string(p.value("contestId", 0LL)) + p.value("index", "");
            pmap[key] = &p;
        }

        for (const auto& kj : queue) {
            std::string key = kj.get<std::string>();
            auto it = pmap.find(key);
            long long cid = it != pmap.end() ? it->second->value("contestId", 0LL) : 0;
            std::string ix = it != pmap.end() ? it->second->value("index", "") : "";
            std::string nm = it != pmap.end() ? it->second->value("name", "") : "";

            bool done = false;
            if (c.contains("results") && c["results"].contains(me) &&
                c["results"][me].contains(key))
                done = c["results"][me][key].value("solved", false);

            items.push_back({{"key", key},
                             {"contest_id", c.value("id", "")},
                             {"index", ix},
                             {"name", nm},
                             {"url", problem_url(cid, ix)},
                             {"done", done}});
        }
    }
    return {{"ok", true}, {"items", items}};
}

json list_view(storage::Store& store, const std::string& me) {
    std::lock_guard<std::mutex> lock(store.mtx);
    bool admin = is_admin_locked(store, me);

    json items = json::array();
    for (const auto& c : store.contests) {
        std::string status = c.value("status", "");
        bool hidden = (status == "draft" || status == "scheduled");
        if (hidden && !admin) continue;

        items.push_back({{"id", c.value("id", "")},
                         {"round", c.value("round", 0)},
                         {"name", round_name(c.value("round", 0))},
                         {"status", status},
                         {"start", c.value("start", 0LL)},
                         {"end", end_unix(c)},
                         {"scheduled_at", c.value("scheduled_at", 0LL)},
                         {"duration_min", c.value("duration_min", 0LL)},
                         {"problem_count", c.value("problems", json::array()).size()}});
    }

    std::sort(items.begin(), items.end(), [](const json& a, const json& b) {
        return a.value("round", 0) > b.value("round", 0);
    });
    return {{"ok", true}, {"items", items}};
}

}
