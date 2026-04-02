#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"
#include <vector>

class Parser {
public:
    Parser(const std::vector<Token>& tokens);
    std::vector<Statement> parse();
    bool isAtEnd() const { return pos >= tokens.size() || tokens[pos].type == TokenType::END; }

private:
    std::vector<Token> tokens;
    size_t pos;

    Statement statement();
    Statement variableDeclaration(bool isMutable = false);
    std::vector<DestructuringBinding> parseDestructuringPattern();
    Statement functionDeclaration();
    Statement classDeclaration(bool isAbstract);
    std::shared_ptr<Expression> ifExpr();
    Statement returnStatement();
    Statement useStatement();
    Statement exportStatement();
    Statement whileStatement();
    Statement forStatement();
    Statement printStatement();
    std::shared_ptr<Expression> expression();
    std::shared_ptr<Expression> assignment();
    std::shared_ptr<Expression> block();
    std::shared_ptr<Expression> pipeline();
    std::shared_ptr<Expression> logicOr();
    std::shared_ptr<Expression> logicAnd();
    std::shared_ptr<Expression> equality();
    std::shared_ptr<Expression> comparison();
    std::shared_ptr<Expression> term();
    std::shared_ptr<Expression> factor();
    std::shared_ptr<Expression> unary();
    std::shared_ptr<Expression> primary();
    std::shared_ptr<Expression> array();
    std::shared_ptr<Expression> object();
    std::shared_ptr<Expression> matchExpr();
    std::shared_ptr<Expression> parseInterpolatedString(const std::string& val);
    std::shared_ptr<Expression> consoleExpr();
    std::shared_ptr<Expression> processExpr();
    std::shared_ptr<Expression> fsExpr();
    std::shared_ptr<Expression> stringExpr();

    template<typename T>
    std::shared_ptr<T> addLine(std::shared_ptr<T> expr, int line) {
        if (expr) expr->line = line;
        return expr;
    }

    Statement addLine(Statement stmt, int line);

    Token current();
    Token advance();
    bool match(TokenType type);
    bool check(TokenType type);
    bool isIdentifierLike(TokenType type);
};

#endif