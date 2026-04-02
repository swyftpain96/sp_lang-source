CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -flto=auto
TARGET = sp
SRCS = main.cpp lexer.cpp parser.cpp interpreter.cpp types.cpp resolver.cpp compiler.cpp vm.cpp transpiler.cpp
OBJS = $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -static -o $(TARGET) $(OBJS) -ldl

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)