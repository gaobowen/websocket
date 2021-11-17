#ifndef GWS_SOCKHANDLE_H
#define GWS_SOCKHANDLE_H

#include <uv.h>
#include <functional>
#include <vector>
#include <string_view>
#include <map>
#include <mutex>
#include <atomic>

#include "concurrentqueue.h"
#include "http_parser.h"
#include "Utils.h"

using namespace std;

#define ON_MESSAGE_FREE 1

namespace GWS {

    typedef typename moodycamel::ConcurrentQueue<tuple<char*, size_t>> SendQueue;

    class SockHandle
    {
    private:
        bool isHandshaked = false;
        bool canWSKey = false;
        string wsKey = "";
        vector<tuple<char*, size_t>> frames;
        size_t frames_total = 0;
        long remainBytes = 0;
        unsigned char* payLoadData = nullptr;
        WSHeader wsHeader;
        //uv_rwlock_t rwlock;
        //std::mutex mutex;
        int opCode;
        uv_async_t async_handle;
        void finish();
        void addSendTask(char* buff, size_t len);
    public:
        SockHandle();
        ~SockHandle();
        string path;
        map<string, string> query;
        std::atomic_bool is_closed;
        SendQueue sendQueue;
        void* client;
        void* data;
        void ReadBuffer(char* buff, size_t len);
        static void SendBuffer(SockHandle* handle, char* buff, size_t len, int opCode);
        static void CloseHandle(SockHandle* handle);
        void SetClient(void* client);
        std::function<void(std::shared_ptr<SockHandle> sock)> OnOpen;
        std::function<void(std::shared_ptr<SockHandle> sock, const char* msg, size_t len, int opCode)> OnMessage;
        std::function<void(std::shared_ptr<SockHandle> sock)> OnClosed;
        static int message_begin_cb(http_parser* p);
        static int url_cb(http_parser* p, const char* at, size_t length);
        static int status_cb(http_parser* p, const char* at, size_t length);
        static int header_field_cb(http_parser* p, const char* buf, size_t len);
        static int header_value_cb(http_parser* p, const char* buf, size_t len);
        static int headers_complete_cb(http_parser* p);
        static int body_cb(http_parser* p, const char* at, size_t length);
        static int message_complete_cb(http_parser* p);
        static int chunk_header_cb(http_parser* p);
        static int chunk_complete_cb(http_parser* p);
        static void tcp_shutdown(uv_shutdown_t* req, int status);
        static void tcp_close(uv_handle_t* client);
        static http_parser_settings settings;
    };

}

#endif