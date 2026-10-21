#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Source spans, not line patterns. Never executes config code or flattens generated rules.
// Offsets refer to bytes in the original UTF-8 document, including its BOM/CRLF.
namespace LootFilterConfig {
struct Token {
    size_t begin = 0, end = 0, mate = std::string::npos;
    std::string text;
    bool literal = false;
};
struct Field {
    std::string key;
    size_t begin = 0, end = 0, valueBegin = 0, valueEnd = 0, next = 0;
};
struct Patch { size_t begin, end; std::string text; };

inline std::string Quote(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 32 || c == 127) {
                result += '\\';
                result += char('0' + c / 100);
                result += char('0' + c / 10 % 10);
                result += char('0' + c % 10);
            } else result += char(c);
        }
    }
    return result + '"';
}

class Document {
    size_t rootToken = 0;
    std::vector<Token> tokens;

    size_t LongEnd(size_t start) const {
        if (start >= source.size() || source[start] != '[') return std::string::npos;
        size_t bracket = start + 1;
        while (bracket < source.size() && source[bracket] == '=') ++bracket;
        if (bracket >= source.size() || source[bracket] != '[') return std::string::npos;
        std::string closing = "]" + source.substr(start + 1, bracket - start - 1) + "]";
        size_t end = source.find(closing, bracket + 1);
        if (end == std::string::npos) throw std::runtime_error("Unclosed Lua long string/comment");
        return end + closing.size();
    }

    void Lex() {
        tokens.clear();
        std::vector<size_t> stack;
        size_t p = source.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
        while (p < source.size()) {
            unsigned char c = source[p];
            if (std::isspace(c)) { ++p; continue; }
            if (source.compare(p, 2, "--") == 0) {
                size_t end = LongEnd(p + 2);
                if (end != std::string::npos) p = end;
                else { p = source.find('\n', p); if (p == std::string::npos) p = source.size(); }
                continue;
            }
            Token token; token.begin = p;
            size_t longEnd = c == '[' ? LongEnd(p) : std::string::npos;
            if (longEnd != std::string::npos) { p = longEnd; token.literal = true; }
            else if (c == '\'' || c == '"') {
                const char quote = char(c); ++p;
                bool closed = false;
                while (p < source.size()) {
                    if (source[p] == '\\') { p += 2; continue; }
                    if (source[p++] == quote) { closed = true; break; }
                }
                if (!closed || p > source.size()) throw std::runtime_error("Unclosed Lua quoted string");
                token.literal = true;
            } else if (std::isalnum(c) || c == '_') {
                do { ++p; } while (p < source.size() && (std::isalnum(static_cast<unsigned char>(source[p])) || source[p] == '_'));
            } else ++p;
            token.end = p;
            token.text = source.substr(token.begin, p - token.begin);
            tokens.push_back(token);
            size_t index = tokens.size() - 1;
            if (token.literal) continue;
            if (token.text == "{" || token.text == "[" || token.text == "(") stack.push_back(index);
            if (token.text == "}" || token.text == "]" || token.text == ")") {
                if (stack.empty()) throw std::runtime_error("Unmatched Lua closing delimiter");
                size_t open = stack.back(); stack.pop_back();
                const auto& text = tokens[open].text;
                if ((text == "{" && token.text != "}") || (text == "[" && token.text != "]") || (text == "(" && token.text != ")"))
                    throw std::runtime_error("Mismatched Lua delimiters");
                tokens[open].mate = index; tokens[index].mate = open;
            }
        }
        if (!stack.empty()) throw std::runtime_error("Unclosed Lua delimiter");
    }

    size_t ExpressionEnd(size_t first, size_t limit) const {
        int blocks = 0;
        size_t p = first;
        for (; p < limit; ++p) {
            const auto& t = tokens[p];
            if (t.literal) continue;
            if (t.text == "{" || t.text == "[" || t.text == "(") { p = t.mate; continue; }
            if (t.text == "function" || t.text == "if" || t.text == "do" || t.text == "repeat") ++blocks;
            else if (t.text == "end" || t.text == "until") --blocks;
            else if (!blocks && (t.text == "," || t.text == ";")) break;
        }
        return p;
    }

    static std::string Key(const Token& token) {
        if (token.literal && token.text.size() >= 2 && (token.text[0] == '"' || token.text[0] == '\''))
            return token.text.substr(1, token.text.size() - 2);
        return token.text;
    }

public:
    std::string source, error, returnedName;
    size_t rootBegin = 0, rootEnd = 0;
    bool generated = false;
    std::vector<Field> fields, rules;
    std::string rulesTail;

    bool Parse(const std::string& text) {
        source = text; error.clear(); returnedName.clear(); fields.clear(); rules.clear(); rulesTail.clear(); generated = false;
        try {
            Lex();
            size_t returned = std::string::npos;
            int blocks = 0;
            for (size_t p = 0; p < tokens.size(); ++p) {
                const auto& t = tokens[p];
                if (t.literal) continue;
                if (t.text == "{" || t.text == "[" || t.text == "(") { p = t.mate; continue; }
                if (t.text == "function" || t.text == "if" || t.text == "do" || t.text == "repeat") ++blocks;
                else if (t.text == "end" || t.text == "until") --blocks;
                else if (t.text == "return" && !blocks) returned = p + 1;
            }
            if (returned >= tokens.size()) throw std::runtime_error("Expected a returned config table");
            rootToken = returned;
            if (tokens[returned].text != "{") {
                returnedName = tokens[returned].text;
                if (returned + 1 < tokens.size() && tokens[returned + 1].text != ";")
                    throw std::runtime_error("Computed return expression: use the complete source editor");
                rootToken = std::string::npos;
                for (size_t p = 0; p + 3 < tokens.size(); ++p) {
                    if (tokens[p].text == "local" && tokens[p + 1].text == returnedName && tokens[p + 2].text == "=" && tokens[p + 3].text == "{") {
                        rootToken = p + 3; break;
                    }
                    if (!tokens[p].literal && (tokens[p].text == "{" || tokens[p].text == "[" || tokens[p].text == "(")) p = tokens[p].mate;
                }
                if (rootToken == std::string::npos) throw std::runtime_error("Returned config is computed: use the complete source editor");
                generated = true;
            }
            rootBegin = tokens[rootToken].begin; rootEnd = tokens[tokens[rootToken].mate].end;
            fields = Table(rootToken);
            std::map<std::string, bool> seen;
            for (const auto& field : fields) {
                if (field.key.empty() || !seen.emplace(field.key, true).second)
                    throw std::runtime_error("Config must have unique named fields");
            }
            if (const auto* field = Find("rules")) {
                size_t p = TokenAt(field->valueBegin);
                if (tokens[p].text != "{" || tokens[tokens[p].mate].end != field->valueEnd)
                    throw std::runtime_error("Computed rules expression: use the complete source editor");
                rules = Table(p);
                for (const auto& rule : rules) if (!rule.key.empty()) throw std::runtime_error("Keyed rules table: use the complete source editor");
                size_t tail = rules.empty() ? field->valueBegin + 1 : rules.back().next;
                rulesTail = source.substr(tail, field->valueEnd - 1 - tail);
            }
            return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }

    size_t TokenAt(size_t offset) const {
        auto it = std::lower_bound(tokens.begin(), tokens.end(), offset, [](const Token& t, size_t p) { return t.begin < p; });
        if (it == tokens.end() || it->begin != offset) throw std::runtime_error("Invalid source offset");
        return size_t(it - tokens.begin());
    }

    std::vector<Field> Table(size_t open) const {
        if (tokens[open].literal || tokens[open].text != "{") throw std::runtime_error("Expected table literal");
        std::vector<Field> result;
        const size_t close = tokens[open].mate;
        size_t prefix = tokens[open].end;
        for (size_t p = open + 1; p < close;) {
            Field field; field.begin = prefix;
            size_t first = p;
            if (p + 1 < close && tokens[p + 1].text == "=") { field.key = Key(tokens[p]); p += 2; }
            else if (tokens[p].text == "[" && tokens[p].mate == p + 2 && p + 3 < close && tokens[p + 3].text == "=") {
                field.key = Key(tokens[p + 1]); p += 4;
            }
            size_t end = ExpressionEnd(p, close);
            if (end <= p) throw std::runtime_error("Missing table field value");
            field.valueBegin = tokens[p].begin; field.valueEnd = tokens[end - 1].end;
            field.end = tokens[end - 1].end;
            field.next = end < close ? tokens[end].end : field.end;
            if (first >= end) throw std::runtime_error("Invalid table field");
            result.push_back(field); prefix = field.next; p = end < close ? end + 1 : end;
        }
        return result;
    }

    const Field* Find(const std::string& key) const {
        for (const auto& field : fields) if (field.key == key) return &field;
        return nullptr;
    }
    std::string Value(const Field& field) const { return source.substr(field.valueBegin, field.valueEnd - field.valueBegin); }
    std::vector<std::string> RuleSources() const {
        std::vector<std::string> result;
        for (const auto& rule : rules) result.push_back(source.substr(rule.begin, rule.end - rule.begin));
        return result;
    }
    std::vector<std::pair<std::string, std::string>> RuleFields(size_t index) const {
        std::vector<std::pair<std::string, std::string>> result;
        size_t p = TokenAt(rules.at(index).valueBegin);
        if (tokens[p].text == "{") for (const auto& f : Table(p)) result.push_back({f.key, Value(f)});
        return result;
    }
    bool IsString(const std::string& expression) const {
        Document doc; doc.source = expression;
        try { doc.Lex(); return doc.tokens.size() == 1 && doc.tokens[0].literal; }
        catch (...) { return false; }
    }
    static std::string Apply(std::string text, std::vector<Patch> patches) {
        std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) { return a.begin > b.begin; });
        size_t previous = text.size();
        for (const auto& patch : patches) {
            if (patch.begin > patch.end || patch.end > previous) throw std::runtime_error("Overlapping config edits");
            text.replace(patch.begin, patch.end - patch.begin, patch.text); previous = patch.begin;
        }
        return text;
    }
    std::string Update(const std::map<std::string, std::string>& globals, const std::vector<std::string>& editedRules, bool changeRules) const {
        if (!error.empty()) throw std::runtime_error(error);
        std::vector<Patch> patches;
        std::string additions;
        for (const auto& pair : globals) {
            if (pair.first == "rules") continue;
            if (const auto* field = Find(pair.first)) {
                if (Value(*field) != pair.second) patches.push_back({field->valueBegin,field->valueEnd,pair.second});
            } else additions += pair.first + " = " + pair.second + ",\n    ";
        }
        if (changeRules) {
            std::string content = "{";
            for (const auto& raw : editedRules) {
                // Parse each draft as an array entry. This also handles leading comments,
                // one-line tables, long strings, and a pasted trailing comma/semicolon.
                Document draft;
                if (!draft.Parse("return { rules = {" + raw + "\n} }" ) || draft.rules.size() != 1)
                    throw std::runtime_error("Rule draft must contain exactly one Lua expression: " + draft.error);
                const auto sources = draft.RuleSources();
                content += sources[0] + ",";
                // Keep comments after the expression as well as before it.
                // The final newline belongs to our wrapper, not the draft.
                if (draft.rulesTail.size() > 1) content += draft.rulesTail.substr(0, draft.rulesTail.size() - 1) + "\n";
            }
            content += rulesTail + "\n    }";
            if (const auto* field = Find("rules")) patches.push_back({field->valueBegin,field->valueEnd,content});
            else additions += "rules = " + content + ",\n    ";
        }
        // Insert before the first field, where an extra separator is never needed.
        if (!additions.empty()) patches.push_back({rootBegin + 1, rootBegin + 1, "\n    " + additions});
        return Apply(source, patches);
    }
};

inline bool Read(const std::string& path, std::string& text) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer; buffer << file.rdbuf(); text = buffer.str(); return !file.bad();
}
}
