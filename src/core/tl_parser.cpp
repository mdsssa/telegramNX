#include "core/tl_parser.h"

#include <cstring>

#include "core/tl_schema.h"
#include "platform/platform.h"

namespace tgnx::tl {

namespace {

constexpr uint32_t kVector = 0x1cb5c415;
constexpr uint32_t kBoolTrue = 0x997275b5;
constexpr uint32_t kBoolFalse = 0xbc799737;

TlValue parseField(const FieldDef& f, TlReader& r);

TlValue parseBoxed(TlReader& r) {
    uint32_t ctor = r.u32();
    TlValue v;
    v.kind = TlValue::Kind::Object;
    v.object = std::make_shared<TlNode>(parseObject(r, ctor));
    return v;
}

TlValue parseVector(const FieldDef& f, TlReader& r) {
    uint32_t ctor = r.u32();
    if (ctor != kVector) throw ProtocolError("expected vector constructor");
    uint32_t count = r.u32();
    TlValue v;
    v.kind = TlValue::Kind::Vector;
    v.items.reserve(count);
    std::string_view ref = f.ref ? f.ref : "";
    for (uint32_t i = 0; i < count; ++i) {
        if (ref == "int") {
            TlValue e;
            e.kind = TlValue::Kind::Int;
            e.intValue = r.i32();
            v.items.push_back(std::move(e));
        } else if (ref == "long") {
            TlValue e;
            e.kind = TlValue::Kind::Long;
            e.intValue = r.i64();
            v.items.push_back(std::move(e));
        } else if (ref == "double") {
            TlValue e;
            e.kind = TlValue::Kind::Double;
            uint8_t buf[8];
            r.raw(buf, 8);
            std::memcpy(&e.doubleValue, buf, 8);
            v.items.push_back(std::move(e));
        } else if (ref == "string" || ref == "bytes") {
            TlValue e;
            e.kind = ref == "string" ? TlValue::Kind::String : TlValue::Kind::Bytes;
            e.text = r.str();
            v.items.push_back(std::move(e));
        } else if (ref == "int128" || ref == "int256") {
            TlValue e;
            e.kind = TlValue::Kind::Bytes;
            size_t n = ref == "int128" ? 16 : 32;
            e.text.resize(n);
            r.raw(e.text.data(), n);
            v.items.push_back(std::move(e));
        } else if (ref == "Bool" || ref == "true") {
            TlValue e;
            e.kind = TlValue::Kind::Bool;
            e.boolValue = true;
            v.items.push_back(std::move(e));
        } else {
            v.items.push_back(parseBoxed(r));
        }
    }
    return v;
}

TlValue parseField(const FieldDef& f, TlReader& r) {
    TlValue v;
    switch (f.type) {
        case FieldType::Int:
            v.kind = TlValue::Kind::Int;
            v.intValue = r.i32();
            break;
        case FieldType::Long:
            v.kind = TlValue::Kind::Long;
            v.intValue = r.i64();
            break;
        case FieldType::Int128:
        case FieldType::Int256: {
            v.kind = TlValue::Kind::Bytes;
            size_t n = f.type == FieldType::Int128 ? 16 : 32;
            v.text.resize(n);
            r.raw(v.text.data(), n);
            break;
        }
        case FieldType::Double: {
            v.kind = TlValue::Kind::Double;
            uint8_t buf[8];
            r.raw(buf, 8);
            std::memcpy(&v.doubleValue, buf, 8);
            break;
        }
        case FieldType::String:
            v.kind = TlValue::Kind::String;
            v.text = r.str();
            break;
        case FieldType::Bytes:
            v.kind = TlValue::Kind::Bytes;
            v.text = r.str();
            break;
        case FieldType::Bool: {
            uint32_t val = r.u32();
            v.kind = TlValue::Kind::Bool;
            v.boolValue = val == kBoolTrue;
            break;
        }
        case FieldType::True:
            v.kind = TlValue::Kind::Bool;
            v.boolValue = true;
            break;
        case FieldType::Vector:
            v = parseVector(f, r);
            break;
        case FieldType::Object:
        case FieldType::Boxed:
            v = parseBoxed(r);
            break;
    }
    return v;
}

}  // namespace

TlNode parseObject(TlReader& r, uint32_t ctor) {
    const CtorDef* def = findCtor(ctor);
    if (!def) {
        throw ProtocolError("unknown TL constructor " + hex(&ctor, 4));
    }
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    if (def->hasFlags) flags = r.u32();
    if (def->hasFlags2) flags2 = r.u32();

    TlNode node;
    node.ctor = ctor;
    node.fields.reserve(def->count);
    for (uint16_t i = 0; i < def->count; ++i) {
        const FieldDef& f = def->fields[i];
        if (f.flag >= 0) {
            uint32_t bits = f.flag == 0 ? flags : flags2;
            if ((bits & (1u << f.bit)) == 0) continue;
        }
        node.fields.emplace_back(f.name, parseField(f, r));
    }
    return node;
}

TlNode parseObject(TlReader& r) {
    uint32_t ctor = r.u32();
    return parseObject(r, ctor);
}

TlNode parseObject(const Bytes& b, size_t* consumed) {
    TlReader r(b);
    TlNode node = parseObject(r);
    if (consumed) *consumed = r.pos();
    return node;
}

int64_t getLong(const TlNode& node, std::string_view name, int64_t fallback) {
    const TlValue* v = node.get(name);
    if (!v) return fallback;
    if (v->kind == TlValue::Kind::Long || v->kind == TlValue::Kind::Int) return v->intValue;
    return fallback;
}

int64_t getInt(const TlNode& node, std::string_view name, int64_t fallback) {
    return getLong(node, name, fallback);
}

bool getBool(const TlNode& node, std::string_view name, bool fallback) {
    const TlValue* v = node.get(name);
    if (!v) return fallback;
    if (v->kind == TlValue::Kind::Bool) return v->boolValue;
    return fallback;
}

std::string getString(const TlNode& node, std::string_view name, std::string fallback) {
    const TlValue* v = node.get(name);
    if (!v) return fallback;
    if (v->kind == TlValue::Kind::String || v->kind == TlValue::Kind::Bytes) return v->text;
    return fallback;
}

const TlNode* getObject(const TlNode& node, std::string_view name) {
    const TlValue* v = node.get(name);
    if (!v || v->kind != TlValue::Kind::Object || !v->object) return nullptr;
    return v->object.get();
}

const std::vector<TlValue>* getVector(const TlNode& node, std::string_view name) {
    const TlValue* v = node.get(name);
    if (!v || v->kind != TlValue::Kind::Vector) return nullptr;
    return &v->items;
}

}  // namespace tgnx::tl
