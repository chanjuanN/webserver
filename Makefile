CXX = g++
CFLAGS += -g -Wno-return-type -Wno-format-truncation

TARGET = server
OBJS = ./src/*.cpp
INCLUDE = -I./include $(shell find ./include -type d -exec echo -I{} \;)

server: $(OBJS) 
		$(CXX) $(CFLAGS) $(INCLUDE) $(OBJS) -o ./$(TARGET) -lpthread -lmysqlclient
clean:
	rm -r server

