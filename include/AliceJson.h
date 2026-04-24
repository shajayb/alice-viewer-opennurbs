#ifndef ALICE_JSON_H
#define ALICE_JSON_H

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <type_traits>
#include "AliceMemory.h"

namespace Alice { extern LinearArena g_JsonArena; }

// --- Custom JSON Parser (Zero-Heap, DOD) ---
namespace AliceJson {
    enum JsonType { J_NULL, J_OBJECT, J_ARRAY, J_STRING, J_NUMBER, J_BOOL };
    struct JsonValue;
    struct JsonKeyValuePair { const char* key; JsonValue* value; };
    struct JsonValue {
        JsonType type = J_NULL;
        union {
            double number;
            bool boolean;
            struct { const char* ptr; size_t len; } string;
            struct { JsonKeyValuePair* pairs; uint32_t count; } object;
            struct { JsonValue** values; uint32_t count; } array;
        };

        const JsonValue& operator[](const char* key) const {
            if (type == J_OBJECT) {
                for (uint32_t i = 0; i < object.count; ++i) {
                    if (strcmp(object.pairs[i].key, key) == 0) return *object.pairs[i].value;
                }
            }
            static JsonValue nullVal; return nullVal;
        }

        const JsonValue& operator[](uint32_t index) const {
            if (type == J_ARRAY && index < array.count) return *array.values[index];
            static JsonValue nullVal; return nullVal;
        }

        const JsonValue& operator[](int index) const { return (*this)[(uint32_t)index]; }

        bool contains(const char* key) const {
            if (type != J_OBJECT) return false;
            for (uint32_t i = 0; i < object.count; ++i) {
                if (strcmp(object.pairs[i].key, key) == 0) return true;
            }
            return false;
        }

        uint32_t size() const {
            if (type == J_ARRAY) return array.count;
            if (type == J_OBJECT) return object.count;
            return 0;
        }

        std::string get_string() const {
            if (type == J_STRING) return std::string(string.ptr, string.len);
            return "";
        }

        template<typename T> T get() const {
            if constexpr (std::is_same_v<T, std::string>) return get_string();
            return (T)*this;
        }

        operator double() const { return (type == J_NUMBER) ? number : 0.0; }
        operator float() const { return (type == J_NUMBER) ? (float)number : 0.0f; }
        operator int() const { return (type == J_NUMBER) ? (int)number : 0; }
        operator int64_t() const { return (type == J_NUMBER) ? (int64_t)number : 0; }
        operator std::string() const { return get_string(); }
    };

    static void skip_ws(const char*& p) { while (*p && (*p <= 32)) p++; }
    static void skip_value(const char*& p) {
        skip_ws(p);
        if (*p == '{') {
            p++; skip_ws(p);
            while (*p && *p != '}') { if (*p == '\"') { skip_value(p); skip_ws(p); if (*p == ':') { p++; skip_value(p); } } skip_ws(p); if (*p == ',') p++; skip_ws(p); }
            if (*p == '}') p++;
        } else if (*p == '[') {
            p++; skip_ws(p);
            while (*p && *p != ']') { skip_value(p); skip_ws(p); if (*p == ',') p++; skip_ws(p); }
            if (*p == ']') p++;
        } else if (*p == '\"') {
            p++; while (*p && *p != '\"') { if (*p == '\\') p++; p++; }
            if (*p == '\"') p++;
        } else {
            while (*p && *p != ',' && *p != '}' && *p != ']' && !(*p <= 32)) p++;
        }
    }
    static JsonValue* parse_value(const char*& p);
    static char* parse_string_raw(const char*& p) {
        if (*p != '\"') return nullptr;
        p++; const char* start = p;
        while (*p && *p != '\"') { if (*p == '\\') p++; p++; }
        size_t len = p - start;
        char* str = (char*)Alice::g_JsonArena.allocate(len + 1);
        memcpy(str, start, len); str[len] = '\0';
        if (*p == '\"') p++; return str;
    }
    static JsonValue* parse_object(const char*& p) {
        if (*p != '{') return nullptr;
        const char* count_p = p + 1; uint32_t count = 0; skip_ws(count_p);
        while (*count_p && *count_p != '}') {
            if (*count_p == '\"') { skip_value(count_p); skip_ws(count_p); if (*count_p == ':') { count_p++; skip_value(count_p); count++; } }
            skip_ws(count_p); if (*count_p == ',') count_p++; skip_ws(count_p);
        }
        JsonValue* obj = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue));
        obj->type = J_OBJECT; obj->object.count = count; p++; skip_ws(p);
        obj->object.pairs = (JsonKeyValuePair*)Alice::g_JsonArena.allocate(sizeof(JsonKeyValuePair) * count);
        for (uint32_t i = 0; i < count; ++i) {
            obj->object.pairs[i].key = parse_string_raw(p); skip_ws(p);
            if (*p == ':') p++; skip_ws(p);
            obj->object.pairs[i].value = parse_value(p); skip_ws(p);
            if (*p == ',') p++; skip_ws(p);
        }
        if (*p == '}') p++; return obj;
    }
    static JsonValue* parse_array(const char*& p) {
        if (*p != '[') return nullptr;
        const char* count_p = p + 1; uint32_t count = 0; skip_ws(count_p);
        while (*count_p && *count_p != ']') { skip_value(count_p); count++; skip_ws(count_p); if (*count_p == ',') count_p++; skip_ws(count_p); }
        JsonValue* arr = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue));
        arr->type = J_ARRAY; arr->array.count = count; p++; skip_ws(p);
        arr->array.values = (JsonValue**)Alice::g_JsonArena.allocate(sizeof(JsonValue*) * count);
        for (uint32_t i = 0; i < count; ++i) { arr->array.values[i] = parse_value(p); skip_ws(p); if (*p == ',') p++; skip_ws(p); }
        if (*p == ']') p++; return arr;
    }

    static JsonValue* parse_value(const char*& p) {
        skip_ws(p);
        if (*p == '{') return parse_object(p);
        if (*p == '[') return parse_array(p);
        if (*p == '\"') {
            JsonValue* val = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue));
            val->type = J_STRING; char* s = parse_string_raw(p);
            val->string.ptr = s; val->string.len = strlen(s); return val;
        }
        if ((*p >= '0' && *p <= '9') || *p == '-') {
            JsonValue* val = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue));
            val->type = J_NUMBER; char* end; val->number = strtod(p, &end); p = end; return val;
        }
        if (strncmp(p, "true", 4) == 0) { JsonValue* val = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue)); val->type = J_BOOL; val->boolean = true; p += 4; return val; }
        if (strncmp(p, "false", 5) == 0) { JsonValue* val = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue)); val->type = J_BOOL; val->boolean = false; p += 5; return val; }
        if (strncmp(p, "null", 4) == 0) { JsonValue* val = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue)); val->type = J_NULL; p += 4; return val; }
        JsonValue* nv = (JsonValue*)Alice::g_JsonArena.allocate(sizeof(JsonValue)); nv->type = J_NULL; return nv;
    }

    static JsonValue parse(const uint8_t* data, size_t size) {
        char* buf = (char*)Alice::g_JsonArena.allocate(size + 1);
        memcpy(buf, data, size); buf[size] = '\0';
        const char* p = buf; JsonValue* val = parse_value(p);
        return val ? *val : JsonValue();
    }
}

#endif
