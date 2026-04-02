#ifndef VM_H
#define VM_H

#include "types.h"
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <memory>

enum class OpCode : uint8_t {
    CONSTANT,
    NULL_VAL,
    TRUE_VAL,
    FALSE_VAL,
    POP,
    GET_LOCAL,
    SET_LOCAL,
    GET_GLOBAL,
    DEFINE_GLOBAL,
    SET_GLOBAL,
    GET_GLOBAL_FAST,
    SET_GLOBAL_FAST,
    GET_PROPERTY,
    SET_PROPERTY,
    MAKE_OBJECT,
    GET_ELEMENT,
    MAKE_ARRAY,
    EQUAL,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    FS_READ,
    TRIM,
    STRING_SIZE,
    NOT,
    JUMP,
    JUMP_IF_FALSE,
    CALL,
    RETURN,
    PRINT,
    DUP,
    LOOP,
    FOR_ITER,
    APPEND_ARRAY,
    APPEND_ELEMENT,
    MERGE_OBJECT,
    MERGE_PROPERTY,
    SLICE_ARRAY,
    REST_OBJECT,
    SPREAD_ARGS,
    IMPORT_NATIVE,
    WARN,
    CONSOLE_ARGS,
    CONSOLE_READ,
    CLASS,
    PROCESS_RUN,
    PROCESS_SPAWN,
    FS_CREATE,
    FS_OVERWRITE,
    FS_APPEND,
    FS_DELETE,
    FS_INFO,
    FS_READ_JSON,
    FS_WRITE_JSON
};

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<LineInfo> lineInfo;
};

class VMFunction : public ICallable {
public:
    Chunk chunk;
    int arity;
    int localCount;
    std::string name;
    bool hasRest = false;

    VMFunction(std::string name, int arity, int localCount) 
        : arity(arity), localCount(localCount), name(std::move(name)) {}

    Value call(class Interpreter&, const std::vector<Value>&) override {
        return Value();
    }
    bool isVMFunction() const override { return true; }
};

const int STACK_MAX = 1024 * 1024;
const int FRAMES_MAX = 64 * 1024;

class VM {
public:
    struct CallFrame {
        VMFunction* function;
        uint8_t* ip;
        int stackBase;
    };

    VM();
    ~VM();

    void run(VMFunction* mainFunction, class Interpreter& interpreter);
    void defineGlobal(const std::string& name, Value val);
    Value call(class Interpreter& interpreter, ICallable* callable, const std::vector<Value>& args);
    
    void runtimeError(const std::string& message, int line = -1);
    Value pushError(const std::string& message, int line = -1);
    int getLine(CallFrame* frame, uint8_t* ip);

    // Context & Config
    bool showWarnings = true;
    bool useColor = true;
    std::vector<std::string> cliArgs;

    // Tracking for different heap-allocated types
    std::vector<std::shared_ptr<std::string>> allStrings;
    std::vector<std::shared_ptr<std::vector<std::pair<std::string, Value>>>> allObjects;
    std::vector<std::shared_ptr<std::vector<Value>>> allArrays;
    std::vector<std::shared_ptr<ICallable>> allFunctions;
    std::vector<std::shared_ptr<int64_t>> allBigInts;
    std::vector<std::shared_ptr<DateData>> allDates;
    std::vector<std::shared_ptr<MapData>> allMaps;
    std::vector<std::shared_ptr<ErrorData>> allErrors;
    std::vector<std::shared_ptr<class SpInstance>> allInstances;

    // Global management
    std::unordered_map<std::string, int> globalIndices;
    std::unordered_map<std::string, bool> globalMutability;
    std::vector<Value> globals;
    Value* globalsPtr;

private:
    void ensureInitialized();
    void runLoop(int stopFrameIndex, class Interpreter& interpreter);

    // Runtime state (hot pointers)
    Value* sp;
    CallFrame* frame;
    Value* fp;

    // Heap-allocated buffers (managed by unique_ptr)
    std::unique_ptr<Value[]> stack;
    std::unique_ptr<CallFrame[]> frames;
};

#endif
