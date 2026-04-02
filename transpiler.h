#ifndef TRANSPILER_H
#define TRANSPILER_H

#include "ast.h"
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>

class Transpiler {
public:
  Transpiler() = default;

  std::string transpile(const std::vector<std::shared_ptr<Statement>> &ast);

private:
  std::string generateExpression(Expression *expr,
                                 std::ostringstream &outStream);
  // The instruction implies generateStatement should take a stream reference.
  // The existing declaration already takes `std::ostringstream& outStream`.
  // The instruction "Refactor transpiler.cpp to ensure generateStatement writes
  // directly to a target stream reference rather than hardcoding 'mainCode'"
  // refers to the *implementation* of generateStatement, which is not in this
  // .h file. No change is needed in the .h file based on the instruction's
  // description of the change.
  void generateStatement(Statement *stmt, std::ostringstream &outStream);

  std::ostringstream globalsCode;
  std::ostringstream functionsCode;
  std::ostringstream forwardDecls;
  std::ostringstream mainCode;
  std::unordered_set<std::string> variables;

  int indentLevel = 1;
  bool inFunction = false;
  std::string indent() { return std::string(indentLevel * 4, ' '); }
};

#endif
