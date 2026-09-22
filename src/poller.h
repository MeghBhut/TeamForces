#pragma once
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cf_api.h"
#include "storage.h"

namespace poller {

struct ProblemResult {
    bool solved = false;
    int solved_at_min = -1;
    int wrong_attempts = 0;
    bool upsolved = false;
    int effort_min = -1;

};

std::map<std::string, ProblemResult> evaluate(const std::set<std::string>& problem_keys,
                                              long long contest_start,
                                              long long contest_end,
                                              const std::vector<cf::Submission>& subs);

void run_loop(storage::Store& store, cf::Client& cf, const std::atomic<bool>& stop);

void recompute_all(storage::Store& store, cf::Client& cf);

}
