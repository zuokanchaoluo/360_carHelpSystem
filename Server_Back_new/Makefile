# 360Car Back Server Makefile

CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2 -pthread
LDFLAGS = -lpthread -lmysqlclient

TARGET = server_back
SOURCES = main.cpp ipc.cpp DBConnection.cpp dbpool.cpp OperationFactory.cpp UserOperation.cpp VideoOperation.cpp ImageOperation.cpp SmsClient.cpp
OBJECTS = $(SOURCES:.cpp=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

run: $(TARGET)
	./$(TARGET)
