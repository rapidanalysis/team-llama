#include "tags.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace xerxes {
namespace {

// ---------------------------------------------------------------------------
// Minimal JSON reader (enough of RFC 8259 to load any valid tags.json).
// ---------------------------------------------------------------------------
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    std::string str;                                   // String
    std::vector<Json> arr;                             // Array
    std::vector<std::pair<std::string, Json>> obj;     // Object (last duplicate wins on lookup)

    const Json* get(const std::string& key) const {
        const Json* found = nullptr;
        for (const auto& kv : obj)
            if (kv.first == key) found = &kv.second;
        return found;
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    Json parse_document() {
        skip_ws();
        Json v = parse_value(0);
        skip_ws();
        if (p_ != s_.size()) fail("extra data after JSON value");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < p_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        throw TagFileError(what + " (line " + std::to_string(line) + ", column " + std::to_string(col) + ")");
    }

    void skip_ws() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_;
    }

    bool consume(const char* lit) {
        size_t n = std::strlen(lit);
        if (s_.compare(p_, n, lit) == 0) {
            p_ += n;
            return true;
        }
        return false;
    }

    Json parse_value(int depth) {
        if (depth > 256) fail("JSON nested too deeply");
        if (p_ >= s_.size()) fail("unexpected end of file");
        Json v;
        char c = s_[p_];
        if (c == '{') {
            v.kind = Json::Object;
            ++p_;
            skip_ws();
            if (p_ < s_.size() && s_[p_] == '}') {
                ++p_;
                return v;
            }
            while (true) {
                skip_ws();
                if (p_ >= s_.size() || s_[p_] != '"') fail("expected property name");
                std::string key = parse_string();
                skip_ws();
                if (p_ >= s_.size() || s_[p_] != ':') fail("expected ':'");
                ++p_;
                skip_ws();
                v.obj.emplace_back(std::move(key), parse_value(depth + 1));
                skip_ws();
                if (p_ < s_.size() && s_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < s_.size() && s_[p_] == '}') {
                    ++p_;
                    return v;
                }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.kind = Json::Array;
            ++p_;
            skip_ws();
            if (p_ < s_.size() && s_[p_] == ']') {
                ++p_;
                return v;
            }
            while (true) {
                skip_ws();
                v.arr.push_back(parse_value(depth + 1));
                skip_ws();
                if (p_ < s_.size() && s_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < s_.size() && s_[p_] == ']') {
                    ++p_;
                    return v;
                }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.kind = Json::String;
            v.str = parse_string();
            return v;
        }
        if (consume("true") || consume("false")) {
            v.kind = Json::Bool;
            return v;
        }
        if (consume("null")) return v;
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = p_;
            if (s_[p_] == '-') ++p_;
            while (p_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[p_])) || s_[p_] == '.' ||
                                      s_[p_] == 'e' || s_[p_] == 'E' || s_[p_] == '+' || s_[p_] == '-'))
                ++p_;
            if (p_ == start + (s_[start] == '-' ? 1u : 0u)) fail("invalid number");
            v.kind = Json::Number;
            return v;
        }
        fail("unexpected character");
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    uint32_t parse_hex4() {
        if (p_ + 4 > s_.size()) fail("truncated \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char h = s_[p_++];
            v <<= 4;
            if (h >= '0' && h <= '9')
                v |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f')
                v |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                v |= static_cast<uint32_t>(h - 'A' + 10);
            else
                fail("invalid \\u escape");
        }
        return v;
    }

    std::string parse_string() {
        ++p_;  // opening quote
        std::string out;
        while (true) {
            if (p_ >= s_.size()) fail("unterminated string");
            char c = s_[p_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("invalid control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= s_.size()) fail("unterminated string");
            char e = s_[p_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = parse_hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && s_.compare(p_, 2, "\\u") == 0) {
                        size_t save = p_;
                        p_ += 2;
                        uint32_t lo = parse_hex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else
                            p_ = save;
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: fail("invalid escape");
            }
        }
    }

    const std::string& s_;
    size_t p_ = 0;
};

// Equivalent of Python's json.dumps(str) with ensure_ascii=True.
std::string json_quote(const std::string& in) {
    std::string out = "\"";
    char buf[16];
    auto emit_u = [&](uint32_t u) {
        std::snprintf(buf, sizeof buf, "\\u%04x", u);
        out += buf;
    };
    for (size_t i = 0; i < in.size();) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20 || c == 0x7F)
                        emit_u(c);
                    else
                        out += static_cast<char>(c);
            }
            ++i;
            continue;
        }
        // Decode one UTF-8 sequence; invalid bytes are emitted as U+FFFD.
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            len = 0;
        }
        if (len == 0 || i + len > in.size()) {
            cp = 0xFFFD;
            len = 1;
        } else {
            for (size_t k = 1; k < len; ++k) {
                unsigned char cc = static_cast<unsigned char>(in[i + k]);
                if ((cc & 0xC0) != 0x80) {
                    cp = 0xFFFD;
                    len = 1;
                    break;
                }
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            emit_u(0xD800 + (cp >> 10));
            emit_u(0xDC00 + (cp & 0x3FF));
        } else {
            emit_u(cp);
        }
        i += len;
    }
    out += '"';
    return out;
}

}  // namespace

std::string to_upper_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const char* ws = " \t\n\r\v\f";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

TagStore TagStore::load(const std::string& path, bool require_tags_key) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        int err = errno;
        if (err == ENOENT) throw FileNotFound("No such file or directory: '" + path + "'");
        throw TagFileError("could not open '" + path + "': " + std::strerror(err));
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // UTF-8 BOM

    Json root;
    try {
        root = Parser(text).parse_document();
    } catch (const TagFileError& e) {
        throw TagFileError("'" + path + "' is not valid JSON: " + e.what());
    }
    if (root.kind != Json::Object) throw TagFileError("'" + path + "': top level must be an object");

    TagStore store;
    const Json* tags = root.get("tags");
    if (!tags) {
        if (require_tags_key) throw TagFileError("'" + path + "' has no \"tags\" list");
        return store;
    }
    if (tags->kind != Json::Array) throw TagFileError("'" + path + "': \"tags\" must be a list");
    for (size_t i = 0; i < tags->arr.size(); ++i) {
        const Json& t = tags->arr[i];
        const Json* id = t.kind == Json::Object ? t.get("id") : nullptr;
        const Json* cmd = t.kind == Json::Object ? t.get("command") : nullptr;
        if (!id || !cmd || id->kind != Json::String || cmd->kind != Json::String)
            throw TagFileError("'" + path + "': tag #" + std::to_string(i + 1) +
                               " must have string \"id\" and \"command\" fields");
        store.set(to_upper_ascii(id->str), cmd->str);
    }
    return store;
}

void TagStore::save(const std::string& path) const {
    std::string out = "{\n    \"tags\": [";
    for (size_t i = 0; i < entries_.size(); ++i) {
        out += i ? ",\n" : "\n";
        out += "        {\n";
        out += "            \"id\": " + json_quote(entries_[i].first) + ",\n";
        out += "            \"command\": " + json_quote(entries_[i].second) + "\n";
        out += "        }";
    }
    out += entries_.empty() ? "]\n}" : "\n    ]\n}";

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw TagFileError("could not write '" + path + "': " + std::strerror(errno));
    f << out;
    f.close();
    if (!f) throw TagFileError("could not write '" + path + "'");
}

const std::string* TagStore::find(const std::string& id) const {
    for (const auto& e : entries_)
        if (e.first == id) return &e.second;
    return nullptr;
}

void TagStore::set(const std::string& id, const std::string& command) {
    for (auto& e : entries_) {
        if (e.first == id) {
            e.second = command;
            return;
        }
    }
    entries_.emplace_back(id, command);
}

bool TagStore::erase(const std::string& id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == id) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

}  // namespace xerxes
