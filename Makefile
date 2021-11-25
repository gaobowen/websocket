override CXXFLAGS += -pthread -lpthread -Wpedantic -Wall -Wextra -Wsign-conversion -Wconversion -std=c++2a -lstdc++
override LDFLAGS += -lz -lssl -lcrypto
override LDFLAGS += -lcurl -ltbb


websock: 
	$(CXX) -o0 -g $(CXXFLAGS) http_parser.cpp Collector.cpp SockHandle.cpp EpollServer.cpp Server.cpp -o Server $(LDFLAGS) 
clean:
	rm -f *.o
	rm -f websock