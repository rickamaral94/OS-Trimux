#!/bin/sh
# Runs before every menu start (boot and after each game): restores the
# firmware's default gamepad mode and keeps the RAM working folder small.
rm -f /tmp/trimui_inputd/input_no_dpad /tmp/trimui_inputd/input_dpad_to_joystick
rm -rf /tmp/trimux/cache 2>/dev/null
exit 0
