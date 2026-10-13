// Game & Watch port: utils.cpp without <sstream> (std::stringstream alone costs ~100 KB of code).
#include <stdio.h>
#include <stdlib.h>
#include "utils.hpp"

std::string Utils::to_string(int i)      { char b[16]; snprintf(b, sizeof b, "%d", i); return std::string(b); }
std::string Utils::to_string(char c)     { return std::string(1, c); }
std::string Utils::to_hex_string(int i)  { char b[16]; snprintf(b, sizeof b, "%x", (unsigned)i); return std::string(b); }
uint32_t Utils::from_hex_string(std::string s) { return (uint32_t)strtoul(s.c_str(), NULL, 16); }
