#pragma once

#include <map>
#include <string>
#include <vector>

namespace npu_avs {

class JsonValue {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    static JsonValue Null();
    static JsonValue Boolean(bool value);
    static JsonValue Number(std::string lexeme);
    static JsonValue String(std::string value);
    static JsonValue ArrayValue(Array value);
    static JsonValue ObjectValue(Object value);

    Type GetType() const { return type_; }
    bool AsBoolean() const { return boolean_; }
    const std::string& AsText() const { return text_; }
    const Array& AsArray() const { return array_; }
    const Object& AsObject() const { return object_; }

private:
    Type type_ = Type::Null;
    bool boolean_ = false;
    std::string text_;
    Array array_;
    Object object_;
};

bool ParseJson(const std::string& text, JsonValue& value, std::string& error);

} // namespace npu_avs
