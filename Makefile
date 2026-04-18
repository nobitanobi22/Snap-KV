CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
SRCS     = src/main.cpp src/server.cpp src/store.cpp src/parser.cpp src/reaper.cpp
TARGET   = snapkv

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -Isrc $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET) snapkv.rdb
