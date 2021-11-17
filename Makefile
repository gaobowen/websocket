override CXXFLAGS += -pthread -lpthread -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++2a
override LDFLAGS += -lz -lssl -lcrypto
override LDFLAGS += -luv -lcurl -ltbb
# -fsanitize=address
websock: 
	$(CXX) -o0  -g $(CXXFLAGS) http_parser.c Collector.cpp SockHandle.cpp Server.cpp -o Server $(LDFLAGS)

clean:
	rm -f *.o
	rm -f websock