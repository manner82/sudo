/*
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2019-2020 Robert Manner <robert.manner@oneidentity.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This is an open source non-commercial project. Dear PVS-Studio, please check it.
 * PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
 */

#include "sudo_python_module.h"

#include "sudo_dso.h"
#include "sudo_debug.h"
#include "sudo_fatal.h"
#include "sudo_gettext.h"
#include "sudo_plugin.h"
#include "sudo_util.h"

PyTypeObject *sudo_type_SudoersPolicyPlugin;
static struct policy_plugin *policy_plugin = NULL;


// TODO this is code copy
#define _PATH_SUDO_CONF "/etc/sudo.conf"
struct generic_plugin {
    unsigned int type;
    unsigned int version;
    /* the rest depends on the type... */
};

static bool
sudo_load_policy_plugin(const char *path)
{
    struct generic_plugin *plugin;
    void *handle = NULL;
    bool ret = false;
    bool quiet = false;
    debug_decl(sudo_load_policy_plugin, SUDO_DEBUG_PLUGIN);

    const char *symbol_name = "sudoers_policy";
    /* Check plugin owner/mode and fill in path
    if (!sudo_check_plugin(info, path, sizeof(path)))
        goto done; TODO
      */

    /* Open plugin and map in symbol */
    handle = sudo_dso_load(path, SUDO_DSO_LAZY|SUDO_DSO_GLOBAL);
    if (!handle) {
        if (!quiet) {
            const char *errstr = sudo_dso_strerror();
            sudo_warnx(U_("error in %s while loading plugin \"%s\""),
                _PATH_SUDO_CONF, symbol_name);
            sudo_warnx(U_("unable to load %s: %s"), path,
                errstr ? errstr : "unknown error");
        }
        goto done;
    }
    plugin = sudo_dso_findsym(handle, symbol_name);
    if (!plugin) {
        if (!quiet) {
            sudo_warnx(U_("error in %s, while loading plugin \"%s\""),
                _PATH_SUDO_CONF, symbol_name);
            sudo_warnx(U_("unable to find symbol \"%s\" in %s"),
                symbol_name, path);
        }
        goto done;
    }

    if (SUDO_API_VERSION_GET_MAJOR(plugin->version) != SUDO_API_VERSION_MAJOR) {
        if (!quiet) {
            sudo_warnx(U_("error in %s, while loading plugin \"%s\""),
                _PATH_SUDO_CONF, symbol_name);
            sudo_warnx(U_("incompatible plugin major version %d (expected %d) found in %s"),
                SUDO_API_VERSION_GET_MAJOR(plugin->version),
                SUDO_API_VERSION_MAJOR, path);
        }
        goto done;
    }

    switch (plugin->type) {
    case SUDO_POLICY_PLUGIN:
        if (policy_plugin) {
		    sudo_warnx(U_("ignoring duplicate plugin \"%s\" in %s"),
			           symbol_name, _PATH_SUDO_CONF);
        } else {
            policy_plugin = (struct policy_plugin *)plugin;
        }
        break;
    default:
        if (!quiet) {
            sudo_warnx(U_("error in %s, while loading plugin \"%s\""),
                _PATH_SUDO_CONF, symbol_name);
            sudo_warnx(U_("unknown plugin type %d found in %s"), plugin->type, path);
        }
        goto done;
    }

    /* Handle is either in use or has been closed. */
    handle = NULL;

    ret = true;

done:
    if (handle != NULL)
        sudo_dso_unload(handle);
    debug_return_bool(ret);
}


/*
__init__(self, user_env: Tuple[str, ...], settings: Tuple[str, ...],
    version: str, user_info: Tuple[str, ...],
    plugin_options: Tuple[str, ...])
 */
static PyObject *
_sudo_SudoersPolicyPlugin__Init(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__Init, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "__init__", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);

    PyObject *py_user_env = NULL, *py_settings = NULL,
             *py_user_info = NULL, *py_plugin_options = NULL;
    char **user_env = NULL, **settings = NULL, **user_info = NULL, **plugin_options = NULL;
    const char *version = NULL;
    const char *error_msg = NULL;

    static const char *keywords[] = { "self", "user_env", "settings", "version", "user_info", "plugin_options", NULL };
    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs, "OOOsOO|:sudo.SudoersPolicyPlugin", (char **)keywords,
                                     &py_self, &py_user_env, &py_settings, &version, &py_user_info, &py_plugin_options))
    {
        py_log_last_error2("Failed to parse arguments of SudoersPolicyPlugin.__init__", false);
        goto cleanup;
    }

    sudo_debug_printf(SUDO_DEBUG_TRACE, "Parsed arguments: self='%p' user_env='%p' settings='%p' version='%s' user_info='%p' plugin_options='%p'",
                      (void *)py_self, py_user_env, py_settings, version, py_user_info, py_plugin_options);

    user_env = py_str_array_from_tuple(py_user_env);
    settings = py_str_array_from_tuple(py_settings);
    user_info = py_str_array_from_tuple(py_user_info);
    plugin_options = py_str_array_from_tuple(py_plugin_options);

    if (user_env == NULL || settings == NULL ||
        user_info == NULL || plugin_options == NULL)
        goto cleanup;

    if (version == NULL)  // TODO message
        goto cleanup;

    const char *policy_plugin_path = "/usr/lib/sudo/sudoers.so";
    sudo_debug_printf(SUDO_DEBUG_TRACE, "Linking to the plugin");
    sudo_load_policy_plugin(policy_plugin_path);
    if (policy_plugin == NULL) {
        sudo_debug_printf(SUDO_DEBUG_TRACE, "Failed to load policy plugin");
        PyErr_Format(sudo_exc_SudoException, "%s: failed to load policy plugin from '%s'",
                     __func__, policy_plugin_path);
        goto cleanup;
    }

    sudo_debug_printf(SUDO_DEBUG_TRACE, "Calling plugin open");
    int ret = policy_plugin->open(SUDO_POLICY_PLUGIN, py_ctx.sudo_conv, py_ctx.sudo_log,
                                  settings, user_info, user_env, plugin_options, &error_msg);
    sudo_debug_printf(SUDO_DEBUG_TRACE, "Plugin open returned: %d", ret);
    if (ret != SUDO_RC_OK) {
        PyErr_Format(sudo_exc_SudoException, "%s: open of sudoers policy plugin returned '%d' - '%s'",
                     __func__, ret, error_msg == NULL ? "" : error_msg);
    }

cleanup:
    Py_CLEAR(py_empty);
    str_array_free(&user_env);
    str_array_free(&settings);
    str_array_free(&user_info);
    str_array_free(&plugin_options);

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

/*
    check_policy(self, argv: Tuple[str, ...], env_add: Tuple[str, ...]) -> (rc, command_info_out, argv_out, user_env_out)
 */
static PyObject *
_sudo_SudoersPolicyPlugin__check_policy(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__check_policy, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "check_policy", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);

    PyObject *py_argv = NULL, *py_env_add = NULL;
    PyObject *py_result = NULL, *py_command_info_out = NULL, *py_argv_out = NULL, *py_user_env_out = NULL;

    char **argv = NULL, **env_add = NULL;
    char **command_info_out = NULL, **user_env_out = NULL, **argv_out = NULL;
    const char *error_str = NULL;
    int ret = -1;

    static const char *keywords[] = { "self", "argv", "env_add", NULL };
    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs,
                                     "OOO|:sudo.SudoersPolicyPlugin.check_policy",
                                     (char **)keywords, &py_self, &py_argv, &py_env_add))
    {
        py_log_last_error2("Failed to parse arguments of SudoersPolicyPlugin.__init__", false);
        goto cleanup;
    }

    argv = py_str_array_from_tuple(py_argv);
    env_add = py_str_array_from_tuple(py_env_add);

    if (argv == NULL || env_add == NULL)
        goto cleanup;

    if (policy_plugin == NULL) {
        PyErr_Format(sudo_exc_SudoException, "%s: Constructor was not called!", __func__);
        goto cleanup;
    }

    sudo_debug_printf(SUDO_DEBUG_TRACE, "Calling sudoers policy plugin check_policy");
    int argc = PyTuple_Size(py_argv);
    ret = policy_plugin->check_policy(argc, argv, env_add, &command_info_out,
                                      &argv_out, &user_env_out, &error_str);
    sudo_debug_printf(SUDO_DEBUG_TRACE, "sudoers policy plugin check_policy returned: %d - %s",
                      ret, error_str == NULL ? "(null)" : error_str);

    py_command_info_out = py_str_array_to_tuple(command_info_out);
    py_argv_out = py_str_array_to_tuple(argv_out);
    py_user_env_out = py_str_array_to_tuple(user_env_out);
    if (py_command_info_out == NULL || py_argv_out == NULL || py_user_env_out == NULL)
        goto cleanup;

    py_result = Py_BuildValue("(iOOO)", ret, py_command_info_out, py_argv_out, py_user_env_out);

cleanup:
    Py_CLEAR(py_empty);
    Py_CLEAR(py_command_info_out);
    Py_CLEAR(py_argv_out);
    Py_CLEAR(py_user_env_out);

    str_array_free(&argv);
    str_array_free(&env_add);
    /* TODO this is double free. why?
    str_array_free(&command_info_out);
    str_array_free(&argv_out);
    str_array_free(&user_env_out);
     */

    if (PyErr_Occurred()) {
        Py_CLEAR(py_result);
        debug_return_ptr(NULL);
    }

    debug_return_ptr(py_result);
}

/*
    init_session(self, user_pwd: Tuple, user_env: Tuple[str, ...])
 */
static PyObject *
_sudo_SudoersPolicyPlugin__init_session(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__init_session, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "init_session", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    // TODO

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

/*
    list(self, argv: Tuple[str, ...], is_verbose: int, user: str)
 */
static PyObject *
_sudo_SudoersPolicyPlugin__list(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__list, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "list", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    // TODO

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

/*
    validate(self)
 */
static PyObject *
_sudo_SudoersPolicyPlugin__validate(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__validate, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "validate", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    if (policy_plugin == NULL) {
        PyErr_Format(sudo_exc_SudoException, "%s: Constructor was not called!", __func__);
        goto cleanup;
    }

    const char *error_str = NULL;
    int rc = policy_plugin->validate(&error_str);
    if (rc != SUDO_RC_OK) {
        PyErr_Format(sudo_exc_SudoException, "%s: Error during validate %d - %s", __func__, rc, error_str);
        goto cleanup;
    }

cleanup:
    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

/*
    invalidate(self, remove: int)
 */
static PyObject *
_sudo_SudoersPolicyPlugin__invalidate(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__invalidate, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "invalidate", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    PyObject *py_empty = PyTuple_New(0);
    static const char *keywords[] = { "self", "remove", NULL };
    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs,
                                     "Oi|:sudo.SudoersPolicyPlugin.invalidate",
                                     (char **)keywords, &py_self, &remove))
    {
        goto cleanup;
    }

    if (policy_plugin == NULL) {
        PyErr_Format(sudo_exc_SudoException, "%s: Constructor was not called!", __func__);
        goto cleanup;
    }

    policy_plugin->invalidate(remove);

cleanup:
    Py_CLEAR(py_empty);

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}

/*
    show_version(self, is_verbose: int)
 */
static PyObject *
_sudo_SudoersPolicyPlugin__show_version(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__show_version, PYTHON_DEBUG_C_CALLS);
    int rc = SUDO_RC_ERROR;
    int is_verbose = 0;

    py_debug_python_call("SudoersPolicyPlugin", "show_version", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    PyObject *py_empty = PyTuple_New(0);
    static const char *keywords[] = { "self", "is_verbose", NULL };
    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs,
                                     "Oi|:sudo.SudoersPolicyPlugin.check_policy",
                                     (char **)keywords, &py_self, &is_verbose))
    {
        py_log_last_error2("Failed to parse arguments of SudoersPolicyPlugin.show_version", false);
        goto cleanup;
    }

    if (policy_plugin == NULL) {
        PyErr_Format(sudo_exc_SudoException, "%s: Constructor was not called!", __func__);
        goto cleanup;
    }

    rc = policy_plugin->show_version(is_verbose);  // TODO raise exception instead?

cleanup:
    Py_CLEAR(py_empty);

    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr(PyLong_FromLong(rc));
}

/*
    close(self, exit_status: int, error: int)
 */
static PyObject *
_sudo_SudoersPolicyPlugin__close(PyObject *py_self, PyObject *py_args, PyObject *py_kwargs)
{
    debug_decl(_sudo_SudoersPolicyPlugin__close, PYTHON_DEBUG_C_CALLS);

    py_debug_python_call("SudoersPolicyPlugin", "close", py_args, py_kwargs, PYTHON_DEBUG_C_CALLS);

    int exit_status = 0, error = 0;
    PyObject *py_empty = PyTuple_New(0);
    static const char *keywords[] = { "self", "exit_status", "error", NULL };
    if (!PyArg_ParseTupleAndKeywords(py_args ? py_args : py_empty, py_kwargs,
                                     "Oii|:sudo.SudoersPolicyPlugin.close",
                                     (char **)keywords, &py_self, &exit_status, &error))
    {
        goto cleanup;
    }

    if (policy_plugin == NULL) {
        PyErr_Format(sudo_exc_SudoException, "%s: Constructor was not called!", __func__);
        goto cleanup;
    }

    int rc = policy_plugin->close(exit_status, error);
    if (rc != SUDO_RC_OK) {
        PyErr_Format(sudo_exc_SudoException, "%s: close of sudoers policy plugin returned '%d' - '%s'",
                     __func__, ret, error_msg == NULL ? "" : error_msg);
    }

cleanup:
    Py_CLEAR(py_empty);
    if (PyErr_Occurred())
        debug_return_ptr(NULL);

    debug_return_ptr_pynone;
}


static PyMethodDef _sudo_SudoersPolicyPlugin_class_methods[] =
{
    {"__init__", (PyCFunction)_sudo_SudoersPolicyPlugin__Init,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"check_policy", (PyCFunction)_sudo_SudoersPolicyPlugin__check_policy,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"init_session", (PyCFunction)_sudo_SudoersPolicyPlugin__init_session,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"list", (PyCFunction)_sudo_SudoersPolicyPlugin__list,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"validate", (PyCFunction)_sudo_SudoersPolicyPlugin__validate,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"invalidate", (PyCFunction)_sudo_SudoersPolicyPlugin__invalidate,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"show_version", (PyCFunction)_sudo_SudoersPolicyPlugin__show_version,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {"close", (PyCFunction)_sudo_SudoersPolicyPlugin__close,
                 METH_VARARGS | METH_KEYWORDS,
                 "TODO"},
    {NULL, NULL, 0, NULL}
};


// Needs to be called after base class (sudo.Plugin) was created already!
int
sudo_module_register_sudoers_policy_plugin(PyObject *py_module)
{
    debug_decl(_sudo_module_register_sudoers_policy_plugin, PYTHON_DEBUG_INTERNAL);
    int rc = SUDO_RC_ERROR;
    PyObject *py_class = NULL;

    py_class = sudo_module_create_class("sudo.SudoersPolicyPlugin",
        _sudo_SudoersPolicyPlugin_class_methods, (PyObject *)sudo_type_Plugin);
    if (py_class == NULL)
        goto cleanup;

    if (PyModule_AddObject(py_module, "SudoersPolicyPlugin", py_class) < 0) {
        goto cleanup;
    }

    Py_INCREF(py_class);
    rc = SUDO_RC_OK;

    Py_CLEAR(sudo_type_SudoersPolicyPlugin);
    sudo_type_SudoersPolicyPlugin = (PyTypeObject *)py_class;
    Py_INCREF(sudo_type_SudoersPolicyPlugin);

cleanup:
    Py_CLEAR(py_class);
    debug_return_int(rc);
}