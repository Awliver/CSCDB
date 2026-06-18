#pragma once

#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>

inline std::atomic<bool> g_output_file_enabled{true};

inline bool parse_set_output_file_off(const char *sql) {
    while (*sql == ' ' || *sql == '\t' || *sql == '\n' || *sql == '\r') sql++;
    if (strncasecmp(sql, "set", 3) != 0) return false;
    std::string low;
    for (const char *p = sql; *p; ++p) {
        char c = *p;
        low += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }
    while (!low.empty() && (low.back() == ' ' || low.back() == '\t' || low.back() == '\r' ||
                            low.back() == '\n' || low.back() == ';')) {
        low.pop_back();
    }
    if (low == "set output_file off") {
        g_output_file_enabled.store(false);
        return true;
    }
    return false;
}

inline void append_output_file(const std::string &content) {
    if (!g_output_file_enabled.load()) return;
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << content;
    outfile.close();
}

inline bool output_file_enabled() { return g_output_file_enabled.load(); }
