#ifndef GWS_UTILS_H
#define GWS_UTILS_H

#include <cstring>
#include <string_view>
#include <tuple>

#define PRINT_HEADER 0

namespace GWS {
    struct WSHeader
    {
        bool fin = 0;
        bool isError = 0;
        bool isMask = 0;
        int opCode = 0;
        int maskOffset = 0;
        int dataOffset = 0;
        unsigned char mask[4];
        u_int64_t payload = 0;
        u_int64_t frameLength = 0;
    };

    namespace Utils {
        inline void base64(unsigned char* src, char* dst) {
            const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            for (int i = 0; i < 18; i += 3) {
                *dst++ = b64[(src[i] >> 2) & 63];
                *dst++ = b64[((src[i] & 3) << 4) | ((src[i + 1] & 240) >> 4)];
                *dst++ = b64[((src[i + 1] & 15) << 2) | ((src[i + 2] & 192) >> 6)];
                *dst++ = b64[src[i + 2] & 63];
            }
            *dst++ = b64[(src[18] >> 2) & 63];
            *dst++ = b64[((src[18] & 3) << 4) | ((src[19] & 240) >> 4)];
            *dst++ = b64[((src[19] & 15) << 2)];
            *dst++ = '=';
        }

        inline void decodeWSHeader(WSHeader& header, unsigned char* buf, size_t len) {
            header.fin = buf[0] >> 7;
            header.isError = buf[0] & 0x70;
            header.opCode = buf[0] & 0x0f;
            header.maskOffset = 0;
            header.dataOffset = 2;
            header.isMask = (buf[1] >> 7) > 0;
            if (header.isMask) {
                header.maskOffset = 2;
                header.dataOffset += 4;
            }
            header.payload = buf[1] & 0x7f;
#if PRINT_HEADER
            printf("buffer len=%d payload=%ld\n", len, header.payload);
#endif
            if (header.payload == 126) {
                header.maskOffset += 2;
                header.dataOffset += 2;
                header.payload = (u_int64_t)buf[2] << 8;
                header.payload += (u_int64_t)buf[3];
            }
            else if (header.payload == 127) {
                header.maskOffset += 8;
                header.dataOffset += 8;
                header.payload = (u_int64_t)buf[2] << 56;
                header.payload += (u_int64_t)buf[3] << 48;
                header.payload += (u_int64_t)buf[4] << 40;
                header.payload += (u_int64_t)buf[5] << 32;

                header.payload += (u_int64_t)buf[6] << 24;
                header.payload += (u_int64_t)buf[7] << 16;
                header.payload += (u_int64_t)buf[8] << 8;
                header.payload += (u_int64_t)buf[9] << 0;
            }
            header.frameLength = header.payload + header.dataOffset;
#if PRINT_HEADER
            printf("frameLength=%d payload=%ld\n", header.frameLength, header.payload);
#endif            
            if (header.isMask) {
                header.mask[0] = buf[header.maskOffset];
                header.mask[1] = buf[header.maskOffset + 1];
                header.mask[2] = buf[header.maskOffset + 2];
                header.mask[3] = buf[header.maskOffset + 3];
            }
        }

        inline void decodeData(WSHeader& header, unsigned char* data, size_t len) {
            if (header.isMask) {
                for (size_t i = 0; i < len; i++) {
                    data[i] = data[i] ^ header.mask[i % 4];
                }
            }
        }

        /**
         *
         * len <= 16 kb
         * isMask always false. 客户端必须在它发送到服务器的所有帧中添加掩码, 服务端禁止在发送数据帧给客户端时添加掩码。
         * opCode: 0 continue, 1 text, 2 binary
        */
        inline std::tuple<unsigned char*, size_t> encode(unsigned char* buff, size_t len, int opCode = 2) {
            if (len == 0) return std::tuple<unsigned char*, size_t>((unsigned char*)nullptr, 0);
            if (len < 126) {
                auto onelen = 2 + len;
                auto onebuff = (char*)malloc(onelen);
                memset(onebuff, 0, onelen);
                onebuff[0] = (unsigned char)0x80;
                onebuff[0] += (unsigned char)opCode;
                onebuff[1] += (unsigned char)len;
                memcpy(&onebuff[2], buff, len);
                return std::tuple<unsigned char*, size_t>((unsigned char*)onebuff, onelen);
            }

            //16K 分片
            int segment_len = 16 * 1024;
            int segment_count = len / segment_len;
            size_t last = len % segment_len;
            if (last == 0 && segment_count > 0) {
                segment_count--;
                last = segment_len;
            }

            unsigned char first[4] = { 0, 0, 0, 0 };
            unsigned char mid[4] = { 0, 0, 0, 0 };
            unsigned char end[4] = { 0, 0, 0, 0 };
            int header_len = 4;
            if (segment_count == 0) {
                end[0] = (unsigned char)0x80;
                end[0] += (unsigned char)opCode;
                end[1] += 126;
                end[2] = (last & 0xff00) >> 8;
                end[3] = (last & 0xff);
            }
            else {
                // 16 * 1024 == 0x4000
                first[0] += opCode;
                first[1] += 126;
                first[2] = 0x40;

                mid[1] += 126;
                mid[2] = 0x40;

                end[0] = 0x80;
                end[1] += 126;
                end[2] = (last & 0xff00) >> 8;
                end[3] = (last & 0xff);
            }

            auto sendlen = 4 * segment_count + 4 + len;
            auto sendbuff = (char*)malloc(sendlen);
            size_t offset = 0;
            //first
            if (segment_count > 0) {
                memcpy(&sendbuff[offset], first, 4);
                offset += 4;
                memcpy(&sendbuff[offset], buff, segment_len);
                offset += segment_len;
            }

            //mid
            for (size_t i = 1; i < segment_count; i++) {
                memcpy(&sendbuff[offset], mid, 4);
                offset += 4;
                memcpy(&sendbuff[offset], &buff[i * segment_len], segment_len);
                offset += segment_len;
            }

            //end
            memcpy(&sendbuff[offset], end, 4);
            offset += 4;
            memcpy(&sendbuff[offset], &buff[len - last], last);

            assert(offset + last == sendlen);

            WSHeader header;
            decodeWSHeader(header, (unsigned char*)sendbuff, sendlen);

            return std::tuple<unsigned char*, size_t>((unsigned char*)sendbuff, sendlen);
        }

        inline std::tuple<unsigned char*, size_t> encodeHeader(WSHeader header) {
            header.isMask = false;
            auto len = header.payload;
            if (len < 126) {
                header.dataOffset = 2;
            }
            else if (len < 30 * 1024) {
                header.dataOffset = 4;
            }
            auto buff = (unsigned char*)malloc(header.dataOffset);
            memset(buff, 0, header.dataOffset);
            if (header.fin) buff[0] = 0x80;
            buff[0] += (unsigned char)header.opCode;
            buff[1] = 0x00; //isMask
            if (len < 126) {
                buff[1] += len;
            }
            else if (len < 30 * 1024) {
                buff[1] += 126;
                buff[2] = (len & 0xff00) >> 8;
                buff[3] = (len & 0xff);
            }
            else {
                throw("len > 30 kb\n");
            }

            printf("%02X\n", buff[0]);

            return std::tuple<unsigned char*, size_t>(buff, header.dataOffset);
        }
    }
}

#endif