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

#include "cocaine/drivers/native_server.hpp"
#include "cocaine/engine.hpp"

using namespace cocaine::engine::driver;
using namespace cocaine::networking;

native_server_job_t::native_server_job_t(native_server_t* driver, const messages::request_t& request, const route_t& route):
    unique_id_t(request.id),
    job::job_t(driver, request.policy),
    m_route(route)
{ }

void native_server_job_t::react(const events::chunk_t& event) {
    send(messages::tag_t(id()), ZMQ_SNDMORE);
    
    zeromq_server_t* server = static_cast<zeromq_server_t*>(m_driver);
    server->socket().send(event.message);
}

void native_server_job_t::react(const events::error_t& event) {
    send(messages::error_t(id(), event.code, event.message));
}

void native_server_job_t::react(const events::choked_t& event) {
    send(messages::tag_t(id(), true));
}

native_server_t::native_server_t(engine_t* engine, const std::string& method, const Json::Value& args):
    zeromq_server_t(engine, method, args)
{ }

Json::Value native_server_t::info() const {
    Json::Value result(Json::objectValue);

    result["statistics"] = stats();
    result["type"] = "native-server";
    result["endpoint"] = m_socket.endpoint();
    result["route"] = m_socket.route();

    return result;
}

void native_server_t::process(ev::idle&, int) {
    if(m_socket.pending()) {
        zmq::message_t message;
        route_t route;

        do {
            m_socket.recv(&message);

            if(!message.size()) {
                break;
            }

            route.push_back(std::string(
                static_cast<const char*>(message.data()),
                message.size()));
        } while(m_socket.more());

        if(route.empty() || !m_socket.more()) {
            syslog(LOG_ERR, "driver [%s:%s]: got a corrupted request - no route", 
                m_engine->name().c_str(), m_method.c_str());
            return;
        }

        while(m_socket.more()) {
            unsigned int type = 0;
            messages::request_t request;

            boost::tuple<unsigned int&, messages::request_t&> tier(type, request);

            if(!m_socket.recv_multi(tier)) {
                syslog(LOG_ERR, "driver [%s:%s]: got a corrupted request from '%s'",
                    m_engine->name().c_str(), m_method.c_str(), route.back().c_str());
                continue;
            }

            // TEST: This is temporary for testing purposes
            BOOST_ASSERT(type == request.type);

            boost::shared_ptr<native_server_job_t> job;
            
            try {
                job.reset(new native_server_job_t(this, request, route));
            } catch(const std::runtime_error& e) {
                syslog(LOG_ERR, "driver [%s:%s]: got a corrupted request from '%s' - %s",
                    m_engine->name().c_str(), m_method.c_str(), route.back().c_str(), e.what());
                continue;
            }

            if(!m_socket.more() || !m_socket.recv(job->request())) {
                syslog(LOG_ERR, "driver [%s:%s]: got a corrupted request from '%s' - missing request body",
                    m_engine->name().c_str(), m_method.c_str(), route.back().c_str());
                job->process_event(events::error_t(events::request_error, "missing request body"));
                continue;
            }

            m_engine->enqueue(job);
        }
    } else {
        m_watcher.start(m_socket.fd(), ev::READ);
        m_processor.stop();
    }
}

