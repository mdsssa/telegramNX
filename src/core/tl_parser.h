// Schema-driven TL object parser. Produces a generic field tree, which keeps
// the client independent from the exact shape of complex types like Message.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/tl.h"

namespace tgnx::tl {

struct TlNode;

struct TlValue {
    enum class Kind : uint8_t { Null, Int, Long, Double, Bool, String, Bytes, Object, Vector };
    Kind kind = Kind::Null;
    int64_t intValue = 0;
    double doubleValue = 0;
    bool boolValue = false;
    std::string text;  // string/bytes payload
    std::vector<TlValue> items;
    std::shared_ptr<TlNode> object;
};

struct TlNode {
    uint32_t ctor = 0;
    std::vector<std::pair<std::string, TlValue>> fields;

    const TlValue* get(std::string_view name) const {
        for (const auto& f : fields) {
            if (f.first == name) return &f.second;
        }
        return nullptr;
    }
};

TlNode parseObject(TlReader& r, uint32_t ctor);
TlNode parseObject(TlReader& r);  // reads the constructor id too
TlNode parseObject(const Bytes& b, size_t* consumed = nullptr);

// Convenience accessors; all return defaults for missing/mismatched fields.
int64_t getLong(const TlNode& node, std::string_view name, int64_t fallback = 0);
int64_t getInt(const TlNode& node, std::string_view name, int64_t fallback = 0);
bool getBool(const TlNode& node, std::string_view name, bool fallback = false);
std::string getString(const TlNode& node, std::string_view name, std::string fallback = {});
const TlNode* getObject(const TlNode& node, std::string_view name);
const std::vector<TlValue>* getVector(const TlNode& node, std::string_view name);

}  // namespace tgnx::tl
