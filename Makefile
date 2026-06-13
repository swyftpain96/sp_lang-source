CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -flto=auto -DCPPHTTPLIB_OPENSSL_SUPPORT
TARGET = sp
SRCS = main.cpp lexer.cpp parser.cpp interpreter.cpp types.cpp resolver.cpp compiler.cpp vm.cpp transpiler.cpp native_net.cpp native_sqlite.cpp native_storage.cpp
OBJS = $(SRCS:.cpp=.o)

ifeq ($(OS),Windows_NT)
    LIBS = -lssl -lcrypto -lsqlite3 -lws2_32 -lcrypt32 -lwsock32
else
    LIBS = -ldl -lssl -lcrypto -pthread -lsqlite3
endif

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)