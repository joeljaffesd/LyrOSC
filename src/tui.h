#pragma once

#include <deque>
#include <string>

// Terminal width used for word-wrapping
static constexpr int TUI_WIDTH = 80;
// Maximum transcript lines kept in view
static constexpr int TUI_MAX_LINES = 20;

struct TuiConfig {
    std::string mode;       // "live" or "demo"
    std::string osc_target; // e.g. "127.0.0.1:9010"
    std::string device;     // device name or "demo"
};

class Tui {
public:
    explicit Tui(const TuiConfig& cfg);

    // Redraw the entire screen.
    void render() const;

    // Add a completed (final=true) or in-progress line.
    void add_line(const std::string& text, bool final);

    // Update status message shown in the footer.
    void set_status(const std::string& s);

private:
    TuiConfig           cfg_;
    std::deque<std::string> lines_;
    std::string         pending_;   // current partial line
    std::string         status_;

    static std::string wrap(const std::string& s, int width);
};
