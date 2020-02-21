import sudo
import sys
import os
import uuid
from socket import gethostbyname, gethostname


class AAPluginWrapper(sudo.Plugin):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.user_info = sudo.options_as_dict(self.user_info)
        self.plugin_options = sudo.options_as_dict(self.plugin_options)
        self.settings = sudo.options_as_dict(self.settings)

        self._cookie = {}
        self._session_cookie = {}
        self._name = ""

        try:
            hostname = gethostname()
        except Exception:
            hostname = "localhost.localdomain"

        try:
            ip = gethostbyname(hostname)
        except Exception:
            ip = "127.0.0.1"

        self._info = {
            "session_id": "sudo-{}".format(uuid.uuid1()),
            "protocol": "sudo",
            "connection_name": "sudo-{}".format(hostname),
            "client_ip": ip,
            "client_port": None,
            "client_hostname": hostname,
            "gateway_user": self._invoking_user(),
            "gateway_domain": hostname,
            "server_username": self._runas_user(),
            "server_domain": hostname,
            "key_value_pairs": {}
        }

        try:
            self.plugin = self._load_aa_plugin()
        except sudo.PluginException as e:
            sudo.log_error(str(e))
            raise

    def check(self, command_info, run_argv, run_env):
        try:
            while True:
                auth_result = self.plugin.authenticate(
                    self._cookie, self._session_cookie, **self._info)

                sudo.debug(sudo.DEBUG.DIAG, "AA Plugin returned: "
                           "{}".format(auth_result))

                rc = self._handle_auth_result(auth_result)
                if rc is not None:
                    return rc
        except sudo.PluginException as e:
            sudo.log_error(str(e))
            raise

    def show_version(self, verbose):
        sudo.log_info("Using AAPlugin: {} version {} (loaded from "
                      "'{}')".format(self._name, self._version,
                                     self._plugin_path))

    def _handle_auth_result(self, auth_result):
        self._cookie = auth_result.get("cookie", self._cookie)
        self._session_cookie = auth_result.get("session_cookie",
                                               self._session_cookie)

        verdict = auth_result.get('verdict')

        if verdict == 'ACCEPT':
            return sudo.RC.ACCEPT

        if verdict == 'DENY':
            raise sudo.PluginReject("AAPlugin {}: denied".format(
                                    self._name))

        if verdict == 'NEEDINFO':
            return self._handle_auth_result_need_info(auth_result)

        raise sudo.PluginError("Failed to understand verdict "
                               "returned by AAPlugin")

    def _handle_auth_result_need_info(self, auth_result):
        question = auth_result.get('question')

        try:
            result_name, msg, disable_echo = question
        except Exception:
            raise sudo.PluginError("Failed to understand AAPlugin question "
                                   "'{}'".format(question))

        conv_type = sudo.CONV.PROMPT_ECHO_ON if disable_echo \
            else sudo.CONV.PROMPT_ECHO_OFF
        resp, = sudo.conv(sudo.ConvMessage(conv_type, msg, -1))
        self._info["key_value_pairs"][result_name] = resp
        return None

    def _load_aa_plugin(self):
        self._plugin_path = self.plugin_options.get("AAPlugin")

        if not self._plugin_path:
            raise sudo.PluginError("No AAPlugin was specified")

        if not os.path.isabs(self._plugin_path):
            base_path = os.path.dirname(self.settings.get('plugin_path', '/'))
            self._plugin_path = os.path.join(
                base_path, "python", "aa", self._plugin_path)

        manifest = self._load_manifest()

        self._name = manifest.get("name",
                                  os.path.basename(self._plugin_path))
        if manifest.get('type') != "aa":
            raise sudo.PluginError("The plugin {} ({}) is not "
                                   "an AAPlugin".format(self._name,
                                                        self._plugin_path))
        self._version = manifest.get('version', '')

        config = self._load_config()

        plugin_pkg_name = manifest.get("entry_point", "main.py")
        plugin_pkg_name = os.path.splitext(plugin_pkg_name)[0]
        plugin_cls_name = "Plugin"

        sys.path.insert(0, self._plugin_path)
        aa_plugin_cls = getattr(
            __import__(plugin_pkg_name, fromlist=[plugin_cls_name]),
            plugin_cls_name)

        plugin = aa_plugin_cls(config)
        return plugin

    def _load_config(self):
        try:
            path = "/etc/sudo.aa.d/{}.conf".format(self._name)
            with open(path, "r") as f:
                return f.read()
        except IOError as e:
            raise sudo.PluginError("Failed to load configuration of AAPlugin: "
                                   "{}".format(str(e)))

    def _load_manifest(self):
        manifest = {}
        try:
            path = os.path.join(self._plugin_path, "MANIFEST")
            with open(path, "r") as f:
                # TODO print line number on error
                for cnt, line in enumerate(f):
                    key, value = line.split(":", 1)
                    manifest[key] = value.strip()

        except Exception as e:
            msg = "Failed to load manifest of AAPlugin: {}".format(str(e))
            raise sudo.PluginError(msg)

        return manifest

    def _invoking_user(self):
        user = self.user_info.get("user")
        if not user:
            raise sudo.PluginError("Unable to determine invoking user")
        return user

    def _runas_user(self):
        return self.settings.get("runas_user") or "root"
