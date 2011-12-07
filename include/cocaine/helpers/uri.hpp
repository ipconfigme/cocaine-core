//
// Copyright (C) 2011 Andrey Sibiryov <me@kobology.ru>
//
// Licensed under the BSD 2-Clause License (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef COCAINE_URI_HPP
#define COCAINE_URI_HPP

#include <sstream>

#include <uriparser/Uri.h>

#include "cocaine/common.hpp"

namespace cocaine { namespace helpers {

struct uri_t {
    public:
        uri_t(const std::string& source):
            m_source(source)
        {
            // URI parser initialization
            UriParserStateA state;
            int result;

            state.uri = &m_uri;
            result = uriParseUriA(&state, m_source.c_str());

            if(result != URI_SUCCESS) {
                char buffer[256];
                strerror_r(result, buffer, 256);
                
                throw std::runtime_error(buffer);
            }
        }

        ~uri_t() {
            uriFreeUriMembersA(&m_uri);
        }

        inline std::string source() const {
            return m_source;
        }

        inline std::string scheme() const {
            return std::string(m_uri.scheme.first, m_uri.scheme.afterLast);
        }

        inline std::string userinfo() const {
            return std::string(m_uri.userInfo.first, m_uri.userInfo.afterLast);
        }

        inline std::string host() const {
            return std::string(m_uri.hostText.first, m_uri.hostText.afterLast);
        }            

        inline unsigned int port() const {
            unsigned int port = 0;
        
            if(m_uri.portText.first && m_uri.portText.afterLast) {
                std::istringstream extract(std::string(
                    m_uri.portText.first, m_uri.portText.afterLast));
                extract >> port;
            }

            return port;
        }

        inline std::vector<std::string> path() const {
            std::vector<std::string> path;
            UriPathSegmentA *segment = m_uri.pathHead;

            while(segment) {
                path.push_back(std::string(segment->text.first, segment->text.afterLast));
                segment = segment->next;
            }

            return path;
        }

        inline std::map<std::string, std::string> query() const {
            std::map<std::string, std::string> query;

            if(m_uri.query.first && m_uri.query.afterLast) {
                std::string src(m_uri.query.first, m_uri.query.afterLast);
                size_t start = 0, delimiter, end;
        
                do {
                    delimiter = src.find_first_of('=', start);
                    end = src.find_first_of('&', start);

                    if(delimiter != src.npos) {
                        query.insert(std::make_pair(
                            std::string(src, start, delimiter - start),
                            std::string(src, delimiter + 1, end - delimiter - 1)
                        ));
                    }

                    start = end + 1;
                } while(end != src.npos);
            }

            return query;
        }

        inline std::string fragment() const {
            return std::string(m_uri.fragment.first, m_uri.fragment.afterLast);
        }

    private:
        const std::string m_source;
        UriUriA m_uri;
};

}}

#endif
