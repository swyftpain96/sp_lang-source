CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -flto=auto
TARGET = sp
SRCS = main.cpp lexer.cpp parser.cpp interpreter.cpp types.cpp resolver.cpp compiler.cpp vm.cpp transpiler.cpp
OBJS = $(SRCS:.cpp=.o)

ifneq ($(OS),Windows_NT)
    LIBS = -ldl
endif

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)