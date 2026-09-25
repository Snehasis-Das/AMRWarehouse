#pragma once

#include <string>
#include "structs.h"
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

bool parse(const std::string& filepath, State& state);
