#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <chrono>
#include <cmath>
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "compiler.h"
#include "vm.h"
#include "interpreter.h"
#include "transpiler.h"

int main(int argc, char* argv[]) {
    fflush(stdout);
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << argv[1] << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    bool useAot = false;
    bool useVm = true;
    bool showWarnings = true;
    bool useColor = true;
    std::vector<std::string> cliArgs;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--aot") {
            useAot = true;
        } else if (arg == "--interpreter") {
            useVm = false;
        } else if (arg == "--vm") {
            useVm = true;
        } else if (arg == "--no-warnings") {
            showWarnings = false;
        } else if (arg == "--no-color") {
            useColor = false;
        } else {
            cliArgs.push_back(arg);
        }
    }
    fflush(stdout);

    try {
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();

        Parser parser(tokens);
        std::vector<Statement> statements = parser.parse();

        Resolver resolver;
        resolver.resolveModule(statements);

        if (useAot) {
            std::vector<std::shared_ptr<Statement>> sharedStatements;
            for (auto& s : statements) {
                sharedStatements.push_back(std::make_shared<Statement>(std::move(s)));
            }
            Transpiler transpiler;
            std::string cppCode = transpiler.transpile(sharedStatements);
            std::ofstream outFile("out.cpp");
            outFile << cppCode;
            outFile.close();
            int ret = system("g++ -std=c++17 -Wall -Wextra -O3 -march=native -flto=auto out.cpp types.o interpreter.o vm.o lexer.o parser.o resolver.o compiler.o -o out_bin");
            if (ret != 0) {
                std::cerr << "Native compilation failed." << std::endl;
                return ret;
            }
            std::string runCmd = "./out_bin";
            for (const auto& arg : cliArgs) runCmd += " \"" + arg + "\"";
            return system(runCmd.c_str());
        }

        VM vm;
        vm.showWarnings = showWarnings;
        vm.useColor = useColor;
        vm.cliArgs = cliArgs;
        
        Interpreter interpreter(&vm);
        interpreter.cliArgs = cliArgs;
        Value::CurrentContext = &interpreter;
        
        if (!useVm) {
            interpreter.interpret(statements);
            return 0;
        }

        Compiler compiler;
        CompilationResult result = compiler.compile("main", statements, resolver.mainLocalCount);
        auto timeFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>&) {
            auto now = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
            return Value((double)duration.count());
        });
        Value::registerFunction(timeFunc);
        vm.defineGlobal("time", Value(timeFunc.get(), true));

        auto floorFuncReal = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
            if (args.empty()) return Value(0.0);
            return Value(std::floor(args[0].asNumber()));
        });
        Value::registerFunction(floorFuncReal);
        vm.defineGlobal("floor", Value(floorFuncReal.get(), true));

        auto rangeFuncNative = std::make_shared<NativeFunction>([](Interpreter& interpreter, const std::vector<Value>& args) {
            if (args.size() != 2 || !args[0].isNumber() || !args[1].isNumber()) {
                throw std::runtime_error("range expects 2 numeric arguments (start, end)");
            }
            int start = (int)args[0].asNumber();
            int end = (int)args[1].asNumber();
            auto* array = interpreter.makeArray();
            for (int i = start; i < end; ++i) {
                array->push_back(Value((double)i));
            }
            return Value(array);
        });
        Value::registerFunction(rangeFuncNative);
        vm.defineGlobal("range", Value(rangeFuncNative.get(), true));

        auto consoleShowFunc = std::make_shared<NativeFunction>([](Interpreter&, const std::vector<Value>& args) {
            Value last;
            for (size_t i = 0; i < args.size(); ++i) {
                last = args[i];
                std::cout << last.toString();
                if (i < args.size() - 1) std::cout << " ";
            }
            std::cout << std::endl;
            return last;
        });
        auto consoleArgsFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            fprintf(stdout, "[DEBUG] console.args() called, cliArgs size: %zu\n", interp.cliArgs.size());
            fflush(stdout);
            auto* arr = interp.makeArray();
            for (const auto& a : interp.cliArgs) arr->push_back(Value(interp.makeString(a)));
            return Value(arr);
        });
        Value::registerFunction(consoleArgsFunc);

        auto consoleReadFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            std::string line;
            if (std::getline(std::cin, line)) return Value(interp.makeString(line));
            return Value(Type::NULL_VAL);
        });
        Value::registerFunction(consoleReadFunc);

        auto consoleWarnFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>& args) {
            (void)interp;
            for (size_t i = 0; i < args.size(); ++i) {
                std::cerr << "\033[33m[Warning] \033[0m" << args[i].toString();
                if (i < args.size() - 1) std::cerr << " ";
            }
            std::cerr << std::endl;
            return args.empty() ? Value(Type::NULL_VAL) : args.back();
        });
        Value::registerFunction(consoleWarnFunc);

        auto* cObj = interpreter.makeObject();
        cObj->push_back({"show", Value(consoleShowFunc.get(), true)});
        cObj->push_back({"args", Value(consoleArgsFunc.get(), true)});
        cObj->push_back({"read", Value(consoleReadFunc.get(), true)});
        cObj->push_back({"warn", Value(consoleWarnFunc.get(), true)});
        Value consoleVal(cObj);
        vm.defineGlobal("console", consoleVal);
        interpreter.environment->define("console", consoleVal);

        auto dateNowFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return Value(interp.makeDate(ms));
        });
        Value::registerFunction(dateNowFunc);
        auto* dObj = interpreter.makeObject();
        dObj->push_back({"now", Value(dateNowFunc.get(), true)});
        Value dateVal(dObj);
        vm.defineGlobal("Date", dateVal);
        interpreter.environment->define("Date", dateVal);

        auto mapConstructorFunc = std::make_shared<NativeFunction>([](Interpreter& interp, const std::vector<Value>&) {
            return Value(interp.makeMap());
        });
        vm.defineGlobal("Map", Value(mapConstructorFunc.get(), true));
        vm.defineGlobal("HashMap", Value(mapConstructorFunc.get(), true));

        vm.allStrings = std::move(result.allStrings);
        vm.allBigInts = std::move(result.allBigInts);

        if (!result.mainFunction) {
            std::cerr << "Compilation failed: no main function" << std::endl;
            return 1;
        }
        interpreter.callHandler = [&](ICallable* f, const std::vector<Value>& args) {
            return vm.call(interpreter, f, args);
        };
        
        vm.run(result.mainFunction.get(), interpreter);
    } catch (const std::exception& e) {
        if (useColor) std::cerr << "\033[31m\033[1m[Error] \033[0m\033[31m" << e.what() << "\033[0m" << std::endl;
        else std::cerr << "[Error] " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}