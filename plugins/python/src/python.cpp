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

#include <sstream>
#include <boost/filesystem/fstream.hpp>

#include "python.hpp"

#include "cocaine/registry.hpp"

using namespace cocaine::core;
using namespace cocaine::engine;

python_t::python_t(context_t& ctx):
    plugin_t(ctx, "python"),
    m_python_module(NULL)
{ }

void python_t::initialize(const app_t& app) {
    Json::Value args(app.manifest["args"]);

    if(!args.isObject()) {
        throw unrecoverable_error_t("malformed manifest");
    }
    
    boost::filesystem::path source(args["source"].asString());
   
    if(source.empty()) {
        throw unrecoverable_error_t("no code location has been specified");
    }

    if(boost::filesystem::is_directory(source)) {
        source /= "__init__.py";
    }

    boost::filesystem::ifstream input(source);
    
    if(!input) {
        throw unrecoverable_error_t("unable to open " + source.string());
    }

    // Acquire the interpreter state
    thread_state_t state;
    
    // NOTE: Prepend the current application cache location to the sys.path,
    // so that it could import different stuff from there
    PyObject* syspath = PySys_GetObject("path");

    // XXX: Does it steal the reference or not?
    PyList_Insert(syspath, 0,
        PyString_FromString( 
#if BOOST_FILESYSTEM_VERSION == 3
            source.parent_path().string().c_str()
#else
            source.branch_path().string().c_str()
#endif
        )
    );
   
    // Initialize the context module
    PyObject* module = Py_InitModule("context", NULL);
    
    PyModule_AddObject(
        module,
        "manifest",
        PyDictProxy_New(
            wrap(app.manifest["args"])
        )
    );

    // Load the code
    std::stringstream stream;
    stream << input.rdbuf();

    compile(source.string(), stream.str());
}

void python_t::invoke(invocation_site_t& site, const std::string& method) {
    if(!m_python_module) {
        throw unrecoverable_error_t("python module is not initialized");
    }

    thread_state_t state;
    
    python_object_t object(
        PyObject_GetAttrString(
            m_python_module,
            method.c_str()
        )
    );
    
    if(PyErr_Occurred()) {
        throw unrecoverable_error_t(exception());
    }

    if(PyType_Check(object)) {
        if(PyType_Ready(reinterpret_cast<PyTypeObject*>(*object)) != 0) {
            throw unrecoverable_error_t(exception());
        }
    }

    if(!PyCallable_Check(object)) {
        throw unrecoverable_error_t("'" + method + "' is not callable");
    }

    python_object_t args(NULL);
#if PY_VERSION_HEX >= 0x02070000
    boost::shared_ptr<Py_buffer> buffer;
#endif

    if(site.request && site.request_size) {
#if PY_VERSION_HEX >= 0x02070000
        buffer.reset(static_cast<Py_buffer*>(malloc(sizeof(Py_buffer))), free);

        buffer->buf = const_cast<void*>(site.request);
        buffer->len = site.request_size;
        buffer->readonly = true;
        buffer->format = NULL;
        buffer->ndim = 0;
        buffer->shape = NULL;
        buffer->strides = NULL;
        buffer->suboffsets = NULL;

        python_object_t view(PyMemoryView_FromBuffer(buffer.get()));
#else
        python_object_t view(PyBuffer_FromMemory(
            const_cast<void*>(site.request), 
            site.request_size));
#endif

        args = PyTuple_Pack(1, *view);
    } else {
        args = PyTuple_New(0);
    }

    python_object_t result(PyObject_Call(object, args, NULL));

    if(PyErr_Occurred()) {
        throw recoverable_error_t(exception());
    } else if(result.valid()) {
        respond(site, result);
    }
}

std::string python_t::exception() {
    python_object_t type(NULL), object(NULL), traceback(NULL);
    
    PyErr_Fetch(&type, &object, &traceback);
    python_object_t message(PyObject_Str(object));
    
    return PyString_AsString(message);
}

void python_t::compile(const std::string& path, const std::string& code) {
    python_object_t bytecode(
        Py_CompileString(
            code.c_str(),
            path.c_str(),
            Py_file_input
        )
    );

    if(PyErr_Occurred()) {
        throw unrecoverable_error_t(exception());
    }

    m_python_module = PyImport_ExecCodeModule(
        "<application>",
        bytecode);
    
    if(PyErr_Occurred()) {
        throw unrecoverable_error_t(exception());
    }
}

void python_t::respond(invocation_site_t& site, python_object_t& result) {
    if(PyString_Check(result)) {
        throw recoverable_error_t("the result must be an iterable");
    }

    python_object_t iterator(PyObject_GetIter(result));

    if(iterator.valid()) {
        python_object_t item(NULL);

        while(true) {
            item = PyIter_Next(iterator);

            if(PyErr_Occurred()) {
                throw recoverable_error_t(exception());
            } else if(!item.valid()) {
                break;
            }
        
#if PY_VERSION_HEX > 0x02060000
            if(PyObject_CheckBuffer(item)) {
                boost::shared_ptr<Py_buffer> buffer(
                    static_cast<Py_buffer*>(malloc(sizeof(Py_buffer))),
                    free);

                if(PyObject_GetBuffer(item, buffer.get(), PyBUF_SIMPLE) == 0) {
                    Py_BEGIN_ALLOW_THREADS
                        site.push(buffer->buf, buffer->len);
                    Py_END_ALLOW_THREADS
                    
                    PyBuffer_Release(buffer.get());
                } else {
                    throw recoverable_error_t("unable to serialize the result");
                }
            }
#else
            if(PyString_Check(item)) {
                callback(PyString_AsString(item), PyString_Size(item));
            } else {
                throw recoverable_error_t("unable to serialize the result");
            }
#endif
        }
    } else {
        throw recoverable_error_t(exception());
    }
}

PyObject* python_t::wrap(const Json::Value& value) {
    PyObject* object = NULL;

    switch(value.type()) {
        case Json::booleanValue:
            return PyBool_FromLong(value.asBool());
        case Json::intValue:
        case Json::uintValue:
            return PyLong_FromLong(value.asInt());
        case Json::realValue:
            return PyFloat_FromDouble(value.asDouble());
        case Json::stringValue:
            return PyString_FromString(value.asCString());
        case Json::objectValue: {
            object = PyDict_New();
            Json::Value::Members names(value.getMemberNames());

            for(Json::Value::Members::iterator it = names.begin();
                it != names.end();
                ++it) 
            {
                PyDict_SetItemString(object, it->c_str(), wrap(value[*it]));
            }

            break;
        } case Json::arrayValue: {
            object = PyTuple_New(value.size());
            Py_ssize_t position = 0;

            for(Json::Value::const_iterator it = value.begin(); 
                it != value.end();
                ++it) 
            {
                PyTuple_SetItem(object, position++, wrap(*it));
            }

            break;
        } case Json::nullValue:
            Py_RETURN_NONE;
    }

    return object;
}

PyThreadState* g_state = NULL;

void save() {
    g_state = PyEval_SaveThread();
}

void restore() {
    PyEval_RestoreThread(g_state);
}

extern "C" {
    void initialize(registry_t& registry) {
        // Initialize the Python subsystem
        Py_InitializeEx(0);

        // Initialize the GIL
        PyEval_InitThreads();

        // Save the main thread
        save();

        // NOTE: In case of a fork, restore the main thread state and acquire the GIL,
        // call the python post-fork handler and save the main thread again, releasing the GIL.
        pthread_atfork(NULL, NULL, restore);
        pthread_atfork(NULL, NULL, PyOS_AfterFork);
        pthread_atfork(NULL, NULL, save);

        registry.install("python", &python_t::create);
    }

    __attribute__((destructor)) void finalize() {
        restore();
        Py_Finalize();
    }
}
