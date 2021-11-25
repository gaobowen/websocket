#include "SockHandle.h"
#include "Collector.h"
#include "EpollServer.h"

#include <uv.h>
#include <string.h>

#define PORT 9001

using namespace GWS;
using namespace std;


auto openHandle = [](std::shared_ptr<SockHandle> sock) {
    printf("handle open %p \n", sock.get());
    printf("handle open path %s \n", sock->path.c_str());
    auto it = sock->query.begin();
    while (it != sock->query.end()) {
        printf("handle open query key=%s value=%s \n", it->first.c_str(), it->second.c_str());
        it++;
    }

    sock->data = malloc(1);

    sock->OnDispose = [](GWS::SockHandle* handle) {
        if (handle->data) {
            free(handle->data);
        }
    };


};

auto messageHandle = [](std::shared_ptr<SockHandle> sock, const char* msg, size_t len, int opCode) {
    GWS::SockHandle::SendBuffer(sock.get(), (char*)msg, len, opCode);
};

auto closeHandle = [](std::shared_ptr<SockHandle> sock) {
    printf("handle close %p \n", sock.get());
};



int main(int argc, char** argv) {

    EpollServer server(openHandle, messageHandle, closeHandle);

    server.Run((char*)"9001");

    return 0;
}

