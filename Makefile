CXX      = g++
CXXFLAGS = -std=c++17 -Wall -O2 -Iinclude -Ithird_party
LDLIBS   = -pthread -lcurl

TARGET = xerxes_backend
SRCS   = $(wildcard src/*.cpp)

$(TARGET): $(SRCS) $(wildcard include/*.h)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
