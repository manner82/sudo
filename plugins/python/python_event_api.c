#include "sudo_python_module.h"

PyTypeObject *sudo_type_Event;

event_alloc_func *sudo_event_alloc = NULL;

static void _sudo_Event_register_impl(PyObject *py_self);

static struct sudo_plugin_event *
_sudo_Event_as_c_event(PyObject *py_event)
{   // TODO debug
    struct sudo_plugin_event * event = NULL;
    long long event_id = py_object_get_optional_attr_number(py_event, "__event_id");

    if (event_id >= 0) {
        event = (struct sudo_plugin_event *) event_id;
    } else {
        PyErr_Format(sudo_exc_SudoException, "sudo.Event is not associated with a sudo event");
    }

    return event;
}

struct sudo_plugin_event *
_sudo_event_create(void)
{
    if (sudo_event_alloc == NULL) {
        PyErr_Format(sudo_exc_SudoException, "sudo.Event.register: sudo event API is not available");
        return NULL;
    }

    struct sudo_plugin_event *event = (*sudo_event_alloc)();
    if (event == NULL) {
        PyErr_Format(sudo_exc_SudoException, "sudo.Event.register: failed to create event");
    }

    return event;
}

static void
_loopbreak(void)
{
    struct sudo_plugin_event * event = _sudo_event_create();
    if (event == NULL) {
        return;
    }

    event->loopbreak(event);
    event->free(event);
}

static bool
_sudo_Event_is_registered_to_sudo(PyObject *py_event)
{
    return PyObject_HasAttrString(py_event, "__event_id");
}

static void
_sudo_Event_ensure_deregistered_from_sudo(PyObject *py_event)
{
    if (!_sudo_Event_is_registered_to_sudo(py_event))
        return;

    struct sudo_plugin_event * event = _sudo_Event_as_c_event(py_event);
    if (event == NULL) {
        printf("XXX the impossible has happened\n");
        return;
    }

    event->free(event); // this also removes from the queue

    // ensure we'll not associated with the underlying sudo event any more
    if (PyObject_DelAttrString(py_event, "__event_id") < 0) {
        printf("XXX the impossible has happened2\n");
        return;
    }

    Py_CLEAR(py_event);  // remove the reference for the callback
}

void
_sudo_Event_callback(int fd, int what, void *closure)
{
    debug_decl(_sudo_Event_callback, PYTHON_DEBUG_CALLBACKS);
    // printf("XXX fd=%d what=%d\n", fd, what);

    PyObject *py_event = closure;
    (void) fd;

    // TODO why switching to correct interpreter seems not needed?

    PyObject *py_callback = NULL;

    Py_XINCREF(py_event);

    if (!PyObject_TypeCheck(py_event, sudo_type_Event)) {
        PyErr_Format(PyExc_TypeError, "sudo.Event.callback: the callback was called with wrong type");
        goto cleanup;
    }

    // we deregister before running the python callbacks, so they are able to register again
    _sudo_Event_ensure_deregistered_from_sudo(py_event);
    if (PyErr_Occurred())
        goto cleanup;

    #define CALL_METHOD(flag, method_name) \
    if ((what & flag) != 0) { \
        py_debug_python_call("sudo.Event", method_name, NULL, NULL, PYTHON_DEBUG_PY_CALLS); \
        PyObject *py_result = PyObject_CallMethod(py_event, method_name, NULL); \
        if (py_result == NULL) \
            goto cleanup; \
        Py_CLEAR(py_result); \
    }

    CALL_METHOD(SUDO_PLUGIN_EV_READ, "on_readable")
    CALL_METHOD(SUDO_PLUGIN_EV_WRITE, "on_writable")
    CALL_METHOD(SUDO_PLUGIN_EV_SIGNAL, "on_signal")
    CALL_METHOD(SUDO_PLUGIN_EV_TIMEOUT, "on_timeout")

    #undef CALL_METHOD

    // TODO add API for is_registered()
    long long persist = py_object_get_optional_attr_number(py_event, "__persist");
    if (persist == 1 && !_sudo_Event_is_registered_to_sudo(py_event)) {
        _sudo_Event_register_impl(py_event);
    }

cleanup:
    Py_CLEAR(py_callback);
    Py_CLEAR(py_event);

    if (PyErr_Occurred()) {
        py_log_last_error("Error while calling callback of sudo.Event");

        _loopbreak();
        if (PyErr_Occurred()) {
            py_log_last_error("Error while trying to stop the sudo event queue");
        }
    }

    debug_return;
}

static PyObject *
_sudo_Event__Init(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_Event__Init, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("sudo.Event", "__init__", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);
    PyObject *py_signal = NULL;

    static char *keywords[] = { "self", "signal", "read_fd", "write_fd", "persist", NULL };
    int signal = -1, read_fd = -1, write_fd = -1, persist = 0;
    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs, "O!|iiii:sudo.Event.__init__", keywords,
                                     sudo_type_Event, &py_self, &signal, &read_fd, &write_fd, &persist))
        goto cleanup;

    if (signal >= 0) {
        py_object_set_attr_number(py_self, "__signal", signal);
        if (PyErr_Occurred())
            goto cleanup;
    }

    if (read_fd >= 0) {
        py_object_set_attr_number(py_self, "__read_fd", read_fd);
        if (PyErr_Occurred())
            goto cleanup;
    }

    if (write_fd >= 0) {
        py_object_set_attr_number(py_self, "__write_fd", write_fd);
        if (PyErr_Occurred())
            goto cleanup;
    }

    py_object_set_attr_number(py_self, "__persist", persist);

cleanup:
    Py_CLEAR(py_signal);
    Py_CLEAR(py_empty);

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

static PyObject *
_sudo_Event__Del(PyObject *py_self, PyObject *py_args)
{
    debug_decl(_sudo_Event__Del, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("sudo.Event", "__del__", py_args, NULL, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);
    if (!PyArg_ParseTuple(py_args ? py_args : py_empty, "O!:sudo.Event.__del__",
                                     sudo_type_Event, &py_self))
        goto cleanup;

    _sudo_Event_ensure_deregistered_from_sudo(py_self);

    printf("XXX Event removed successfully\n");

cleanup:
    Py_CLEAR(py_empty);
    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

static int
_verify_has_callable(PyObject *py_object, const char *cb_name)
{
    debug_decl(_verify_has_callable, PYTHON_DEBUG_INTERNAL);
    PyObject *py_callback = NULL;
    int rc = SUDO_RC_ERROR;

    if (!PyObject_HasAttrString(py_object, cb_name)) {
        PyErr_Format(sudo_exc_SudoException, "Event has no callback '%s'", cb_name);
        goto cleanup;
    }

    py_callback = PyObject_GetAttrString(py_object, cb_name);
    if (py_object == NULL)
        goto cleanup;

    if (!PyCallable_Check(py_callback)) {
        PyErr_Format(sudo_exc_SudoException, "Event callback '%s' is not callable", cb_name);
        goto cleanup;
    }

    rc = SUDO_RC_OK;

cleanup:
    Py_CLEAR(py_callback);
    debug_return_int(rc);
}

static PyObject *
_sudo_Event_register(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_Event_register, PYTHON_DEBUG_C_CALLS);
    py_debug_python_call("sudo.Event", "register", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    static char *keywords[] = { "self", "timeout", NULL };

    PyObject *py_empty = PyTuple_New(0);
    PyObject *py_timeout = NULL; // Note: this will contain borrowed reference

    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs, "O!|O:Event.register", keywords,
                                     sudo_type_Event, &py_self, &py_timeout))
        goto cleanup;

    if (_sudo_Event_is_registered_to_sudo(py_self)) {
        PyErr_Format(sudo_exc_SudoException, "sudo.Event.register: event is already registered");
        goto cleanup;
    }

    if (py_timeout) {
        if (PyObject_SetAttrString(py_self, "__timeout", py_timeout) < 0)
            goto cleanup;
    }

    _sudo_Event_register_impl(py_self);

cleanup:
    Py_CLEAR(py_empty);

    if (PyErr_Occurred()) {
        debug_return_ptr(NULL);
    }

    debug_return_ptr_pynone;
}

static PyObject *
_sudo_Event_deregister(PyObject *py_self, PyObject *py_args)
{
    debug_decl(_sudo_Event_deregister, PYTHON_DEBUG_C_CALLS);
    py_debug_python_call("sudo.Event", "deregister", py_args, NULL, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);
    if (!PyArg_ParseTuple(py_args ? py_args : py_empty, "O!:sudo.Event.deregister",
                                     sudo_type_Event, &py_self))
        goto cleanup;

    _sudo_Event_ensure_deregistered_from_sudo(py_self);

cleanup:
    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

static void
_sudo_Event_register_impl(PyObject *py_self)
{
    debug_decl(_sudo_Event_deregister, PYTHON_DEBUG_C_CALLS);

    struct sudo_plugin_event *event = NULL;
    PyObject *py_timeout = NULL;
    long long signal = -1, read_fd = -1, write_fd = -1;
    int sudo_events = 0, sudo_event_fd = -1;

    py_timeout = py_object_get_optional_attr(py_self, "__timeout", Py_None);
    if (py_timeout == NULL)
        goto cleanup;

    struct timespec timeout = { -1, -1 };
    struct timespec *timeout_ptr = NULL; // == infinite
    if (py_timeout != Py_None) {
        timeout_ptr = &timeout;
        py_timedelta_to_timespec(py_timeout, timeout_ptr);
        if (PyErr_Occurred())
            goto cleanup;
    }

    signal = py_object_get_optional_attr_number(py_self, "__signal");
    if (PyErr_Occurred())
        goto cleanup;

    read_fd = py_object_get_optional_attr_number(py_self, "__read_fd");
    if (PyErr_Occurred())
        goto cleanup;

    write_fd = py_object_get_optional_attr_number(py_self, "__write_fd");
    if (PyErr_Occurred())
        goto cleanup;

    if (signal >= 0) {
        if (_verify_has_callable(py_self, "on_signal") != SUDO_RC_OK)
            goto cleanup;

        sudo_events |= SUDO_PLUGIN_EV_SIGNAL;
        sudo_event_fd = (int)signal;

        if (read_fd >= 0 || write_fd >= 0) {
            PyErr_Format(sudo_exc_SudoException, "You can not register the same "
                                                 "event for both signal and file operation. ");
            goto cleanup;
        }
    }

    if (read_fd >= 0) {
        if (_verify_has_callable(py_self, "on_readable") != SUDO_RC_OK)
            goto cleanup;

        sudo_events |= SUDO_PLUGIN_EV_READ;
        sudo_event_fd = (int)read_fd;
    }

    if (write_fd >= 0) {
        if (_verify_has_callable(py_self, "on_writable") != SUDO_RC_OK)
            goto cleanup;

        sudo_events |= SUDO_PLUGIN_EV_WRITE;
        sudo_event_fd = (int)write_fd;

        if (write_fd >= 0 && write_fd != read_fd) {
            PyErr_Format(sudo_exc_SudoException, "You can not register the same event for "
                                                 "read / write on different file descriptors.");
            goto cleanup;
        }
    }

    if (timeout_ptr) {
        if (_verify_has_callable(py_self, "on_timeout") != SUDO_RC_OK)
            goto cleanup;

        sudo_events |= SUDO_PLUGIN_EV_TIMEOUT;
    }

    if (sudo_events == 0) {
        PyErr_Format(sudo_exc_SudoException, "No event type is specified");
        goto cleanup;
    }

    sudo_debug_printf(SUDO_DEBUG_TRACE, "sudo.Event.register: fd='%d', events='%d', timeout='%ld secs %ld nsecs'\n",
                      sudo_event_fd, sudo_events, timeout.tv_sec, timeout.tv_nsec);

    event = _sudo_event_create();
    if (event == NULL)
        goto cleanup;

    // event is never persistant, otherwise we would need to track logic when it gets destroyed
    if (event->set(event, sudo_event_fd, sudo_events, &_sudo_Event_callback, py_self) != SUDO_RC_OK) {
        PyErr_Format(sudo_exc_SudoException, "sudo.Event.register: failed to set event parameters");
        goto cleanup;
    }

    if (event->add(event, timeout_ptr) != SUDO_RC_OK) {
        PyErr_Format(sudo_exc_SudoException, "sudo.Event.register: failed to register event to sudo mainloop");
        goto cleanup;
    }

    py_object_set_attr_number(py_self, "__event_id", (long long)event);
    Py_INCREF(py_self); // for the callback

cleanup:
    Py_CLEAR(py_timeout);

    if (PyErr_Occurred() && event) {
        event->free(event);
    }

    debug_return;
}

PyObject *
_sudo_Event_loopbreak(PyObject *py_self, PyObject *py_args)
{
    debug_decl(_sudo_Event_loopbreak, PYTHON_DEBUG_C_CALLS);
    py_debug_python_call("sudo.Event", "loopbreak", py_args, NULL, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);

    // just to verify (we do not use it)
    if (!PyArg_ParseTuple(py_args ? py_args : py_empty, "O!:sudo.Event.deregister",
                                     sudo_type_Event, &py_self))
        goto cleanup;

    _loopbreak();

cleanup:
    Py_CLEAR(py_empty);

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

static PyMethodDef _sudo_Event_class_methods[] =
{
    {"__init__", (PyCFunction)_sudo_Event__Init,
                 METH_VARARGS | METH_KEYWORDS,
                 "Encapsulates a callback waiting for some event"},
    {"__del__", (PyCFunction)_sudo_Event__Del,
                 METH_VARARGS,
                 ""},
    {"register", (PyCFunction)_sudo_Event_register,
                 METH_VARARGS | METH_KEYWORDS,
                 "Register the event in the sudo event loop"},
    {"deregister", (PyCFunction)_sudo_Event_deregister,
                 METH_VARARGS,
                 "Remove the event from the sudo event loop"},
    {"loopbreak", (PyCFunction)_sudo_Event_loopbreak,
                 METH_VARARGS,
                 ""},
    {NULL, NULL, 0, NULL}
};

int
sudo_module_register_event_api(PyObject *py_module)
{
    // TODO can we get different event_alloc per plugin?

    debug_decl(sudo_module_register_event_api, PYTHON_DEBUG_INTERNAL);
    int rc = SUDO_RC_ERROR;
    PyObject *py_class = NULL;

    py_class = sudo_module_create_class("sudo.Event", _sudo_Event_class_methods);
    if (py_class == NULL)
        goto cleanup;

    if (PyModule_AddObject(py_module, "Event", py_class) < 0) {
        goto cleanup;
    }

    Py_CLEAR(sudo_type_Event);
    sudo_type_Event = (PyTypeObject *)py_class;
    py_class = NULL;
    Py_INCREF(sudo_type_Event);

    rc = SUDO_RC_OK;

cleanup:
    Py_CLEAR(py_class);
    debug_return_int(rc);
}


// TODO how to support
//   pending ?
//   setbase NOPE
//   fd
//   loopbreak
