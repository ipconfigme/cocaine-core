#ifndef COCAINE_RESPONSE_HPP
#define COCAINE_RESPONSE_HPP

#include <set>

#include "cocaine/common.hpp"

namespace cocaine { namespace core {

class response_t:
    public boost::noncopyable,
    public boost::enable_shared_from_this<response_t>,
    public helpers::birth_control_t<response_t>
{
    public:
        response_t(const std::vector<std::string>& route, boost::shared_ptr<core_t> parent):
            m_route(route),
            m_parent(parent)
        { }

        ~response_t() {
            m_parent->seal(this);
        }

    public:
        template<class T>
        void wait(const std::string& key, boost::shared_ptr<T> future) {
            future->bind(key, shared_from_this());
        }

        void push(const std::string& key, const Json::Value& result) {
            switch(m_root["results"][key].type()) {
                case Json::nullValue:
                    // This is the first or the only result of the set
                    m_root["results"][key] = result;
                    break;
                case Json::arrayValue:
                    // This is an another item in a list
                    m_root["results"][key].append(result);
                    break;
                default: {
                    // This key appeared to be a list, so convert it
                    Json::Value list(Json::arrayValue);

                    list.append(m_root["results"][key]);
                    list.append(result);

                    m_root["results"][key] = list;
                }
            }
        }

        void abort(const std::string& error) {
            m_root.clear();
            m_root["error"] = error;
        }

        void abort(const std::string& key, const std::string& error) {
            Json::Value object;
            object["error"] = error;
            
            push(key, object);
        }
        
    public:
        const Json::Value& root() const {
            return m_root;
        }

        const std::vector<std::string>& route() const {
            return m_route;
        }

    private:
        std::vector<std::string> m_route;
        boost::shared_ptr<core_t> m_parent;

        Json::Value m_root;
};

}}

#endif
