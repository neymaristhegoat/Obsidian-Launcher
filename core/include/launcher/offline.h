#pragma once
#include <string>

namespace launcher::offline {

bool valid_username(const std::string& name);

// Same UUID vanilla gives an offline player (32 hex chars, no dashes)
std::string offline_uuid(const std::string& name);

}