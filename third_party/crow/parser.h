#pragma once

#include <string>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <cstdio>
#include <atomic>
#include <thread>

#include "crow/http_request.h"
#include "crow/http_parser_merged.h"

namespace crow
{
    /// A wrapper for `nodejs/http-parser`.

    ///
    /// Used to generate a \ref crow.request from the TCP socket buffer.
    // Streaming threshold: body size (bytes) above which data is written to a temp file
    // instead of buffered in memory. 0 = disabled.
    inline size_t streaming_threshold = 0;

    // Temp directory for streaming upload temp files (must end with separator)
    inline std::string streaming_temp_dir;

    template<typename Handler>
    struct HTTPParser : public http_parser
    {
        static int on_message_begin(http_parser*)
        {
            return 0;
        }
        static int on_method(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->req.method = static_cast<HTTPMethod>(self->method);

            return 0;
        }
        static int on_url(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->req.raw_url.insert(self->req.raw_url.end(), at, at + length);
            self->req.url_params = query_string(self->req.raw_url);
            self->req.url = self->req.raw_url.substr(0, self->qs_point != 0 ? self->qs_point : std::string::npos);

            self->process_url();

            return 0;
        }
        static int on_header_field(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            switch (self->header_building_state)
            {
                case 0:
                    if (!self->header_value.empty())
                    {
                        self->req.headers.emplace(std::move(self->header_field), std::move(self->header_value));
                    }
                    self->header_field.assign(at, at + length);
                    self->header_building_state = 1;
                    break;
                case 1:
                    self->header_field.insert(self->header_field.end(), at, at + length);
                    break;
            }
            return 0;
        }
        static int on_header_value(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            switch (self->header_building_state)
            {
                case 0:
                    self->header_value.insert(self->header_value.end(), at, at + length);
                    break;
                case 1:
                    self->header_building_state = 0;
                    self->header_value.assign(at, at + length);
                    break;
            }
            return 0;
        }
        static int on_headers_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (!self->header_field.empty())
            {
                self->req.headers.emplace(std::move(self->header_field), std::move(self->header_value));
            }

            self->set_connection_parameters();

            // Check if we should stream the body to a temp file
            if (crow::streaming_threshold > 0)
            {
                auto it = self->req.headers.find("Content-Length");
                if (it != self->req.headers.end())
                {
                    try {
                        size_t content_length = std::stoull(it->second);
                        if (content_length > streaming_threshold)
                        {
                            self->start_streaming(content_length);
                        }
                    } catch (...) {}
                }
            }

            self->process_header();
            return 0;
        }
        static int on_body(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (self->streaming_ && self->body_ofs_.is_open())
            {
                self->body_ofs_.write(at, length);
                self->body_received_ += length;
                return self->body_ofs_.good() ? 0 : -1;
            }
            self->req.body.insert(self->req.body.end(), at, at + length);
            return 0;
        }
        static int on_message_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);

            self->message_complete = true;

            // Finalize streaming: close temp file and set path on request
            if (self->streaming_)
            {
                self->body_ofs_.close();
                self->req.body_stream_path = self->body_temp_path_;
            }

            self->process_message();
            return 0;
        }
        HTTPParser(Handler* handler):
          handler_(handler)
        {
            http_parser_init(this);
        }

        // return false on error
        /// Parse a buffer into the different sections of an HTTP request.
        bool feed(const char* buffer, int length)
        {
            if (message_complete)
                return true;

            const static http_parser_settings settings_{
              on_message_begin,
              on_method,
              on_url,
              on_header_field,
              on_header_value,
              on_headers_complete,
              on_body,
              on_message_complete,
            };

            int nparsed = http_parser_execute(this, &settings_, buffer, length);
            if (http_errno != CHPE_OK)
            {
                return false;
            }
            return nparsed == length;
        }

        bool done()
        {
            return feed(nullptr, 0);
        }

        void clear()
        {
            close_streaming();
            req = crow::request();
            header_field.clear();
            header_value.clear();
            header_building_state = 0;
            qs_point = 0;
            message_complete = false;
            state = CROW_NEW_MESSAGE();
        }

        void close_streaming()
        {
            if (body_ofs_.is_open())
            {
                body_ofs_.close();
            }
            if (!body_temp_path_.empty())
            {
                std::remove(body_temp_path_.c_str());
                body_temp_path_.clear();
            }
            streaming_ = false;
            body_received_ = 0;
        }

        ~HTTPParser()
        {
            close_streaming();
        }

        inline void process_url()
        {
            handler_->handle_url();
        }

        inline void process_header()
        {
            handler_->handle_header();
        }

        inline void process_message()
        {
            handler_->handle();
        }

        inline void set_connection_parameters()
        {
            req.http_ver_major = http_major;
            req.http_ver_minor = http_minor;

            //NOTE(EDev): it seems that the problem is with crow's policy on closing the connection for HTTP_VERSION < 1.0, the behaviour for that in crow is "don't close the connection, but don't send a keep-alive either"

            // HTTP1.1 = always send keep_alive, HTTP1.0 = only send if header exists, HTTP?.? = never send
            req.keep_alive = (http_major == 1 && http_minor == 0) ?
                               ((flags & F_CONNECTION_KEEP_ALIVE) ? true : false) :
                               ((http_major == 1 && http_minor == 1) ? true : false);

            // HTTP1.1 = only close if close header exists, HTTP1.0 = always close unless keep_alive header exists, HTTP?.?= never close
            req.close_connection = (http_major == 1 && http_minor == 0) ?
                                     ((flags & F_CONNECTION_KEEP_ALIVE) ? false : true) :
                                     ((http_major == 1 && http_minor == 1) ? ((flags & F_CONNECTION_CLOSE) ? true : false) : false);
            req.upgrade = static_cast<bool>(upgrade);
        }

        /// The final request that this parser outputs.
        ///
        /// Data parsed is put directly into this object as soon as the related callback returns. (e.g. the request will have the cooorect method as soon as on_method() returns)
        request req;

    private:
        void start_streaming(size_t content_length)
        {
            // Generate a unique temp file path in the configured temp directory
            static std::atomic<size_t> counter{0};
            body_temp_path_ = streaming_temp_dir + "upload_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "_" + std::to_string(counter++) + ".crow_stream";
            body_ofs_.open(body_temp_path_, std::ios::binary);
            if (body_ofs_.is_open())
            {
                streaming_ = true;
                body_received_ = 0;
            }
            else
            {
                body_temp_path_.clear();
            }
        }

        int header_building_state = 0;
        bool message_complete = false;
        std::string header_field;
        std::string header_value;

        bool streaming_ = false;
        std::ofstream body_ofs_;
        std::string body_temp_path_;
        size_t body_received_ = 0;

        Handler* handler_; ///< This is currently an HTTP connection object (\ref crow.Connection).
    };
} // namespace crow

#undef CROW_NEW_MESSAGE
#undef CROW_start_state
