import sudo

import signal
from datetime import timedelta


# An example event implemented as a class
class TimeoutEvent(sudo.Event):
    def __init__(self):
        # The default event type is "timeout"
        super().__init__()

        self.reset_timer()
        self.register()

    def reset_timer(self):
        self._timeout_counter = 10

    def register(self):
        # This registers the event in the sudo mainloop with one second timeout
        super().register(timeout=timedelta(seconds=1))

    def on_timeout(self):
        sudo.log_info("Seconds left: {}".format(self._timeout_counter))

        if self._timeout_counter > 0:
            self._timeout_counter -= 1
            # Reregister for the next timeout:
            # (we could use "persist=True" instead in the constructor)
            self.register()
        else:
            self.loopbreak()  # Example on how to terminate the process


class EventsIOPlugin(sudo.Plugin):
    """
    An example sudo plugin demonstrating the sudo event API capabilities.

    You can install it as an IO plugin by adding the following to sudo.conf:
        Plugin python_io python_plugin.so \
            ModulePath=<path>/example_events_timer.py \
            ClassName=EventsIOPlugin

    After that, run a program with sudo which runs for a long time:
        sudo sleep 120s

    The plugin will count down until zero each second and then terminates
    the process.

    If you send a SIGCONT signal during the count down, you can restart
    the timer:
        pkill -CONT sudo
    """

    def open(self, argv, command_info):
        # Wait for timeout example:
        self.timeout_event = TimeoutEvent()

        # This is an example on how to run something if sudo gets a signal
        # (SIGCONT). It also demonstrates how to implement the callback
        # as a function, so you do not need a class.
        # Note that events do not override the default signal handlers, so
        # after running the event callback, the default signal action will
        # trigger anyway. For example, if you register for a TERM
        # signal, sudo will quit after calling our signal handler.
        signal_event = sudo.Event(signal=signal.SIGCONT, persist=True)
        signal_event.on_signal = self._on_signal
        signal_event.register()  # We do not specify a timeout (== unlimited)

    def _on_signal(self):
        sudo.log_info("Got a CONT signal, restarting the count down")
        self.timeout_event.reset_timer()
