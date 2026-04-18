#include "parser.h"
#include <algorithm>
#include <cctype>

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static int findCRLF(const std::string& buf, size_t start) {
    for (size_t i = start; i + 1 < buf.size(); i++)
        if (buf[i] == '\r' && buf[i+1] == '\n') return (int)i;
    return -1;
}

ParseResult parseRESP(const std::string& buf) {
    ParseResult res;
    if (buf.empty()) return res;

    if (buf[0] == '*') {
        // Multi-bulk: *3\r\n$3\r\nSET\r\n$5\r\nhello\r\n$5\r\nworld\r\n
        int crlf = findCRLF(buf, 1);
        if (crlf < 0) return res;
        int argc = std::stoi(buf.substr(1, crlf - 1));
        size_t pos = crlf + 2;
        for (int i = 0; i < argc; i++) {
            if (pos >= buf.size() || buf[pos] != '$') {
                res.status = ParseStatus::ERROR; return res;
            }
            int cl2 = findCRLF(buf, pos + 1);
            if (cl2 < 0) return res;
            int len = std::stoi(buf.substr(pos + 1, cl2 - pos - 1));
            pos = cl2 + 2;
            if (pos + (size_t)len + 2 > buf.size()) return res;
            res.args.push_back(buf.substr(pos, len));
            pos += len + 2;
        }
        res.status   = ParseStatus::COMPLETE;
        res.consumed = pos;
    } else {
        // Inline: PING\r\n or SET key val\r\n
        int crlf = findCRLF(buf, 0);
        if (crlf < 0) return res;
        std::string line = buf.substr(0, crlf);
        std::string token;
        for (char c : line) {
            if (c == ' ') { if (!token.empty()) { res.args.push_back(token); token.clear(); } }
            else token += c;
        }
        if (!token.empty()) res.args.push_back(token);
        res.status   = ParseStatus::COMPLETE;
        res.consumed = crlf + 2;
    }
    return res;
}
