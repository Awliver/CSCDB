#pragma once

#include <atomic>
#include <fstream>
#include <string>

inline std::atomic<bool> g_output_file_enabled{true};

inline void append_output_file(const std::string &content) {
    if (!g_output_file_enabled.load()) return;
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << content;
    outfile.close();
}

inline bool output_file_enabled() { return g_output_file_enabled.load(); }
