#include "SockHandle.h"
#include "Collector.h"

#include <uv.h>
#include <string.h>
#include <tbb/concurrent_unordered_set.h>

#define PORT 9001

using namespace GWS;
using namespace std;


auto openHandle = [](std::shared_ptr<SockHandle> sock){
    printf("handle open %p \n", sock.get());
    printf("handle open path %s \n", sock->path.c_str());
    auto it = sock->query.begin();
    while(it!=sock->query.end()){
        printf("handle open query key=%s value=%s \n", it->first.c_str(), it->second.c_str());
        it++;
    }
};

auto messageHandle = [](std::shared_ptr<SockHandle> sock, const char* msg, size_t len, int opCode){
    GWS::SockHandle::SendBuffer(sock.get(), (char*)msg, len, opCode);
};

auto closeHandle = [](std::shared_ptr<SockHandle> sock){
    printf("handle close %p \n", sock.get());
};




void on_connection(uv_stream_t* server, int status);

int main(int argc, char** argv) {
    uv_tcp_t tcpServer;
    auto loop = uv_default_loop();
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", PORT, &addr);
    int r;
    r = uv_tcp_init(loop, &tcpServer);
    if (r) {
        fprintf(stderr, "Socket creation error\n");
        return 1;
    }
    r = uv_tcp_bind(&tcpServer, (const struct sockaddr*)&addr, 0);
    if (r) {
        fprintf(stderr, "Bind error\n");
        return 1;
    }
    r = uv_listen((uv_stream_t*)&tcpServer, 4096, on_connection);
    if (r) {
        fprintf(stderr, "Listen error\n");
        return 1;
    }
    printf("Listening port %d \n", PORT);
    uv_run(loop, (uv_run_mode)0);
    return 0;
}


void read_cb(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf) {
    //printf("========recv=======bytes=%ld\n", nread);
    //printf("uv_is_readable(client)=%ld\n", uv_is_readable(client));
    //UV_HANDLE_READABLE
    auto handle = (SockHandle*)client->data;
    if (!handle) {
        uv_close((uv_handle_t*)client, 0);
        if (buf->base != NULL) {
            free(buf->base);
        }
        return;
    }

    if (nread > 0) {
        handle->ReadBuffer(buf->base, nread);
    }
    else if (nread < 0) {
        if (nread != UV_EOF) {
            //printf("Read error %s\n", uv_err_name(nread));
        }
        else {
            //printf("client disconnect\n");
        }
        uv_close((uv_handle_t*)client, SockHandle::tcp_close);
    }
    else if (nread == 0) {
        //printf("nread == 0\n");
    }


    if (buf->base != NULL) {
        free(buf->base);
    }
}

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    //printf("alloc_buffer suggested_size=%d buf->len=%d\n", suggested_size, buf->len);
    buf->len = suggested_size;
    buf->base = static_cast<char*>(malloc(suggested_size));

}

void on_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    auto loop = uv_default_loop();
    uv_tcp_init(loop, client);

    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        //在on_close中释放
        auto handle = new GWS::SockHandle();
        handle->SetClient(client);
        client->data = handle;

        handle->OnOpen = openHandle;
        handle->OnMessage = messageHandle;
        handle->OnClosed = closeHandle;

        SockAccessor accessor;
        SockPair pair(handle, shared_ptr<SockHandle>(handle));
        GWS::collector.insert(accessor, pair);
        accessor.release();

        uv_read_start((uv_stream_t*)client, alloc_buffer, read_cb);
        printf("on_connection\n");
    }
    else {
        uv_close((uv_handle_t*)client, 0);
        printf("on_connection close\n");
    }
}