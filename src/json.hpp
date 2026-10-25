#pragma once
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

class Json {
public:
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;  // insertion order, last duplicate wins

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b)
        : _v(b) {}
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    Json(T n)
        : _v(static_cast<int64_t>(n)) {}
    Json(double d)
        : _v(d) {}
    Json(const char* s)
        : _v(std::string(s)) {}
    Json(std::string s)
        : _v(std::move(s)) {}
    Json(std::string_view s)
        : _v(std::string(s)) {}

    static Json arr(std::initializer_list<Json> items = {});
    static Json obj(std::initializer_list<Member> members = {});
    static Json from_array(Array a);
    static Json from_object(Object o);

    bool is_null() const { return _v.index() == 0; }
    bool is_bool() const { return _v.index() == 1; }
    bool is_int() const { return _v.index() == 2; }
    bool is_number() const { return _v.index() == 2 || _v.index() == 3; }
    bool is_string() const { return _v.index() == 4; }
    bool is_array() const { return _v.index() == 5; }
    bool is_object() const { return _v.index() == 6; }

    bool as_bool(bool def = false) const;
    int64_t as_int(int64_t def = 0) const;
    double as_double(double def = 0) const;
    // view into this value: don't outlive it
    std::string_view as_str(std::string_view def = {}) const;

    const Array& items() const;
    const Object& members() const;
    size_t size() const;  // array length / object member count / 0
    bool has(std::string_view key) const;
    const Json& operator[](std::string_view key) const;  // missing -> null
    const Json& operator[](size_t i) const;              // out of range -> null

    Json& set(std::string key, Json v);  // object (null becomes one); replaces
    Json& push(Json v);                  // array (null becomes one)

    std::string dump() const;
    void dump_to(std::string& out) const;

    // On failure returns nullopt and fills *err ("... at offset N").
    static std::optional<Json> parse(std::string_view text,
                                     std::string* err = nullptr);

private:
    std::variant<std::monostate, bool, int64_t, double, std::string, Array,
                 Object>
        _v;
};
