#include <atomic>
#include <ctime>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "json.hpp"

#include "auth.h"
#include "cf_api.h"
#include "contest.h"
#include "poller.h"
#include "stats.h"
#include "storage.h"

using json = nlohmann::json;

static storage::Store   g_store;
static cf::Client       g_cf;
static std::atomic<bool> g_stop{false};

static const std::string DATA_DIR = "data";
static const std::string WEB_DIR  = "web";

static std::string fmt_time(long long unix_secs) {
    std::time_t t = static_cast<std::time_t>(unix_secs);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

static void send_json(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

static std::string current_user(const httplib::Request& req) {
    std::string cookie = req.get_header_value("Cookie");
    std::string token = auth::token_from_cookie(cookie);
    return auth::user_for_token(g_store, token);
}

static int cli_add_user(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: teamforces --add-user <admin|member> <username> "
                     "<password> <cf_handle>\n";
        return 2;
    }
    std::string role = argv[2], user = argv[3], pass = argv[4], handle = argv[5];
    g_store.load(DATA_DIR);
    std::string err;
    if (!auth::register_user(g_store, user, pass, handle, role, &err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    std::cout << "created " << role << " '" << user << "' (cf_handle=" << handle << ")\n";
    return 0;
}

static int cli_refresh_cache() {
    std::cout << "fetching problemset.problems from Codeforces...\n";
    std::string err;
    auto problems = g_cf.fetch_problemset(&err);
    if (problems.empty()) {
        std::cerr << "failed: " << err << "\n";
        return 1;
    }

    json arr = json::array();
    for (const auto& p : problems) {
        arr.push_back({{"contestId", p.contestId},
                       {"index", p.index},
                       {"name", p.name},
                       {"rating", p.rating},
                       {"tags", p.tags}});
    }
    g_store.load(DATA_DIR);
    {
        std::lock_guard<std::mutex> lock(g_store.mtx);
        g_store.problemset_cache = {{"updated_at", (long long)std::time(nullptr)},
                                    {"problems", arr}};
        g_store.save_problemset_cache();
    }
    std::cout << "cached " << problems.size()
              << " problems -> " << DATA_DIR << "/problemset_cache.json\n";
    return 0;
}

static int cli_poller_test(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: teamforces --poller-test <handle> "
                     "[start_unix duration_min key1 key2 ...]\n";
        return 2;
    }
    std::string handle = argv[2];

    std::cout << "fetching user.status for '" << handle << "' (respecting rate limit)...\n";
    std::string err;
    auto subs = g_cf.fetch_user_status(handle, &err);
    if (subs.empty() && !err.empty()) {
        std::cerr << "failed: " << err << "\n";
        return 1;
    }
    std::cout << "got " << subs.size() << " submissions.\n\n";

    std::cout << "--- most recent submissions ---\n";
    int shown = 0;
    for (const auto& s : subs) {
        if (shown++ >= 15) break;
        std::cout << fmt_time(s.creationTimeSeconds) << "  "
                  << (s.verdict.empty() ? "(judging)" : s.verdict) << "  "
                  << s.key() << "  " << s.problemName << "\n";
    }
    std::cout << "\n";

    long long start = 0, end = 0;
    std::set<std::string> keys;

    if (argc >= 5) {

        start = std::stoll(argv[3]);
        long long dur_min = std::stoll(argv[4]);
        end = start + dur_min * 60;
        for (int i = 5; i < argc; ++i) keys.insert(argv[i]);
        std::cout << "--- evaluating explicit window ---\n";
    } else {

        long long earliest = 0;
        for (const auto& s : subs) {
            if (keys.size() >= 5 && keys.count(s.key()) == 0) continue;
            keys.insert(s.key());
            if (earliest == 0 || s.creationTimeSeconds < earliest)
                earliest = s.creationTimeSeconds;
            if (keys.size() >= 5) break;
        }
        start = earliest > 0 ? earliest - 1 : 0;
        end   = static_cast<long long>(std::time(nullptr));
        std::cout << "--- evaluating auto-demo window (synthetic) ---\n";
    }

    std::cout << "window: " << fmt_time(start) << "  ->  " << fmt_time(end) << "\n";
    std::cout << "problems: ";
    for (const auto& k : keys) std::cout << k << " ";
    std::cout << "\n\n";

    auto results = poller::evaluate(keys, start, end, subs);
    std::cout << "key        solved  elapsed  effort  wrong  upsolved\n";
    std::cout << "---------  ------  -------  ------  -----  --------\n";
    for (const auto& kv : results) {
        const auto& r = kv.second;
        printf("%-9s  %-6s  %7d  %6d  %5d  %-8s\n", kv.first.c_str(),
               r.solved ? "yes" : "no", r.solved_at_min, r.effort_min,
               r.wrong_attempts, r.upsolved ? "yes" : "no");
    }
    return 0;
}

static void register_routes(httplib::Server& svr) {

    svr.Post("/api/login", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }

        std::string username = body.value("username", "");
        std::string password = body.value("password", "");
        if (!auth::check_login(g_store, username, password))
            return send_json(res, 401, {{"ok", false}, {"error", "invalid credentials"}});

        std::string token = auth::create_session(g_store, username);

        res.set_header("Set-Cookie",
                       "session=" + token + "; HttpOnly; Path=/; SameSite=Lax");
        json u = auth::find_user(g_store, username);
        send_json(res, 200, {{"ok", true}, {"username", username},
                             {"role", u.value("role", "member")}});
    });

    svr.Post("/api/logout", [](const httplib::Request& req, httplib::Response& res) {
        std::string token = auth::token_from_cookie(req.get_header_value("Cookie"));
        auth::destroy_session(g_store, token);
        res.set_header("Set-Cookie", "session=; HttpOnly; Path=/; Max-Age=0");
        send_json(res, 200, {{"ok", true}});
    });

    svr.Get("/api/whoami", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}});
        json u = auth::find_user(g_store, me);
        send_json(res, 200, {{"ok", true}, {"username", me},
                             {"role", u.value("role", "member")},
                             {"cf_handle", u.value("cf_handle", "")}});
    });

    svr.Post("/api/admin/register", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        if (!auth::is_admin(g_store, me))
            return send_json(res, 403, {{"ok", false}, {"error", "admin only"}});

        json body;
        try { body = json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }

        std::string username  = body.value("username", "");
        std::string password  = body.value("password", "");
        std::string cf_handle = body.value("cf_handle", "");
        std::string role      = body.value("role", "member");

        std::string err;
        if (!auth::register_user(g_store, username, password, cf_handle, role, &err))
            return send_json(res, 400, {{"ok", false}, {"error", err}});
        send_json(res, 200, {{"ok", true}, {"username", username}, {"role", role}});
    });

    svr.Post("/api/admin/refresh_cache", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (!auth::is_admin(g_store, me))
            return send_json(res, 403, {{"ok", false}, {"error", "admin only"}});

        std::string err;
        auto problems = g_cf.fetch_problemset(&err);
        if (problems.empty())
            return send_json(res, 502, {{"ok", false}, {"error", err}});

        json arr = json::array();
        for (const auto& p : problems)
            arr.push_back({{"contestId", p.contestId}, {"index", p.index},
                           {"name", p.name}, {"rating", p.rating}, {"tags", p.tags}});
        {
            std::lock_guard<std::mutex> lock(g_store.mtx);
            g_store.problemset_cache = {{"updated_at", (long long)std::time(nullptr)},
                                        {"problems", arr}};
            g_store.save_problemset_cache();
        }
        send_json(res, 200, {{"ok", true}, {"count", problems.size()}});
    });

    svr.Get("/api/state", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        std::string id = req.has_param("id") ? req.get_param_value("id") : "";
        send_json(res, 200, contest::state_view(g_store, me, id));
    });

    svr.Get("/api/contests", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        send_json(res, 200, contest::list_view(g_store, me));
    });

    svr.Post("/api/admin/create_contest", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (!auth::is_admin(g_store, me))
            return send_json(res, 403, {{"ok", false}, {"error", "admin only"}});

        json body;
        try { body = json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }

        std::vector<int> ratings;
        for (const auto& r : body.value("ratings", json::array()))
            ratings.push_back(r.get<int>());
        int duration_min = body.value("duration_min", 0);
        long long scheduled_at = body.value("scheduled_at", 0LL);

        json out;
        std::string err;
        if (!contest::create_contest(g_store, g_cf, ratings, duration_min,
                                     scheduled_at, &out, &err))
            return send_json(res, 400, {{"ok", false}, {"error", err}});
        send_json(res, 200, {{"ok", true}, {"id", out.value("id", "")},
                             {"count", out.value("count", 0)},
                             {"name", out.value("name", "")}});
    });

    svr.Post("/api/admin/start", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (!auth::is_admin(g_store, me))
            return send_json(res, 403, {{"ok", false}, {"error", "admin only"}});

        json body;
        try { body = req.body.empty() ? json::object() : json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }

        std::string err;
        if (!contest::start_contest(g_store, body.value("id", ""), &err))
            return send_json(res, 400, {{"ok", false}, {"error", err}});
        send_json(res, 200, {{"ok", true}});
    });

    auto admin_contest_action =
        [&svr](const char* path,
               bool (*action)(storage::Store&, const std::string&, std::string*)) {
            svr.Post(path, [action](const httplib::Request& req, httplib::Response& res) {
                std::string me = current_user(req);
                if (!auth::is_admin(g_store, me))
                    return send_json(res, 403, {{"ok", false}, {"error", "admin only"}});
                json body;
                try { body = req.body.empty() ? json::object() : json::parse(req.body); }
                catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }
                std::string err;
                if (!action(g_store, body.value("id", ""), &err))
                    return send_json(res, 400, {{"ok", false}, {"error", err}});
                send_json(res, 200, {{"ok", true}});
            });
        };
    admin_contest_action("/api/admin/pause",           contest::pause_contest);
    admin_contest_action("/api/admin/resume",          contest::resume_contest);
    admin_contest_action("/api/admin/finish",          contest::finish_contest);
    admin_contest_action("/api/admin/delete_contest",  contest::delete_contest);

    svr.Get("/api/upsolve", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        send_json(res, 200, contest::upsolve_view(g_store, me));
    });

    svr.Post("/api/log", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }

        std::string err;
        bool ok = stats::add_log(g_store, me, body.value("problem_key", ""),
                                 body.value("contest_id", ""), body.value("text", ""),
                                 body.value("tag", ""), &err);
        if (!ok) return send_json(res, 400, {{"ok", false}, {"error", err}});
        send_json(res, 200, {{"ok", true}});
    });

    svr.Get("/api/logs", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        std::string fp = req.has_param("problem") ? req.get_param_value("problem") : "";
        std::string fu = req.has_param("user") ? req.get_param_value("user") : "";
        send_json(res, 200, stats::get_logs(g_store, fp, fu));
    });

    svr.Get("/api/members", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        send_json(res, 200, stats::get_members(g_store));
    });

    svr.Post("/api/log/skip", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        json body; try { body = json::parse(req.body); } catch (...) {
            return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }
        stats::skip_log(g_store, me, body.value("problem_key", ""));
        send_json(res, 200, {{"ok", true}});
    });
    svr.Post("/api/log/unskip", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        json body; try { body = json::parse(req.body); } catch (...) {
            return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }
        stats::unskip_log(g_store, me, body.value("problem_key", ""));
        send_json(res, 200, {{"ok", true}});
    });

    svr.Get("/api/pending_logs", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        send_json(res, 200, stats::pending_logs(g_store, me));
    });

    svr.Post("/api/bookmark", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }
        stats::add_bookmark(g_store, me, body.value("problem_key", ""));
        send_json(res, 200, {{"ok", true}});
    });
    svr.Delete("/api/bookmark", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        json body;
        try { body = req.body.empty() ? json::object() : json::parse(req.body); }
        catch (...) { return send_json(res, 400, {{"ok", false}, {"error", "bad JSON"}}); }
        stats::remove_bookmark(g_store, me, body.value("problem_key", ""));
        send_json(res, 200, {{"ok", true}});
    });
    svr.Get("/api/bookmarks", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        send_json(res, 200, stats::get_bookmarks(g_store, me));
    });

    svr.Get("/api/mystats", [](const httplib::Request& req, httplib::Response& res) {
        std::string me = current_user(req);
        if (me.empty()) return send_json(res, 401, {{"ok", false}, {"error", "not logged in"}});
        send_json(res, 200, stats::my_stats(g_store, me));
    });
}

static int run_server(int port) {
    g_store.load(DATA_DIR);
    contest::ensure_rounds(g_store);

    std::thread([] { poller::run_loop(g_store, g_cf, g_stop); }).detach();

    httplib::Server svr;

    if (!svr.set_mount_point("/", WEB_DIR)) {
        std::cerr << "warning: could not mount '" << WEB_DIR
                  << "' (does the folder exist?)\n";
    }

    register_routes(svr);

    std::cout << "TeamForces server listening on http://0.0.0.0:" << port << "\n";
    std::cout << "(reach it from another device via this machine's Tailscale IP)\n";
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "error: failed to bind port " << port
                  << " (is it already in use?)\n";
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {

    if (argc >= 2) {
        std::string mode = argv[1];
        if (mode == "--add-user")     return cli_add_user(argc, argv);
        if (mode == "--refresh-cache") return cli_refresh_cache();
        if (mode == "--poller-test")  return cli_poller_test(argc, argv);
        if (mode == "--recompute") {

            g_store.load(DATA_DIR);
            std::cout << "recomputing results for all non-draft contests...\n";
            poller::recompute_all(g_store, g_cf);
            std::cout << "done -> " << DATA_DIR << "/contests.json\n";
            return 0;
        }
        if (mode == "--port" && argc >= 3) return run_server(std::stoi(argv[2]));
        if (mode == "--help" || mode == "-h") {
            std::cout <<
                "TeamForces\n"
                "  teamforces                         run the server on port 8080\n"
                "  teamforces --port N                run the server on port N\n"
                "  teamforces --add-user ROLE U P H   create a user (ROLE=admin|member)\n"
                "  teamforces --refresh-cache         fetch & cache the CF problemset\n"
                "  teamforces --poller-test HANDLE    test the CF client + poller\n"
                "  teamforces --recompute             recompute results for past contests\n";
            return 0;
        }
        std::cerr << "unknown option '" << mode << "'. Try --help.\n";
        return 2;
    }
    return run_server(8080);
}
