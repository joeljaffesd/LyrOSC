#include "tui.h"
#include <cstdio>
#include <sstream>

// ANSI helpers
static const char* RESET  = "\033[0m";
static const char* BOLD   = "\033[1m";
static const char* DIM    = "\033[2m";
static const char* CYAN   = "\033[36m";
static const char* GREEN  = "\033[32m";
static const char* YELLOW = "\033[33m";
static const char* CLEAR  = "\033[2J\033[H";

static void print_rule(char ch, int width) {
    for (int i = 0; i < width; ++i) putchar(ch);
    putchar('\n');
}

Tui::Tui(const TuiConfig& cfg) : cfg_(cfg) {}

std::string Tui::wrap(const std::string& s, int width) {
    if (static_cast<int>(s.size()) <= width) return s;
    std::string out;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = start + width;
        if (end < s.size()) {
            // Break at last space
            size_t sp = s.rfind(' ', end);
            if (sp != std::string::npos && sp > start) end = sp + 1;
        } else {
            end = s.size();
        }
        out += s.substr(start, end - start);
        if (end < s.size()) out += '\n';
        start = end;
    }
    return out;
}

void Tui::add_line(const std::string& text, bool final) {
    if (final) {
        lines_.push_back(text);
        if (static_cast<int>(lines_.size()) > TUI_MAX_LINES) lines_.pop_front();
        pending_.clear();
    } else {
        pending_ = text;
    }
    render();
}

void Tui::set_status(const std::string& s) {
    status_ = s;
    render();
}

void Tui::render() const {
    printf("%s", CLEAR);

    // Header
    printf("%s%s", BOLD, CYAN);
    printf("  vocal-stem-stt");
    printf("%s\n", RESET);

    printf("%s", DIM);
    printf("  mode: %s  |  osc: %s  |  device: %s\n",
           cfg_.mode.c_str(), cfg_.osc_target.c_str(), cfg_.device.c_str());
    printf("%s", RESET);

    print_rule('-', TUI_WIDTH);

    // Transcript area
    for (const auto& line : lines_) {
        std::string wrapped = wrap(line, TUI_WIDTH - 2);
        // Print each wrapped sub-line
        std::istringstream ss(wrapped);
        std::string sub;
        while (std::getline(ss, sub)) {
            printf("  %s%s%s\n", GREEN, sub.c_str(), RESET);
        }
    }

    // Pending (partial) line
    if (!pending_.empty()) {
        std::string wrapped = wrap(pending_, TUI_WIDTH - 2);
        std::istringstream ss(wrapped);
        std::string sub;
        while (std::getline(ss, sub)) {
            printf("  %s%s%s\n", YELLOW, sub.c_str(), RESET);
        }
    }

    print_rule('-', TUI_WIDTH);

    // Status / hint
    if (!status_.empty()) {
        printf("  %s%s%s\n", DIM, status_.c_str(), RESET);
    }
    printf("  %sCtrl-C twice to quit%s\n", DIM, RESET);

    fflush(stdout);
}
