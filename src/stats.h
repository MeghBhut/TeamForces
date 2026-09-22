#pragma once
#include <string>

#include "json.hpp"
#include "storage.h"

namespace stats {

using json = nlohmann::json;

bool valid_tag(const std::string& tag);

bool add_log(storage::Store& store, const std::string& username,
             const std::string& problem_key, const std::string& contest_id,
             const std::string& text, const std::string& tag, std::string* err);

json get_logs(storage::Store& store, const std::string& filter_problem,
              const std::string& filter_user);

json get_members(storage::Store& store);

bool skip_log(storage::Store& store, const std::string& username, const std::string& key);
bool unskip_log(storage::Store& store, const std::string& username, const std::string& key);

json pending_logs(storage::Store& store, const std::string& username);

bool add_bookmark(storage::Store& store, const std::string& username,
                  const std::string& problem_key);
bool remove_bookmark(storage::Store& store, const std::string& username,
                     const std::string& problem_key);

json get_bookmarks(storage::Store& store, const std::string& username);

json my_stats(storage::Store& store, const std::string& username);

}
