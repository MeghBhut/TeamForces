#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace cf {

struct Problem {
    long long contestId = 0;
    std::string index;
    std::string name;
    int rating = 0;
    std::vector<std::string> tags;

    std::string key() const { return std::to_string(contestId) + index; }
};

struct Submission {
    long long id = 0;
    long long contestId = 0;
    std::string index;
    std::string verdict;
    long long creationTimeSeconds = 0;
    std::string problemName;

    std::string key() const { return std::to_string(contestId) + index; }
};

class Client {
public:

    std::vector<Problem> fetch_problemset(std::string* err = nullptr);

    std::vector<Submission> fetch_user_status(const std::string& handle,
                                              std::string* err = nullptr);

    double min_interval_sec = 2.0;

private:

    std::string http_get(const std::string& url, std::string* err);

    std::mutex rate_mtx_;
    std::chrono::steady_clock::time_point last_request_{};
    bool have_fired_ = false;
};

}
