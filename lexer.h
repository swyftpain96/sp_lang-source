#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>

enum class TokenType {
    SET,
    VAR,
    DEFINE,
    IF,
    ELSE,
    RETURN,
    MATCH,
    DEFAULT,
    USE,
    EXPORT,
    IDENTIFIER,
    WHILE,
    FOR,
    IN,
    AS,
    FROM,
    EQUALS,
    NUMBER,
    STRING,
    BOOLEAN,
    LBRACKET,
    RBRACKET,
    LBRACE,
    RBRACE,
    COLON,
    COMMA,
    DOT,
    DOT_DOT_DOT,
    PIPE,
    LPAREN,
    RPAREN,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    CONSOLE,
    SHOW,
    WARN,
    ARGS,
    READ,
    NULL_VAL,
    UNDEFINED,
    EQUALS_EQUALS,
    NOT_EQUALS,
    LESS,
    LESS_EQUALS,
    GREATER,
    GREATER_EQUALS,
    AND,
    OR,
    NOT,
    ARROW,
    CLASS,
    ABSTRACT,
    PRIVATE,
    READONLY,
    THIS,
    PROCESS,
    RUN,
    SPAWN,
    FS,
    CREATE,
    OVERWRITE,
    APPEND,
    DELETE,
    FS_READ,
    PATH,
    DIRNAME,
    NAME,
    EXT,
    INFO,
    READ_JSON,
    WRITE_JSON,
    STRING_KEYWORD,
    TRIM,
    SIZE,
    LENGTH,
    BIGINT_LITERAL,
    END
};

struct Token {
    TokenType type;
    std::string value;
    int line;
};

class Lexer {
public:
    Lexer(const std::string& source);
    std::vector<Token> tokenize();

private:
    std::string source;
    size_t pos;
    int line;

    char peek();
    char advance();
    void skipWhitespace();
    Token number();
    Token string();
    Token identifier();
};

#endif