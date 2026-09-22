#pragma once
#include <string>
#include <vector>

#include "cf_api.h"
#include "json.hpp"
#include "storage.h"

namespace contest {

using json = nlohmann::json;

long long end_unix(const json& contest);

bool is_live(const json& contest, long long now);

std::string problem_url(long long contestId, const std::string& index);

bool create_contest(storage::Store& store, cf::Client& cf,
                    const std::vector<int>& ratings, int duration_min,
                    long long scheduled_at, json* out, std::string* err);

void ensure_rounds(storage::Store& store);

std::string round_name(int round);

bool start_contest(storage::Store& store, const std::string& id, std::string* err);

int auto_start_due(storage::Store& store, long long now);

bool pause_contest(storage::Store& store, const std::string& id, std::string* err);

bool resume_contest(storage::Store& store, const std::string& id, std::string* err);

bool finish_contest(storage::Store& store, const std::string& id, std::string* err);

bool delete_contest(storage::Store& store, const std::string& id, std::string* err);

json state_view(storage::Store& store, const std::string& me, const std::string& id = "");

json list_view(storage::Store& store, const std::string& me);

json upsolve_view(storage::Store& store, const std::string& me);

}
