#pragma once
#include <string>
#include <vector>

enum class ParseStatus { COMPLETE, INCOMPLETE, ERROR };

struct ParseResult {
    ParseStatus              status   = ParseStatus::INCOMPLETE;
    std::vector<std::string> args;
    size_t                   consumed = 0;
};

ParseResult parseRESP(const std::string& buf);
std::string toUpper(std::string s);
