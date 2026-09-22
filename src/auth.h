#pragma once
#include <string>

#include "json.hpp"
#include "storage.h"

namespace auth {

using json = nlohmann::json;

std::string random_hex(int bytes);

std::string hash_password(const std::string& salt, const std::string& password);

bool register_user(storage::Store& store, const std::string& username,
                   const std::string& password, const std::string& cf_handle,
                   const std::string& role, std::string* err);

bool check_login(storage::Store& store, const std::string& username,
                 const std::string& password);

std::string create_session(storage::Store& store, const std::string& username);

void destroy_session(storage::Store& store, const std::string& token);

std::string token_from_cookie(const std::string& cookie_header);

std::string user_for_token(storage::Store& store, const std::string& token);

json find_user(storage::Store& store, const std::string& username);

bool is_admin(storage::Store& store, const std::string& username);

}
