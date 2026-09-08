#pragma once

#include <algorithm>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace scyllagpt {

// Incremental VT screen: pipe reads are arbitrary byte boundaries, never records.
// Keep cursor/erase operations in stream order, independently of the display widget.
class TerminalScreen {
public:
    void resize(int cols, int rows) {
        cols_ = std::clamp(cols, 2, 1000);
        rows = std::clamp(rows, 1, 500);
        while (y_ >= rows) { scroll(); --y_; }
        rows_ = rows;
        lines_.resize(rows_);
        for (auto& line : lines_) line.resize(cols_, U' ');
        x_ = std::min(x_, cols_ - 1);
        top_ = 0; bottom_ = rows_ - 1; wrap_ = false;
    }
    void feed(std::string_view bytes) {
        for (unsigned char c : bytes) consume(c);
    }
    std::wstring text(std::size_t* cursor = nullptr) const {
        std::wstring out;
        auto append = [&](const std::u32string& line, int minimum) {
            auto end = line.find_last_not_of(U' ');
            const int n = std::max(minimum, end == std::u32string::npos ? 0 : int(end + 1));
            for (int i = 0; i < n; ++i) {
                char32_t c = i < int(line.size()) ? line[i] : U' ';
                if (c > 0xffff) {
                    c -= 0x10000; out += wchar_t(0xd800 + (c >> 10)); out += wchar_t(0xdc00 + (c & 1023));
                } else out += wchar_t(c);
            }
        };
        for (const auto& line : history_) { append(line, 0); out += L'\r'; }
        int last = y_;
        for (int i = 0; i < rows_; ++i)
            if (lines_[i].find_first_not_of(U' ') != std::u32string::npos) last = std::max(last, i);
        for (int i = 0; i <= last; ++i) {
            if (i == y_ && cursor) {
                const auto start = out.size();
                append(lines_[i].substr(0, x_), x_);
                *cursor = out.size(); out.resize(start);
            }
            append(lines_[i], i == y_ ? x_ : 0);
            if (i != last) out += L'\r';
        }
        return out;
    }
    std::string take_reply() { std::string r; r.swap(reply_); return r; }
    bool application_cursor() const { return application_cursor_; }
    bool bracketed_paste() const { return bracketed_paste_; }

private:
    void scroll() {
        if (top_ == 0 && bottom_ == rows_ - 1 && !alternate_) {
            history_.push_back(lines_.front());
            if (history_.size() > 2000) history_.pop_front();
        }
        lines_.erase(lines_.begin() + top_);
        lines_.insert(lines_.begin() + bottom_, std::u32string(cols_, U' '));
    }
    void newline() { wrap_ = false; if (y_ == bottom_) scroll(); else y_ = std::min(y_ + 1, rows_ - 1); }
    void put(char32_t c) {
        if (wrap_) { x_ = 0; newline(); }
        lines_[y_][x_] = c;
        if (x_ == cols_ - 1) wrap_ = autowrap_; else ++x_;
    }
    void erase_line(int mode) {
        int first = mode == 0 ? x_ : 0, last = mode == 1 ? x_ + 1 : cols_;
        std::fill(lines_[y_].begin() + first, lines_[y_].begin() + last, U' ');
    }
    void csi(char final) {
        std::vector<int> p(1, 0);
        const bool priv = !sequence_.empty() && sequence_[0] == '?';
        for (char c : sequence_) {
            if (c >= '0' && c <= '9') p.back() = std::min(10000, p.back() * 10 + c - '0');
            else if (c == ';') p.push_back(0);
        }
        auto arg = [&](int i, int fallback = 1) { return i < int(p.size()) && p[i] ? p[i] : fallback; };
        const int n = arg(0);
        if (final != 'm') wrap_ = false;
        switch (final) {
        case 'A': y_ = std::max(0, y_ - n); break;
        case 'B': case 'e': y_ = std::min(rows_ - 1, y_ + n); break;
        case 'C': case 'a': x_ = std::min(cols_ - 1, x_ + n); break;
        case 'D': x_ = std::max(0, x_ - n); break;
        case 'E': y_ = std::min(rows_ - 1, y_ + n); x_ = 0; break;
        case 'F': y_ = std::max(0, y_ - n); x_ = 0; break;
        case 'G': case '`': x_ = std::clamp(n - 1, 0, cols_ - 1); break;
        case 'd': y_ = std::clamp(n - 1, 0, rows_ - 1); break;
        case 'H': case 'f': y_ = std::clamp(n - 1, 0, rows_ - 1); x_ = std::clamp(arg(1) - 1, 0, cols_ - 1); break;
        case 'K': erase_line(p[0]); break;
        case 'J':
            if (p[0] == 3) history_.clear();
            else if (p[0] == 2) for (auto& line : lines_) line.assign(cols_, U' ');
            else {
                erase_line(p[0]);
                const int first = p[0] == 1 ? 0 : y_ + 1, last = p[0] == 1 ? y_ : rows_;
                for (int i = first; i < last; ++i) lines_[i].assign(cols_, U' ');
            }
            break;
        case 'P': {
            auto& line = lines_[y_]; line.erase(x_, std::min(n, cols_ - x_)); line.resize(cols_, U' '); break;
        }
        case '@': lines_[y_].insert(x_, std::min(n, cols_ - x_), U' '); lines_[y_].resize(cols_); break;
        case 'X': std::fill_n(lines_[y_].begin() + x_, std::min(n, cols_ - x_), U' '); break;
        case 'S': for (int i = 0; i < std::min(n, rows_); ++i) scroll(); break;
        case 'T': for (int i = 0; i < std::min(n, rows_); ++i) { lines_.erase(lines_.begin() + bottom_); lines_.insert(lines_.begin() + top_, std::u32string(cols_, U' ')); } break;
        case 'L': case 'M':
            if (y_ >= top_ && y_ <= bottom_) for (int i = 0; i < std::min(n, bottom_ - y_ + 1); ++i) {
                lines_.erase(lines_.begin() + (final == 'L' ? bottom_ : y_));
                lines_.insert(lines_.begin() + (final == 'L' ? y_ : bottom_), std::u32string(cols_, U' '));
            }
            break;
        case 'r': if (!priv && n < arg(1, rows_)) { top_ = std::clamp(n - 1, 0, rows_ - 1); bottom_ = std::clamp(arg(1, rows_) - 1, top_, rows_ - 1); x_ = y_ = 0; } break;
        case 's': saved_x_ = x_; saved_y_ = y_; break;
        case 'u': x_ = std::min(saved_x_, cols_ - 1); y_ = std::min(saved_y_, rows_ - 1); break;
        case 'n': if (p[0] == 6) reply_ += "\x1b[" + std::to_string(y_ + 1) + ";" + std::to_string(x_ + 1) + "R"; else if (p[0] == 5) reply_ += "\x1b[0n"; break;
        case 'c': reply_ += "\x1b[?1;0c"; break;
        case 'h': case 'l':
            if (priv) for (int mode : p) {
                bool on = final == 'h';
                if (mode == 1) application_cursor_ = on;
                if (mode == 7) autowrap_ = on;
                if (mode == 2004) bracketed_paste_ = on;
                if ((mode == 1049 || mode == 47 || mode == 1047) && on != alternate_) {
                    if (on) { primary_ = lines_; primary_history_.swap(history_); saved_x_ = x_; saved_y_ = y_; for (auto& line : lines_) line.assign(cols_, U' '); x_ = y_ = 0; }
                    else { lines_ = primary_; history_.swap(primary_history_); lines_.resize(rows_); for (auto& line : lines_) line.resize(cols_, U' '); x_ = std::min(saved_x_, cols_ - 1); y_ = std::min(saved_y_, rows_ - 1); }
                    alternate_ = on; top_ = 0; bottom_ = rows_ - 1;
                }
            }
            break;
        default: break; // SGR and unsupported modes never become printable text.
        }
    }
    void consume(unsigned char c) {
        if (state_ == 3) { if (c == 7) state_ = 0; else if (c == 27) state_ = 4; return; }
        if (state_ == 4) { state_ = c == '\\' ? 0 : 3; return; }
        if (state_ == 5) { state_ = 0; return; }
        if (state_ == 2) {
            if (c >= 0x40 && c <= 0x7e) { csi(char(c)); sequence_.clear(); state_ = 0; }
            else if (c == 27) { sequence_.clear(); state_ = 1; }
            else if (sequence_.size() < 256) sequence_ += char(c);
            return;
        }
        if (state_ == 1) {
            state_ = 0;
            switch (c) {
            case '[': sequence_.clear(); state_ = 2; break;
            case ']': case 'P': case '^': case '_': state_ = 3; break;
            case '(': case ')': case '*': case '+': state_ = 5; break;
            case '7': saved_x_ = x_; saved_y_ = y_; break;
            case '8': x_ = std::min(saved_x_, cols_ - 1); y_ = std::min(saved_y_, rows_ - 1); break;
            case 'D': newline(); break;
            case 'E': x_ = 0; newline(); break;
            case 'M': if (y_ == top_) { lines_.erase(lines_.begin() + bottom_); lines_.insert(lines_.begin() + top_, std::u32string(cols_, U' ')); } else y_ = std::max(0, y_ - 1); break;
            case 'c': { int c0 = cols_, r0 = rows_; *this = TerminalScreen(); resize(c0, r0); break; }
            default: break;
            }
            return;
        }
        if (remaining_) {
            if ((c & 0xc0) == 0x80) {
                codepoint_ = (codepoint_ << 6) | (c & 63);
                if (--remaining_ == 0) put(codepoint_ >= minimum_ && codepoint_ <= 0x10ffff && !(codepoint_ >= 0xd800 && codepoint_ <= 0xdfff) ? codepoint_ : 0xfffd);
                return;
            }
            remaining_ = 0; put(0xfffd);
        }
        if (c == 27) { state_ = 1; return; }
        if (c == '\r') { x_ = 0; wrap_ = false; return; }
        if (c == '\n' || c == 11 || c == 12) { newline(); return; }
        if (c == '\b') { x_ = std::max(0, x_ - 1); wrap_ = false; return; }
        if (c == '\t') { x_ = std::min(cols_ - 1, (x_ / 8 + 1) * 8); wrap_ = false; return; }
        if (c < 32 || c == 127) return;
        if (c < 128) put(c);
        else if (c >= 0xc2 && c <= 0xf4) {
            remaining_ = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
            minimum_ = remaining_ == 1 ? 0x80 : remaining_ == 2 ? 0x800 : 0x10000;
            codepoint_ = c & (remaining_ == 1 ? 31 : remaining_ == 2 ? 15 : 7);
        } else put(0xfffd);
    }
    int cols_ = 80, rows_ = 24, x_ = 0, y_ = 0, top_ = 0, bottom_ = 23;
    int saved_x_ = 0, saved_y_ = 0, state_ = 0, remaining_ = 0;
    char32_t codepoint_ = 0, minimum_ = 0;
    bool wrap_ = false, autowrap_ = true, alternate_ = false, application_cursor_ = false, bracketed_paste_ = false;
    std::vector<std::u32string> lines_ = std::vector<std::u32string>(24, std::u32string(80, U' '));
    std::vector<std::u32string> primary_;
    std::deque<std::u32string> history_, primary_history_;
    std::string sequence_, reply_;
};
} // namespace scyllagpt
