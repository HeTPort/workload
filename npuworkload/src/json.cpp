#include "npu_avs/json.h"

#include <cctype>
#include <cstdint>
#include <utility>

namespace npu_avs {
namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& value, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(value, error, 0U)) return false;
        SkipWhitespace();
        if (position_ != text_.size()) return Fail(error, "unexpected trailing content");
        return true;
    }

private:
    bool Fail(std::string& error, const std::string& message) const {
        error = "JSON byte " + std::to_string(position_) + ": " + message;
        return false;
    }

    void SkipWhitespace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    bool ParseValue(JsonValue& value, std::string& error, size_t depth) {
        if (depth > 64U) return Fail(error, "maximum nesting depth exceeded");
        if (position_ >= text_.size()) return Fail(error, "expected a value");
        const char c = text_[position_];
        if (c == '{') return ParseObject(value, error, depth + 1U);
        if (c == '[') return ParseArray(value, error, depth + 1U);
        if (c == '"') {
            std::string string_value;
            if (!ParseString(string_value, error)) return false;
            value = JsonValue::String(std::move(string_value));
            return true;
        }
        if (c == 't') return ParseLiteral("true", JsonValue::Boolean(true), value, error);
        if (c == 'f') return ParseLiteral("false", JsonValue::Boolean(false), value, error);
        if (c == 'n') return ParseLiteral("null", JsonValue::Null(), value, error);
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(value, error);
        return Fail(error, "invalid value token");
    }

    bool ParseLiteral(const char* literal, JsonValue literal_value,
                      JsonValue& value, std::string& error) {
        const size_t start = position_;
        for (size_t i = 0; literal[i] != '\0'; ++i) {
            if (position_ >= text_.size() || text_[position_] != literal[i]) {
                position_ = start;
                return Fail(error, "invalid literal");
            }
            ++position_;
        }
        value = std::move(literal_value);
        return true;
    }

    bool ParseObject(JsonValue& value, std::string& error, size_t depth) {
        ++position_;
        SkipWhitespace();
        JsonValue::Object object;
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            value = JsonValue::ObjectValue(std::move(object));
            return true;
        }
        while (true) {
            if (position_ >= text_.size() || text_[position_] != '"') {
                return Fail(error, "object key must be a string");
            }
            std::string key;
            if (!ParseString(key, error)) return false;
            SkipWhitespace();
            if (position_ >= text_.size() || text_[position_] != ':') {
                return Fail(error, "expected ':' after object key");
            }
            ++position_;
            SkipWhitespace();
            JsonValue member;
            if (!ParseValue(member, error, depth)) return false;
            if (!object.emplace(key, std::move(member)).second) {
                return Fail(error, "duplicate object key '" + key + "'");
            }
            SkipWhitespace();
            if (position_ >= text_.size()) return Fail(error, "unterminated object");
            const char delimiter = text_[position_++];
            if (delimiter == '}') break;
            if (delimiter != ',') return Fail(error, "expected ',' or '}' in object");
            SkipWhitespace();
        }
        value = JsonValue::ObjectValue(std::move(object));
        return true;
    }

    bool ParseArray(JsonValue& value, std::string& error, size_t depth) {
        ++position_;
        SkipWhitespace();
        JsonValue::Array array;
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            value = JsonValue::ArrayValue(std::move(array));
            return true;
        }
        while (true) {
            JsonValue element;
            if (!ParseValue(element, error, depth)) return false;
            array.push_back(std::move(element));
            SkipWhitespace();
            if (position_ >= text_.size()) return Fail(error, "unterminated array");
            const char delimiter = text_[position_++];
            if (delimiter == ']') break;
            if (delimiter != ',') return Fail(error, "expected ',' or ']' in array");
            SkipWhitespace();
        }
        value = JsonValue::ArrayValue(std::move(array));
        return true;
    }

    static int HexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ParseCodeUnit(uint16_t& value, std::string& error) {
        if (position_ + 4U > text_.size()) return Fail(error, "incomplete Unicode escape");
        uint16_t result = 0;
        for (size_t i = 0; i < 4U; ++i) {
            const int digit = HexDigit(text_[position_++]);
            if (digit < 0) return Fail(error, "invalid Unicode escape");
            result = static_cast<uint16_t>((result << 4U) | static_cast<uint16_t>(digit));
        }
        value = result;
        return true;
    }

    static void AppendUtf8(std::string& output, uint32_t code_point) {
        if (code_point <= 0x7fU) output.push_back(static_cast<char>(code_point));
        else if (code_point <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else if (code_point <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
    }

    bool AppendRawUtf8(unsigned char first, std::string& value, std::string& error) {
        size_t continuation_count = 0;
        unsigned char second_minimum = 0x80U;
        unsigned char second_maximum = 0xbfU;
        if (first >= 0xc2U && first <= 0xdfU) continuation_count = 1U;
        else if (first >= 0xe0U && first <= 0xefU) {
            continuation_count = 2U;
            if (first == 0xe0U) second_minimum = 0xa0U;
            if (first == 0xedU) second_maximum = 0x9fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation_count = 3U;
            if (first == 0xf0U) second_minimum = 0x90U;
            if (first == 0xf4U) second_maximum = 0x8fU;
        } else {
            return Fail(error, "invalid UTF-8 leading byte in string");
        }
        if (position_ + continuation_count > text_.size()) {
            return Fail(error, "incomplete UTF-8 sequence in string");
        }
        const unsigned char second = static_cast<unsigned char>(text_[position_]);
        if (second < second_minimum || second > second_maximum) {
            return Fail(error, "invalid UTF-8 sequence in string");
        }
        for (size_t i = 1U; i < continuation_count; ++i) {
            const unsigned char next = static_cast<unsigned char>(text_[position_ + i]);
            if (next < 0x80U || next > 0xbfU) {
                return Fail(error, "invalid UTF-8 continuation byte in string");
            }
        }
        value.push_back(static_cast<char>(first));
        value.append(text_, position_, continuation_count);
        position_ += continuation_count;
        return true;
    }

    bool ParseString(std::string& value, std::string& error) {
        ++position_;
        while (position_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') return true;
            if (c < 0x20U) return Fail(error, "unescaped control character in string");
            if (c != '\\') {
                if (c < 0x80U) value.push_back(static_cast<char>(c));
                else if (!AppendRawUtf8(c, value, error)) return false;
                continue;
            }
            if (position_ >= text_.size()) return Fail(error, "incomplete string escape");
            const char escape = text_[position_++];
            switch (escape) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    uint16_t first = 0;
                    if (!ParseCodeUnit(first, error)) return false;
                    uint32_t code_point = first;
                    if (first >= 0xd800U && first <= 0xdbffU) {
                        if (position_ + 2U > text_.size() || text_[position_] != '\\' ||
                            text_[position_ + 1U] != 'u') {
                            return Fail(error, "high surrogate without low surrogate");
                        }
                        position_ += 2U;
                        uint16_t second = 0;
                        if (!ParseCodeUnit(second, error)) return false;
                        if (second < 0xdc00U || second > 0xdfffU) {
                            return Fail(error, "invalid low surrogate");
                        }
                        code_point = 0x10000U +
                            ((static_cast<uint32_t>(first) - 0xd800U) << 10U) +
                            (static_cast<uint32_t>(second) - 0xdc00U);
                    } else if (first >= 0xdc00U && first <= 0xdfffU) {
                        return Fail(error, "unexpected low surrogate");
                    }
                    AppendUtf8(value, code_point);
                    break;
                }
                default: return Fail(error, "invalid string escape");
            }
        }
        return Fail(error, "unterminated string");
    }

    bool ParseNumber(JsonValue& value, std::string& error) {
        const size_t start = position_;
        if (text_[position_] == '-') ++position_;
        if (position_ >= text_.size()) return Fail(error, "incomplete number");
        if (text_[position_] == '0') {
            ++position_;
            if (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
                return Fail(error, "leading zero in number");
            }
        } else if (text_[position_] >= '1' && text_[position_] <= '9') {
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
        } else return Fail(error, "invalid number integer part");
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            const size_t fraction_start = position_;
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
            if (position_ == fraction_start) return Fail(error, "missing fraction digits");
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            const size_t exponent_start = position_;
            while (position_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
            if (position_ == exponent_start) return Fail(error, "missing exponent digits");
        }
        value = JsonValue::Number(text_.substr(start, position_ - start));
        return true;
    }

    const std::string& text_;
    size_t position_ = 0;
};

} // namespace

JsonValue JsonValue::Null() { return {}; }

JsonValue JsonValue::Boolean(bool value) {
    JsonValue result;
    result.type_ = Type::Boolean;
    result.boolean_ = value;
    return result;
}

JsonValue JsonValue::Number(std::string lexeme) {
    JsonValue result;
    result.type_ = Type::Number;
    result.text_ = std::move(lexeme);
    return result;
}

JsonValue JsonValue::String(std::string value) {
    JsonValue result;
    result.type_ = Type::String;
    result.text_ = std::move(value);
    return result;
}

JsonValue JsonValue::ArrayValue(Array value) {
    JsonValue result;
    result.type_ = Type::Array;
    result.array_ = std::move(value);
    return result;
}

JsonValue JsonValue::ObjectValue(Object value) {
    JsonValue result;
    result.type_ = Type::Object;
    result.object_ = std::move(value);
    return result;
}

bool ParseJson(const std::string& text, JsonValue& value, std::string& error) {
    return JsonParser(text).Parse(value, error);
}

} // namespace npu_avs
