#include "types.h"
#include "interpreter.h"
#include <vector>
#include <stdexcept>

extern "C" {
    uint64_t divide(Interpreter& interp, const std::vector<Value>& args) {
        if (args.size() < 2) {
            throw std::runtime_error("native divide expects 2 arguments");
        }
        if (!args[0].isNumber() || !args[1].isNumber()) {
            throw std::runtime_error("native divide expects numeric arguments");
        }
        return Value(args[0].asNumber() / args[1].asNumber()).bits;
    }
}
