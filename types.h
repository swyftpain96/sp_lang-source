#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <unordered_map>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <functional>

class Expression;
class Interpreter;
struct Value;

struct LineInfo {
    size_t offset;
    int line;
};

// ANSI Color codes
namespace Color {
    const char* const Reset = "\033[0m";
    const char* const Red = "\033[31m";
    const char* const Green = "\033[32m";
    const char* const Yellow = "\033[33m";
    const char* const Blue = "\033[34m";
    const char* const Magenta = "\033[35m";
    const char* const Cyan = "\033[36m";
    const char* const White = "\033[37m";
    const char* const Bold = "\033[1m";
}

class ICallable {
public:
    virtual ~ICallable() = default;
    virtual struct Value call(Interpreter& interpreter, const std::vector<struct Value>& args) = 0;
    virtual bool isVMFunction() const { return false; }
    virtual bool isNative() const { return false; }
    virtual bool isBoundMethod() const { return false; }
};

enum class Type {
    NUMBER,
    STRING,
    BOOLEAN,
    ARRAY,
    OBJECT,
    FUNCTION,
    NULL_VAL,
    UNDEFINED,
    ERROR
};

// NaN-Boxing Constants: 0x7FF0... is the canonical QNaN prefix.
// Tags are stored in bits 48-51. If bit 63 is needed for more tags, it can be used.
#define VALUE_QNAN          0x7FF0000000000000ULL

#define MAKE_TAG(id)        ((uint64_t)(id) << 48)

#define TAG_NIL             MAKE_TAG(1)
#define TAG_UNDEFINED       MAKE_TAG(2)
#define TAG_BOOLEAN         MAKE_TAG(3)
#define TAG_STRING          MAKE_TAG(4)
#define TAG_ARRAY           MAKE_TAG(5)
#define TAG_OBJECT          MAKE_TAG(6)
#define TAG_FUNCTION        MAKE_TAG(7)
#define TAG_NATIVE_FUNCTION MAKE_TAG(8)
#define TAG_CLASS           MAKE_TAG(9)
#define TAG_INSTANCE        MAKE_TAG(10)
#define TAG_BIGINT          MAKE_TAG(11)
#define TAG_DATE            MAKE_TAG(12)
#define TAG_MAP             MAKE_TAG(13)
#define TAG_VM_FUNCTION     MAKE_TAG(14)
#define TAG_ERROR           MAKE_TAG(15)
#define MAKE_TAG_EXT(id)    (((uint64_t)((id) & 0xFULL) << 48) | (1ULL << 63))
#define TAG_NATIVE_MODULE   MAKE_TAG_EXT(1)
#define TAG_BUFFER          MAKE_TAG_EXT(2)

#define QNAN_MASK           0x7FF0000000000000ULL
#define TAG_MASK            (0x000F000000000000ULL | 0x8000000000000000ULL)
#define PAYLOAD_MASK        0x0000FFFFFFFFFFFFULL // 48-bit pointer mask

struct DateData {
    double timestamp;
    DateData(double ts) : timestamp(ts) {}
};

struct ErrorData {
    std::string message;
    int line;
    ErrorData(std::string msg, int l) : message(std::move(msg)), line(l) {}
};

struct Value;
struct ValueHash {
    size_t operator()(const Value& v) const;
};
struct ValueEqual {
    bool operator()(const Value& a, const Value& b) const;
};

struct MapData {
    std::unordered_map<Value, Value, ValueHash, ValueEqual> map;
};

Value parseJSONValue(const std::string& json, size_t& pos, class Interpreter& interp);
std::string stringifyJSON(const Value& val, int indent = 0);

struct Value {
    uint64_t bits;
    explicit Value(uint64_t b) : bits(b) {}

    Value() : bits(VALUE_QNAN | TAG_UNDEFINED) {}
    Value(double n) { 
        union { double d; uint64_t u; } u;
        u.d = n;
        // Canonicalize any existing NaNs to our specific UNDEFINED bit pattern
        if ((u.u & QNAN_MASK) == QNAN_MASK) bits = VALUE_QNAN | TAG_UNDEFINED;
        else bits = u.u;
    }
    Value(long n) {
        bits = 0; // Initialize union
        union { double d; uint64_t u; } u;
        u.d = static_cast<double>(n);
        bits = u.u;
    }
    Value(bool b) : bits(VALUE_QNAN | TAG_BOOLEAN | (b ? 1 : 0)) {}
    Value(Type t) {
        if (t == Type::NULL_VAL) bits = VALUE_QNAN | TAG_NIL;
        else bits = VALUE_QNAN | TAG_UNDEFINED;
    }
    Value(std::string* s) : bits(VALUE_QNAN | TAG_STRING | ((uint64_t)s & PAYLOAD_MASK)) {}
    Value(std::vector<Value>* a) : bits(VALUE_QNAN | TAG_ARRAY | ((uint64_t)a & PAYLOAD_MASK)) {}
    Value(std::vector<std::pair<std::string, Value>>* o) : bits(VALUE_QNAN | TAG_OBJECT | ((uint64_t)o & PAYLOAD_MASK)) {}
    Value(ICallable* f, bool isNative = false) : bits(VALUE_QNAN | (isNative ? TAG_NATIVE_FUNCTION : TAG_FUNCTION) | ((uint64_t)f & PAYLOAD_MASK)) {}
    Value(class SpClass* c) : bits(VALUE_QNAN | TAG_CLASS | ((uint64_t)c & PAYLOAD_MASK)) {}
    Value(class SpInstance* i) : bits(VALUE_QNAN | TAG_INSTANCE | ((uint64_t)i & PAYLOAD_MASK)) {}
    Value(int64_t* b) : bits(VALUE_QNAN | TAG_BIGINT | ((uint64_t)b & PAYLOAD_MASK)) {}
    Value(DateData* d) : bits(VALUE_QNAN | TAG_DATE | ((uint64_t)d & PAYLOAD_MASK)) {}
    Value(MapData* m) : bits(VALUE_QNAN | TAG_MAP | ((uint64_t)m & PAYLOAD_MASK)) {}
    Value(class VMFunction* f) : bits(VALUE_QNAN | TAG_VM_FUNCTION | ((uint64_t)f & PAYLOAD_MASK)) {}
    Value(ErrorData* e) : bits(VALUE_QNAN | TAG_ERROR | ((uint64_t)e & PAYLOAD_MASK)) {}

    static class Interpreter* CurrentContext;
    static void registerString(std::shared_ptr<std::string> s);
    static void registerArray(std::shared_ptr<std::vector<Value>> a);
    static void registerObject(std::shared_ptr<std::vector<std::pair<std::string, Value>>> o);
    static void registerBigInt(std::shared_ptr<int64_t> b);
    static void registerDate(std::shared_ptr<struct DateData> d);
    static void registerMap(std::shared_ptr<struct MapData> m);
    static void registerError(std::shared_ptr<struct ErrorData> e);
    static void registerInstance(std::shared_ptr<class SpInstance> i);
    static void registerClass(std::shared_ptr<class SpClass> c);
    static void registerFunction(std::shared_ptr<ICallable> f);

    inline bool isType(uint64_t tag) const { return (bits & (QNAN_MASK | TAG_MASK)) == (VALUE_QNAN | tag); }

    inline bool isNumber() const { return (bits & QNAN_MASK) != QNAN_MASK; }
    inline bool isNil() const { return bits == (VALUE_QNAN | TAG_NIL); }
    inline bool isUndefined() const { return bits == (VALUE_QNAN | TAG_UNDEFINED); }
    inline bool isBool() const { return isType(TAG_BOOLEAN); }
    inline bool isBoolean() const { return isBool(); } // Alias
    inline bool isString() const { return isType(TAG_STRING); }
    inline bool isObject() const { return isType(TAG_OBJECT) || isMap() || isInstance(); }
    inline bool isArray() const { return isType(TAG_ARRAY); }
    inline bool isFunction() const { return isType(TAG_FUNCTION) || isType(TAG_NATIVE_FUNCTION) || isVMFunction() || isClass(); }
    inline bool isVMFunction() const { return isType(TAG_VM_FUNCTION); } // Alias
    inline bool isNativeFunction() const { return isType(TAG_NATIVE_FUNCTION); }
    inline bool isClass() const { return isType(TAG_CLASS); }
    inline bool isInstance() const { return isType(TAG_INSTANCE); }
    inline bool isBigInt() const { return isType(TAG_BIGINT); }
    inline bool isDate() const { return isType(TAG_DATE); }
    inline bool isMap() const { return isType(TAG_MAP); }
    inline bool isError() const { return isType(TAG_ERROR); }
    
    inline std::string* asString() const { return (std::string*)(bits & PAYLOAD_MASK); }
    inline std::vector<Value>* asArray() const { return (std::vector<Value>*)(bits & PAYLOAD_MASK); }
    inline ErrorData* asError() const { return (ErrorData*)(bits & PAYLOAD_MASK); }
    inline std::vector<std::pair<std::string, Value>>* asObject() const { return (std::vector<std::pair<std::string, Value>>*)(bits & PAYLOAD_MASK); }
    inline ICallable* asFunction() const { return (ICallable*)(bits & PAYLOAD_MASK); }
    inline class SpClass* asClass() const { return (class SpClass*)(bits & PAYLOAD_MASK); }
    inline class SpInstance* asInstance() const { return (class SpInstance*)(bits & PAYLOAD_MASK); }
    inline int64_t* asBigInt() const { return (int64_t*)(bits & PAYLOAD_MASK); }
    inline DateData* asDate() const { return (DateData*)(bits & PAYLOAD_MASK); }
    inline MapData* asMap() const { return (MapData*)(bits & PAYLOAD_MASK); }

    inline double asNumber() const { 
        union { uint64_t u; double d; } u;
        u.u = bits;
        return u.d;
    }
    inline bool asBoolean() const { return bits & 1; }

    // Operator overloads for AOT Transpiler
    std::string toString() const;
    std::string toPureString() const;
    Value operator+(const Value& other) const;
    Value operator-(const Value& other) const { return Value(asNumber() - other.asNumber()); }
    Value operator*(const Value& other) const { return Value(asNumber() * other.asNumber()); }
    Value operator/(const Value& other) const {
        double r = other.asNumber();
        if (r == 0) throw std::runtime_error("Division by zero");
        return Value(asNumber() / r);
    }
    Value operator-() const { return Value(-asNumber()); }
    
    Value operator==(const Value& other) const {
        if (bits == other.bits) return Value(true);
        if (isNil() || isUndefined() || other.isNil() || other.isUndefined()) return Value(false);
        if (isNumber() && other.isNumber()) return Value(asNumber() == other.asNumber());
        if (isBigInt() && other.isBigInt()) return Value(*asBigInt() == *other.asBigInt());
        if (isString() && other.isString()) return Value(*asString() == *other.asString());
        return Value(false);
    }
    Value operator!=(const Value& other) const { return !(*this == other); }
    Value operator<(const Value& other) const { return Value(asNumber() < other.asNumber()); }
    Value operator<=(const Value& other) const { return Value(asNumber() <= other.asNumber()); }
    Value operator>(const Value& other) const { return Value(asNumber() > other.asNumber()); }
    Value operator>=(const Value& other) const { return Value(asNumber() >= other.asNumber()); }
    
    Value operator!() const {
        bool isTrue;
        if (isBool()) {
            isTrue = bits & 1;
        } else {
            isTrue = !(isNil() || isUndefined());
        }
        return Value(!isTrue);
    }

    static bool useColor;
};

// Stream insertion
class NativeFunction : public ICallable {
public:
    using NativeFunc = std::function<Value(class Interpreter&, const std::vector<Value>&)>;
    NativeFunc func;
    NativeFunction(NativeFunc f) : func(std::move(f)) {}
    Value call(class Interpreter& interp, const std::vector<Value>& args) override {
        return func(interp, args);
    }
    bool isNative() const override { return true; }
};

std::ostream& operator<<(std::ostream& os, const Value& val);

struct PropertyDeclaration {
    std::string name;
    std::shared_ptr<class Expression> initializer;
    Value initializer_value;
    bool isPrivate = false;
    bool isReadonly = false;
    int line = -1;
};

struct FunctionDeclaration {
    std::string name;
    std::vector<std::string> parameters;
    std::shared_ptr<class Expression> body;
    int localCount = 0;
    int index = -1;
    bool hasRest = false;
    int line = -1;
};

class SpClass : public ICallable {
public:
    std::string name;
    bool isAbstract;
    std::vector<PropertyDeclaration> properties;
    std::unordered_map<std::string, Value> methods;
    SpClass(std::string n, bool abs)
        : name(n), isAbstract(abs) {}
    
    Value call(Interpreter& interpreter, const std::vector<Value>& args) override;
};

class BoundMethod : public ICallable {
public:
    ICallable* method;
    Value instance;
    BoundMethod(ICallable* m, Value i) : method(m), instance(i) {}
    Value call(Interpreter& interpreter, const std::vector<Value>& args) override;
    bool isBoundMethod() const override { return true; }
};

class SpInstance {
public:
    SpClass* klass;
    std::vector<Value> fields;
    SpInstance(SpClass* k) : klass(k) {
        fields.resize(k->properties.size());
    }
};

#endif