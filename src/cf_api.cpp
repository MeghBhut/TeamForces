#include "cf_api.h"

#include <array>
#include <cstdio>
#include <thread>

#include "json.hpp"

using json = nlohmann::json;

namespace cf {

static std::string run_curl(const std::string& url, std::string* err) {
#if defined(_WIN32)

    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #define POPEN popen
    #define PCLOSE pclose
#endif

#if defined(_WIN32)
    const char* devnull = "NUL";
#else
    const char* devnull = "/dev/null";
#endif
    std::string cmd = "curl -s -L -m 30 --fail-with-body \"" + url + "\" 2>" + devnull;

    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) {
        if (err) *err = "failed to launch curl (is curl on your PATH?)";
        return {};
    }

    std::string out;
    std::array<char, 4096> buf;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0)
        out.append(buf.data(), n);

    int rc = PCLOSE(pipe);
    if (rc != 0 && out.empty()) {

        if (err) *err = "curl exited with code " + std::to_string(rc);
    }
    return out;

    #undef POPEN
    #undef PCLOSE
}

std::string Client::http_get(const std::string& url, std::string* err) {
    std::lock_guard<std::mutex> lock(rate_mtx_);

    if (have_fired_) {

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_request_).count();
        double wait = min_interval_sec - elapsed;
        if (wait > 0)
            std::this_thread::sleep_for(std::chrono::duration<double>(wait));
    }

    last_request_ = std::chrono::steady_clock::now();
    have_fired_ = true;

    return run_curl(url, err);
}

static json parse_cf_envelope(const std::string& body) {
    if (body.empty())
        throw std::runtime_error("empty response (no network? curl missing?)");

    json j = json::parse(body);

    std::string status = j.value("status", "");
    if (status != "OK") {
        std::string comment = j.value("comment", "unknown Codeforces error");
        throw std::runtime_error("Codeforces API: " + comment);
    }
    return j.at("result");
}

std::vector<Problem> Client::fetch_problemset(std::string* err) {
    std::vector<Problem> out;
    try {
        std::string body = http_get("https://codeforces.com/api/problemset.problems", err);
        json result = parse_cf_envelope(body);

        for (const auto& p : result.at("problems")) {
            Problem pr;

            if (!p.contains("contestId")) continue;
            pr.contestId = p.at("contestId").get<long long>();
            pr.index     = p.value("index", "");
            pr.name      = p.value("name", "");
            pr.rating    = p.value("rating", 0);
            if (p.contains("tags"))
                for (const auto& t : p.at("tags")) pr.tags.push_back(t.get<std::string>());
            out.push_back(std::move(pr));
        }
    } catch (const std::exception& e) {
        if (err && err->empty()) *err = e.what();
        return {};
    }
    return out;
}

std::vector<Submission> Client::fetch_user_status(const std::string& handle, std::string* err) {

    for (char c : handle) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            if (err) *err = "invalid handle: '" + handle + "'";
            return {};
        }
    }

    std::vector<Submission> out;
    try {

        std::string url = "https://codeforces.com/api/user.status?handle=" + handle +
                          "&from=1&count=2000";
        std::string body = http_get(url, err);
        json result = parse_cf_envelope(body);

        for (const auto& s : result) {

            if (!s.contains("problem")) continue;
            const auto& prob = s.at("problem");
            if (!prob.contains("contestId")) continue;

            Submission sub;
            sub.id                    = s.value("id", 0LL);
            sub.contestId             = prob.at("contestId").get<long long>();
            sub.index                 = prob.value("index", "");
            sub.verdict               = s.value("verdict", "");
            sub.creationTimeSeconds   = s.value("creationTimeSeconds", 0LL);
            sub.problemName           = prob.value("name", "");
            out.push_back(std::move(sub));
        }
    } catch (const std::exception& e) {
        if (err && err->empty()) *err = e.what();
        return {};
    }
    return out;
}

}
