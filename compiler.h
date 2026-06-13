#ifndef COMPILER_H
#define COMPILER_H

#include "ast.h"
#include "vm.h"
#include <memory>
#include <vector>

struct CompilationResult {
    std::shared_ptr<VMFunction> mainFunction;
    std::vector<std::shared_ptr<VMFunction>> allFunctions;
};

class Compiler {
public:
    CompilationResult compile(const std::string& name, const std::vector<Statement>& statements, int localCount = 0);

private:
    void compile(const std::vector<Statement>& statements);
    void compile(const Statement& stmt);
    void compile(std::shared_ptr<Expression> expr);
    void compileDestructuring(const std::vector<DestructuringBinding>& bindings, bool isMutable);
    
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t b1, uint8_t b2);
    void emitConstant(Value value);
    int emitJump(OpCode instruction);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    std::shared_ptr<VMFunction> currentFunction;
    std::vector<std::shared_ptr<VMFunction>> allFunctions;
    std::string currentModulePrefix = "";
    std::unordered_map<std::string, std::string> importedModules; // bindName -> moduleName
    int currentLine = -1;
};

#endif
