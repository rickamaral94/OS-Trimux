#!/bin/sh
# Same as the firmware's /usr/trimui/bin/premainui.sh (v1.1.1): restore the
# default input mode of trimui_inputd before showing a menu.
rm -f /tmp/trimui_inputd/input_no_dpad
rm -f /tmp/trimui_inputd/input_dpad_to_joystick
exit 0
