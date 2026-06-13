#ifndef AST_H
#define AST_H

#include "types.h"
#include <vector>
#include <memory>
#include <string>
#include <variant>

class Interpreter;

class Expression {
public:
    int line = -1;
    virtual ~Expression() = default;
    virtual Value evaluate(Interpreter& interpreter) = 0;
};

class LiteralExpression : public Expression {
public:
    Value value;
    LiteralExpression(Value v) : value(v) {}
    Value evaluate(Interpreter& interpreter) override;
};

class BigIntLiteralExpression : public Expression {
public:
    std::string value;
    BigIntLiteralExpression(std::string v) : value(v) {}
    Value evaluate(Interpreter& interpreter) override;
};

class IdentifierExpression : public Expression {
public:
    std::string name;
    int depth = -1;
    int index = -1;
    IdentifierExpression(std::string n) : name(n) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ThisExpression : public Expression {
public:
    int depth = -1;
    int index = -1;
    ThisExpression() {}
    Value evaluate(Interpreter& interpreter) override;
};

class MemberExpression : public Expression {
public:
    std::shared_ptr<Expression> object;
    std::string property;
    MemberExpression(std::shared_ptr<Expression> obj, std::string prop)
        : object(std::move(obj)), property(std::move(prop)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class IndexExpression : public Expression {
public:
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
    IndexExpression(std::shared_ptr<Expression> obj, std::shared_ptr<Expression> idx)
        : object(std::move(obj)), index(std::move(idx)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class BinaryExpression : public Expression {
public:
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    std::string op;
    int placeholderIndex = -1; // For pipeline placeholder _
    BinaryExpression(std::shared_ptr<Expression> l, std::shared_ptr<Expression> r, std::string o)
        : left(std::move(l)), right(std::move(r)), op(std::move(o)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class SpreadExpression : public Expression {
public:
    std::shared_ptr<Expression> expr;
    SpreadExpression(std::shared_ptr<Expression> e) : expr(std::move(e)) {}
    Value evaluate(Interpreter&) override { throw std::runtime_error("Spread not implemented in AST interpreter"); }
};

class UnaryExpression : public Expression {
public:
    std::shared_ptr<Expression> expr;
    std::string op;
    UnaryExpression(std::shared_ptr<Expression> e, std::string o) : expr(std::move(e)), op(std::move(o)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class AssignmentExpression : public Expression {
public:
    std::string name;
    std::shared_ptr<Expression> value;
    int depth = -1;
    int index = -1;
    AssignmentExpression(std::string n, std::shared_ptr<Expression> v) : name(std::move(n)), value(std::move(v)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class MemberAssignmentExpression : public Expression {
public:
    std::shared_ptr<Expression> object;
    std::string property;
    std::shared_ptr<Expression> value;
    MemberAssignmentExpression(std::shared_ptr<Expression> obj, std::string prop, std::shared_ptr<Expression> v)
        : object(std::move(obj)), property(std::move(prop)), value(std::move(v)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class IndexAssignmentExpression : public Expression {
public:
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
    std::shared_ptr<Expression> value;
    IndexAssignmentExpression(std::shared_ptr<Expression> obj, std::shared_ptr<Expression> idx, std::shared_ptr<Expression> v)
        : object(std::move(obj)), index(std::move(idx)), value(std::move(v)) {}
    Value evaluate(Interpreter& interpreter) override;
};

struct DestructuringBinding {
    std::string name;             // variable name if this is a leaf binding
    std::string propertyName;     // for object { prop: name }
    int arrayIndex = -1;         // for array [a, b]
    bool isRest = false;         // for ...rest
    std::vector<DestructuringBinding> nested; // for nested patterns: { user: { name } }
    int index = -1;              // resolver assigned slot
    int depth = -1;              // resolver assigned scope depth
};

class DestructuringAssignment : public Expression {
public:
    std::vector<DestructuringBinding> bindings;
    std::shared_ptr<Expression> value;
    DestructuringAssignment(std::vector<DestructuringBinding> b, std::shared_ptr<Expression> v) : bindings(std::move(b)), value(std::move(v)) {}
    Value evaluate(Interpreter&) override { throw std::runtime_error("Destructuring assignment not implemented in AST interpreter"); }
};

class ArrayExpression : public Expression {
public:
    std::vector<std::shared_ptr<Expression>> elements;
    ArrayExpression(std::vector<std::shared_ptr<Expression>> e) : elements(std::move(e)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ObjectExpression : public Expression {
public:
    std::vector<std::pair<std::string, std::shared_ptr<Expression>>> properties;
    ObjectExpression(std::vector<std::pair<std::string, std::shared_ptr<Expression>>> p) : properties(std::move(p)) {}
    Value evaluate(Interpreter& interpreter) override;
};

struct VariableDeclaration {
    std::string name;
    std::shared_ptr<Expression> value;
    TypeAnnotation type;
    int index = -1;
    bool isMutable = false; // true for 'var', false for 'set'
    int line = -1;
};

struct DestructuringDeclaration {
    bool isMutable = false;
    std::vector<DestructuringBinding> bindings;
    std::shared_ptr<Expression> initializer;
    int line = -1;
};

struct PrintStatement {
    std::vector<std::shared_ptr<Expression>> exprs;
    int line = -1;
};

struct WarnStatement {
    std::vector<std::shared_ptr<Expression>> exprs;
    int line = -1;
};

struct LayoutDeclaration {
    std::string name;
    std::vector<PropertyDeclaration> properties;
    TypeAnnotation aliasType;
    int line = -1;
};

struct ClassDeclaration {
    std::string name;
    bool isAbstract = false;
    std::vector<PropertyDeclaration> properties;
    std::vector<FunctionDeclaration> methods;
    int line = -1;
};

class LayoutExpression : public Expression {
public:
    std::vector<PropertyDeclaration> properties;
    LayoutExpression(std::vector<PropertyDeclaration> p) : properties(std::move(p)) {}
    Value evaluate(Interpreter& interpreter) override;
};


class IfExpression : public Expression {
public:
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> thenBranch;
    std::shared_ptr<Expression> elseBranch;
    IfExpression(std::shared_ptr<Expression> c, std::shared_ptr<Expression> t, std::shared_ptr<Expression> e) : condition(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
    Value evaluate(Interpreter& interpreter) override;
};

struct ReturnStatement {
    std::shared_ptr<Expression> value;
    int line = -1;
};

struct MatchCase {
    std::shared_ptr<Expression> pattern; // nullptr indicates default
    std::shared_ptr<Expression> body;
};

class MatchExpression : public Expression {
public:
    std::shared_ptr<Expression> valueToMatch;
    std::vector<MatchCase> cases;
    MatchExpression(std::shared_ptr<Expression> val, std::vector<MatchCase> c)
        : valueToMatch(std::move(val)), cases(std::move(c)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ProcessRunExpression : public Expression {
public:
    std::shared_ptr<Expression> command;
    std::vector<std::shared_ptr<Expression>> arguments;
    ProcessRunExpression(std::shared_ptr<Expression> cmd, std::vector<std::shared_ptr<Expression>> args)
        : command(std::move(cmd)), arguments(std::move(args)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ProcessSpawnExpression : public Expression {
public:
    std::shared_ptr<Expression> command;
    std::vector<std::shared_ptr<Expression>> arguments;
    ProcessSpawnExpression(std::shared_ptr<Expression> cmd, std::vector<std::shared_ptr<Expression>> args)
        : command(std::move(cmd)), arguments(std::move(args)) {}
    Value evaluate(Interpreter& interpreter) override;
};
class ProcessSleepExpression : public Expression {
public:
    std::shared_ptr<Expression> delay;
    ProcessSleepExpression(std::shared_ptr<Expression> d) : delay(std::move(d)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSReadExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    FSReadExpression(std::shared_ptr<Expression> p)
        : path(std::move(p)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSInfoExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    FSInfoExpression(std::shared_ptr<Expression> path) : path(std::move(path)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSCreateExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    std::shared_ptr<Expression> content;
    FSCreateExpression(std::shared_ptr<Expression> p, std::shared_ptr<Expression> c)
        : path(std::move(p)), content(std::move(c)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSOverwriteExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    std::shared_ptr<Expression> content;
    std::shared_ptr<Expression> options;
    FSOverwriteExpression(std::shared_ptr<Expression> p, std::shared_ptr<Expression> c, std::shared_ptr<Expression> opts = nullptr)
        : path(std::move(p)), content(std::move(c)), options(std::move(opts)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSAppendExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    std::shared_ptr<Expression> content;
    std::shared_ptr<Expression> options;
    FSAppendExpression(std::shared_ptr<Expression> p, std::shared_ptr<Expression> c, std::shared_ptr<Expression> opts = nullptr)
        : path(std::move(p)), content(std::move(c)), options(std::move(opts)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSDeleteExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    FSDeleteExpression(std::shared_ptr<Expression> p)
        : path(std::move(p)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSReadJsonExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    FSReadJsonExpression(std::shared_ptr<Expression> p)
        : path(std::move(p)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class FSWriteJsonExpression : public Expression {
public:
    std::shared_ptr<Expression> path;
    std::shared_ptr<Expression> value;
    FSWriteJsonExpression(std::shared_ptr<Expression> p, std::shared_ptr<Expression> v)
        : path(std::move(p)), value(std::move(v)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ConsoleArgsExpression : public Expression {
public:
    std::shared_ptr<Expression> schema;
    ConsoleArgsExpression(std::shared_ptr<Expression> s) : schema(std::move(s)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ConsoleReadExpression : public Expression {
public:
    std::shared_ptr<Expression> options;
    ConsoleReadExpression(std::shared_ptr<Expression> o) : options(std::move(o)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ConsoleShowExpression : public Expression {
public:
    std::vector<std::shared_ptr<Expression>> arguments;
    ConsoleShowExpression(std::vector<std::shared_ptr<Expression>> args) : arguments(std::move(args)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class ConsoleWarnExpression : public Expression {
public:
    std::vector<std::shared_ptr<Expression>> arguments;
    ConsoleWarnExpression(std::vector<std::shared_ptr<Expression>> args) : arguments(std::move(args)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class TrimExpression : public Expression {
public:
    std::shared_ptr<Expression> argument;
    TrimExpression(std::shared_ptr<Expression> arg) : argument(std::move(arg)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class StringSizeExpression : public Expression {
public:
    std::shared_ptr<Expression> argument;
    StringSizeExpression(std::shared_ptr<Expression> arg) : argument(std::move(arg)) {}
    Value evaluate(Interpreter& interpreter) override;
};

struct UseStatement {
    std::string moduleName;
    std::string alias;                                            // "use math as m"
    std::vector<std::pair<std::string,std::string>> namedImports; // {exportName, localAlias}
    bool isNamed = false;                                         // true for { } from form
    int line = -1;
};

struct ExportStatement {
    std::variant<VariableDeclaration, DestructuringDeclaration, FunctionDeclaration, LayoutDeclaration> declaration;
    int line = -1;
};

struct WhileStatement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> body;
    int line = -1;
};

struct ForStatement {
    std::string variableName;
    std::shared_ptr<Expression> collection;
    std::shared_ptr<Expression> body;
    int index = -1;
    int localCount = 0;
    int line = -1;
};


using Statement = std::variant<VariableDeclaration, DestructuringDeclaration, PrintStatement, WarnStatement, FunctionDeclaration, ReturnStatement, UseStatement, std::shared_ptr<Expression>, ExportStatement, WhileStatement, ForStatement, ClassDeclaration, LayoutDeclaration>;

class LambdaExpression : public Expression {
public:
    std::vector<std::pair<std::string, TypeAnnotation>> parameters;
    std::shared_ptr<Expression> body;
    TypeAnnotation returnType;
    int localCount = 0;
    int index = -1;
    bool hasRest = false;
    LambdaExpression(std::vector<std::pair<std::string, TypeAnnotation>> p, std::shared_ptr<Expression> b, bool r, TypeAnnotation ret = TypeAnnotation())
        : parameters(std::move(p)), body(std::move(b)), hasRest(r), returnType(std::move(ret)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class BlockExpression : public Expression {
public:
    std::vector<Statement> statements;
    int localCount = 0;
    BlockExpression(std::vector<Statement> stmts) : statements(std::move(stmts)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class CallExpression : public Expression {
public:
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> arguments;
    CallExpression(std::shared_ptr<Expression> c, std::vector<std::shared_ptr<Expression>> args) : callee(std::move(c)), arguments(std::move(args)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class AsyncExpression : public Expression {
public:
    std::shared_ptr<Expression> body;
    explicit AsyncExpression(std::shared_ptr<Expression> b) : body(std::move(b)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class AfterExpression : public Expression {
public:
    std::shared_ptr<Expression> delay;
    std::shared_ptr<Expression> body;
    AfterExpression(std::shared_ptr<Expression> d, std::shared_ptr<Expression> b)
        : delay(std::move(d)), body(std::move(b)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class EveryExpression : public Expression {
public:
    std::shared_ptr<Expression> delay;
    std::shared_ptr<Expression> body;
    EveryExpression(std::shared_ptr<Expression> d, std::shared_ptr<Expression> b)
        : delay(std::move(d)), body(std::move(b)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class AsExpression : public Expression {
public:
    std::shared_ptr<Expression> left;
    TypeAnnotation type;
    AsExpression(std::shared_ptr<Expression> l, TypeAnnotation t) : left(std::move(l)), type(std::move(t)) {}
    Value evaluate(Interpreter& interpreter) override;
};

class TypeofExpression : public Expression {
public:
    std::shared_ptr<Expression> expr;
    TypeofExpression(std::shared_ptr<Expression> e) : expr(std::move(e)) {}
    Value evaluate(Interpreter& interpreter) override;
};

#endif