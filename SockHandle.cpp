#include "SockHandle.h"
#include "Utils.h"
#include "Collector.h"

#include <string.h>
#include <openssl/sha.h>
#include <cstring>
#include <math.h>
#include <assert.h>
#include <tuple>
#include <thread>

using namespace std;

namespace GWS {

#pragma region 

    http_parser_settings SockHandle::settings =
    {
          .on_message_begin = SockHandle::message_begin_cb,
          .on_url = SockHandle::url_cb,
          .on_status = SockHandle::status_cb,
          .on_header_field = SockHandle::header_field_cb,
          .on_header_value = SockHandle::header_value_cb,
          .on_headers_complete = SockHandle::headers_complete_cb,
          .on_body = SockHandle::body_cb,
          .on_message_complete = SockHandle::message_complete_cb,
          .on_chunk_header = SockHandle::chunk_header_cb,
          .on_chunk_complete = SockHandle::chunk_complete_cb
    };

    int SockHandle::message_begin_cb(http_parser* p) { return 0; }
    int SockHandle::url_cb(http_parser* p, const char* at, size_t length) {
        auto handle = (SockHandle*)p->data;
        //printf("url %s\n", string(at, length).c_str());
        string url = string(at, length).c_str();
        auto pos1 = url.find('?');
        auto pos2 = url.find('#');
        auto pos3 = url.find('@');
        pos1 = url.npos == pos1 ? length : pos1;
        pos2 = url.npos == pos2 ? length : pos2;
        pos3 = url.npos == pos3 ? length : pos3;
        auto pos = fmin(fmin(pos1, pos2), pos3);
        handle->path = string(at, pos);
        auto querylen = length - pos1;
        if (querylen > 2 && at[pos1] == '?') {
            //size_t len = querylen - 1;
            auto querystr = url.substr(pos1, querylen);
            //printf("querystr %s\n", querystr.c_str());
            size_t begin = pos1;
            //?a=b&c=d
            while (true) {
                string key = "";
                string value = "";
                auto next = url.find('&', begin + 1);
                if (next == url.npos) {
                    auto end = querylen;
                    auto split = url.find('=', begin);
                    if (split != url.npos) {
                        key = url.substr(begin + 1, split - begin - 1);
                        value = url.substr(split + 1, end - split - 1); //4-2-1=1
                    }
                    
                }
                else {
                    auto end = next;
                    auto split = url.find('=', begin);
                    if (split != url.npos) {
                        key = url.substr(begin + 1, split - begin - 1);
                        value = url.substr(split + 1, end - split - 1); //4-2-1=1
                    }
                    begin = next;
                }

                if(!key.empty()) {
                    handle->query[key] = value;
                }
            
                //printf("key-value %s = %s\n", key.c_str(), value.c_str());
                if (next == url.npos) break;

            };

        }
        return 0;
    }
    int SockHandle::status_cb(http_parser* p, const char* at, size_t length) { return 0; }
    int SockHandle::header_field_cb(http_parser* p, const char* buf, size_t len) {
        string field = string(buf, len);
        //printf("field=%s\n", field.c_str());
        if (field == "Sec-WebSocket-Key") {
            auto handle = (SockHandle*)p->data;
            handle->canWSKey = true;
        }
        return 0;
    }

    int SockHandle::header_value_cb(http_parser* p, const char* buf, size_t len) {
        string value = string(buf, len);
        //printf("value=%s\n", value.c_str());
        auto handle = (SockHandle*)p->data;
        if (handle && handle->canWSKey && handle->wsKey.empty()) {
            handle->wsKey = value;
        }
        return 0;
    }

    int SockHandle::headers_complete_cb(http_parser* p) {
        auto handle = (SockHandle*)p->data;
        if (!handle || handle->wsKey.empty()) return 0;
        auto value = handle->wsKey;
        value += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        char secWebSocketAccept[29] = { 0 };
        SHA1((unsigned char*)value.c_str(), value.size(), (unsigned char*)&secWebSocketAccept);
        char secWebSocketAccept64[29] = { 0 };
        Utils::base64((unsigned char*)secWebSocketAccept, secWebSocketAccept64);
        string resqstr = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
        resqstr += string(secWebSocketAccept64);
        resqstr += "\r\n\r\n";
        char* cstr = (char*)resqstr.c_str();
        uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
        uv_buf_t uvBuf = uv_buf_init(cstr, strlen(cstr));

        //发送buffer数组，第四个参数表示数组大小
        auto writecb = [](uv_write_t* req, int status) {
            free(req);
        };
        if (uv_write(req, (uv_stream_t*)handle->client, &uvBuf, 1, writecb) == 0) {
            handle->isHandshaked = true;
            if (handle->OnOpen) {
                SockReadAccessor accessor;
                if (collector.find(accessor, handle)) {
                    auto h = accessor->second;
                    if (h->OnOpen) h->OnOpen(h);
                }
                accessor.release();
            }
        }
        else {
            auto req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
            uv_shutdown(req, (uv_stream_t*)handle->client, SockHandle::tcp_shutdown);
        }
        return 0;
    }
    int SockHandle::body_cb(http_parser* p, const char* at, size_t length) { return 0; }
    int SockHandle::message_complete_cb(http_parser* p) {
        return 0;
    }
    int SockHandle::chunk_header_cb(http_parser* p) { return 0; }
    int SockHandle::chunk_complete_cb(http_parser* p) { return 0; }

#pragma endregion

    void SockHandle::tcp_shutdown(uv_shutdown_t* req, int status) {
        auto client = reinterpret_cast<uv_handle_t*>(req->handle);
        if (!client || client->flags & 0x00000001 || client->flags & 0x00000002) return;
        uv_close(client, SockHandle::tcp_close);
        free(req);
    }

    void SockHandle::tcp_close(uv_handle_t* client) {
        if (client) {
            auto handle = reinterpret_cast<SockHandle*>(client->data);
            if (handle) {
                handle->is_closed = true;
                SockReadAccessor accessor;
                if (collector.find(accessor, handle)) {
                    auto h = accessor->second;
                    if (h->OnClosed) h->OnClosed(h);
                }
                accessor.release();
                collector.erase(handle);

                // delete handle;
                // client->data = nullptr;
            }
            //在析构中释放
            //free(client); 
        }
    }

    void SendCallBack(uv_async_t* handle);

    void TestMultithreadingSend(SockHandle* handle, char* buff, size_t len);

    SockHandle::SockHandle() {
        is_closed = false;

        //printf("New SockHandle %p\n", this);
    }

    void SockHandle::SetClient(void* client) {
        this->client = client;
        async_handle.data = this;
        uv_async_init(uv_default_loop(), &async_handle, SendCallBack);
    }

    // int allpayload = 0;
    // int allbuffer = 0;
    void SockHandle::ReadBuffer(char* buff, size_t len) {
        if (!isHandshaked) {
            //printf("%s\n===============Handshaking============\n", string(buff, len).c_str());
            http_parser parser;
            http_parser_init(&parser, HTTP_REQUEST);
            parser.data = this;
            size_t np = http_parser_execute(&parser, &settings, buff, len);
            if (np != len || !isHandshaked) {
                printf("http protocol error. %ld\n", parser.http_errno);
                auto req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
                uv_shutdown(req, (uv_stream_t*)client, SockHandle::tcp_shutdown);
            }
        }
        else {
            //allbuffer += len;
            if (remainBytes == 0) {
                Utils::decodeWSHeader(wsHeader, (unsigned char*)buff, len);
                if (wsHeader.opCode) opCode = wsHeader.opCode;
                remainBytes = wsHeader.frameLength - len;

                payLoadData = (unsigned char*)malloc(wsHeader.payload);
                //auto data = string_view((const char*)payLoadData, wsHeader.payload);
                frames.emplace_back((char*)payLoadData, wsHeader.payload);
                frames_total += wsHeader.payload;
                //allpayload += wsHeader.payload;
                if (remainBytes >= 0) {
                    memcpy(payLoadData, &buff[wsHeader.dataOffset], len);
                }
                else { // remainBytes < 0
                    memcpy(payLoadData, &buff[wsHeader.dataOffset], wsHeader.frameLength);
                    if ((buff[len + remainBytes] & 0x70) == 0) { //isError=0
                        auto left = -remainBytes;
                        remainBytes = 0;
                        Utils::decodeData(wsHeader, (unsigned char*)payLoadData, wsHeader.payload);
                        if (wsHeader.fin) {
                            finish();
                        }
                        assert(wsHeader.isError == false);
                        ReadBuffer(&buff[len - left], left); //remainBytes = 0;
                    }
                    else {
                        //todo close
                    }
                    return;
                }
            }
            else {
                auto copylen = (long)fmin(remainBytes, len);
                memcpy(&payLoadData[wsHeader.frameLength - wsHeader.dataOffset - remainBytes], buff, copylen);
                remainBytes -= len;
                if (remainBytes < 0) { //粘包 & 非法
                    //printf("(remainBytes < 0) %ld\n", remainBytes);
                    //Utils::decodeWSHeader(wsHeader, (unsigned char*)&buff[len + remainBytes], -remainBytes);
                    //assert((buff[len + remainBytes] & 0x70) == 0);
                    if ((buff[len + remainBytes] & 0x70) == 0) { //isError=0
                        auto left = -remainBytes;
                        remainBytes = 0;
                        Utils::decodeData(wsHeader, (unsigned char*)payLoadData, wsHeader.payload);
                        if (wsHeader.fin) {
                            finish();
                        }
                        //Utils::decodeWSHeader(wsHeader, (unsigned char*)&buff[len - left], left);
                        assert(wsHeader.isError == false);
                        ReadBuffer(&buff[len - left], left); //remainBytes = 0;
                    }
                    else {
                        //todo close
                    }
                    return;
                }
            }

            //当前frame没有接收完
            if (remainBytes > 0) {
                return;
            }

            //printf("ReadBuffer Success! \n");

            //remainBytes == 0
            Utils::decodeData(wsHeader, (unsigned char*)payLoadData, wsHeader.payload);
            if (wsHeader.fin) {
                finish();
            }
        }
    }


    void SockHandle::finish() {
        size_t offset = 0;
        size_t count = frames.size();
        size_t total = frames_total;
        char* msg = nullptr;
        if (count == 1) { //短消息
            msg = get<0>(frames[0]);
        }
        else { //长消息
            msg = (char*)malloc(total);
            for (size_t i = 0; i < count; i++) {
                auto s = get<1>(frames[i]);
                memcpy(&msg[offset], get<0>(frames[i]), s);
                offset += s;
                free(get<0>(frames[i]));
            }
        }
        frames_total = 0;
        frames.clear();

        printf("total=%ld\n", total);


        //todo opcode == close
        if (OnMessage) {
            SockReadAccessor accessor;
            if (collector.find(accessor, this)) {
                auto h = accessor->second;
                if (h->OnMessage) h->OnMessage(h, msg, total, opCode);
            }
            accessor.release();
        }

        //free(msg);

        // auto workreq = (uv_work_t*)malloc(sizeof(uv_work_t));
        // workreq->data = new std::tuple<char*, size_t, void*>(msg, total, this);

        // auto work = [](uv_work_t* req) {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(20));
        //     auto tuple = (std::tuple<char*, size_t, void*>*)req->data;
        //     auto msg = std::get<0>(*tuple);
        //     auto total = std::get<1>(*tuple);
        //     auto handle = (SockHandle*)std::get<2>(*tuple);
        //     SockAccessor accessor;
        //     if (collector.find(accessor, handle)) {
        //         auto sock = accessor->second;
        //         if (sock->is_closed) return;
        //         auto client = (uv_handle_t*)sock->client;
        //         if (client->flags & 0x00000001 || client->flags & 0x00000002) {
        //             return;
        //         }
        //         auto temp = (char*)malloc(total);
        //         memcpy(temp, msg, total);
        //         sock->addSendTask(temp, total);
        //         printf("SendData=1 \n");
        //     }

        //     //SendBuffer(msg, total);
        // };

        // auto afterwork = [](uv_work_t* req, int status) {
        //     auto tuple = (std::tuple<char*, size_t, void*>*)req->data;
        //     auto handle = (SockHandle*)std::get<2>(*tuple);
        //     delete tuple;

        //     //client->mutex.unlock();
        //     free(req);

        // };

        // printf("SendData=0 \n");

        // uv_queue_work(uv_default_loop(), workreq, work, afterwork);

#ifdef ON_MESSAGE_FREE
        free(msg);
#endif
    }

    void SendCallBack(uv_async_t* handle) {
        auto sock = reinterpret_cast<SockHandle*>(handle->data);
        if (sock && !sock->is_closed) {
            auto client = (uv_stream_t*)sock->client;
            /*
            UV_HANDLE_CLOSING                     = 0x00000001,
            UV_HANDLE_CLOSED                      = 0x00000002,
            */
            if (client->flags & 0x00000001 || client->flags & 0x00000002) {
                return;
            }

            auto writecb = [](uv_write_t* req, int status) {
                if (req->data) free(req->data); //free(buff);
                free(req);
            };
            tuple<char*, size_t> t;
            while (sock->sendQueue.try_dequeue(t)) {
                auto buff = get<0>(t);
                auto len = get<1>(t);
                uv_write_t* req = (uv_write_t*)malloc(sizeof(uv_write_t));
                uv_buf_t uvBuf = uv_buf_init((char*)buff, len);
                req->data = uvBuf.base;
                if (!(uv_is_writable(client) && uv_write(req, client, &uvBuf, 1, writecb) == 0)) {
                    printf("uv_write error 2\n");
                }
                printf("SendData=2 \n");

            }
        }
    }

    void SockHandle::addSendTask(char* buff, size_t len) {
        auto cli = reinterpret_cast<uv_handle_t*>(client);
        if (is_closed || cli->flags & 0x00000001 || cli->flags & 0x00000002) return;
        sendQueue.try_enqueue(tuple<char*, size_t>(buff, len));
        uv_async_send(&async_handle);
    }

    void SockHandle::SendBuffer(SockHandle* handle, char* buff, size_t len, int opCode) {
        SockReadAccessor accessor;
        if (collector.find(accessor, handle)) {
            auto sock = accessor->second;
            auto rettuple = Utils::encode((unsigned char*)buff, len, opCode);
            auto sendbuff = get<0>(rettuple);
            auto sendlen = get<1>(rettuple);
            sock->addSendTask((char*)sendbuff, sendlen);
            printf("SendData=1 \n");
        }
    }

    void SockHandle::CloseHandle(SockHandle* handle) {
        SockReadAccessor accessor;
        if (collector.find(accessor, handle)) {
            auto sock = accessor->second;
            if (sock->is_closed) return;
            auto req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
            uv_shutdown(req, (uv_stream_t*)sock->client, SockHandle::tcp_shutdown);
        }
    }

    SockHandle::~SockHandle() {
        is_closed = true;
        printf("~SockHandle()\n");
        async_handle.data = nullptr;
        uv_close((uv_handle_t*)&async_handle, 0);

        //清理读取缓冲区
        size_t count = frames.size();
        for (size_t i = 0; i < count; i++) {
            free(get<0>(frames[i]));
        }

        //清理写入缓冲区
        tuple<char*, size_t> item;
        while (sendQueue.size_approx() != 0) {
            sendQueue.try_dequeue(item);
            free(get<0>(item));
        }

        if (client) free(client);
        client = nullptr;

    }

}