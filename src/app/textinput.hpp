// A small single-line text field (port of textinput.py): caret, Delete / arrows / Home / End, Ctrl+Backspace (word),
// Ctrl+U and hold-to-repeat. Works on code points, so the caret never splits a UTF-8 character.
#pragma once
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../util/strutil.hpp"

class TextInput {
public:
    explicit TextInput(const std::string& text = "", size_t limit = 120) : limit_(limit) { set(text); }
    std::string text() const { return su::utf8_encode(t_); }
    size_t caret() const { return c_; }
    void set(const std::string& s) {
        t_ = su::utf8_decode(s);
        c_ = t_.size();
    }
    void insert(const std::string& s) {
        std::vector<uint32_t> add;
        for (uint32_t cp : su::utf8_decode(s))
            if (cp >= 0x20 && cp != 0x7f) add.push_back(cp);
        if (!add.empty() && t_.size() + add.size() <= limit_) {
            t_.insert(t_.begin() + (long)c_, add.begin(), add.end());
            c_ += add.size();
        }
    }
    // apply an editing key; true when it was one (so the caller doesn't treat it as a command)
    bool key(const std::string& k, const std::set<std::string>& mods = {}) {
        bool ctrl = mods.count("Control") > 0;
        if (k == "Backspace") {
            if (ctrl) {  // delete the word before the caret
                size_t j = c_;
                while (j > 0 && t_[j - 1] == ' ') j--;
                while (j > 0 && t_[j - 1] != ' ') j--;
                t_.erase(t_.begin() + (long)j, t_.begin() + (long)c_);
                c_ = j;
            } else if (c_ > 0) {
                t_.erase(t_.begin() + (long)c_ - 1);
                c_--;
            }
        } else if (k == "Delete") {
            if (c_ < t_.size()) t_.erase(t_.begin() + (long)c_);
        } else if (k == "ArrowLeft") {
            c_ = c_ > 0 ? c_ - 1 : 0;
        } else if (k == "ArrowRight") {
            c_ = std::min(t_.size(), c_ + 1);
        } else if (k == "Home") {
            c_ = 0;
        } else if (k == "End") {
            c_ = t_.size();
        } else if (k == "u" && ctrl) {
            t_.clear();
            c_ = 0;
        } else {
            return false;
        }
        return true;
    }
    // repeat the editing key that is being held; true when it changed the text
    bool update(double now, const std::set<std::string>& keys, const std::set<std::string>& mods = {}) {
        static const char* REPEATABLE[] = {"Backspace", "Delete", "ArrowLeft", "ArrowRight"};
        std::string held;
        for (auto* k : REPEATABLE)
            if (keys.count(k)) {
                held = k;
                break;
            }
        if (held.empty()) {
            held_.clear();
            return false;
        }
        if (held != held_) {
            held_ = held;
            next_ = now + 0.40;  // the first press was already applied by key_down
            return false;
        }
        bool changed = false;
        while (now >= next_) {
            auto before = t_;
            key(held, mods);
            changed = changed || t_ != before;
            next_ += 0.032;
            if (next_ < now - 0.2) next_ = now + 0.032;
        }
        return changed;
    }
    // (visible text, caret position in it) for a field that fits `room` characters, keeping the caret in view
    std::pair<std::string, size_t> window(size_t room) const {
        room = std::max<size_t>(room, 4);
        long start = std::max(0L, std::min((long)c_ - (long)room + 1, (long)t_.size() - (long)room));
        size_t end = std::min(t_.size(), (size_t)start + room);
        return {su::utf8_encode(std::vector<uint32_t>(t_.begin() + start, t_.begin() + (long)end)), c_ - (size_t)start};
    }

private:
    std::vector<uint32_t> t_;
    size_t c_ = 0, limit_;
    std::string held_;
    double next_ = 0;
};
