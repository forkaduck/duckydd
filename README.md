# Ducky Detector Daemon
This daemon protects from pretty much every HID injection attack.

## Compatibility Note:
This daemon depends on the following libraries:
```
udev
xkbcommon
xkbcommon-x11
xcb
libc
```

At the moment you will have to link against the x libraries even if they are
not used. But you can disable the use of the x keymaps retrieved from the x server
in the config later on.

Systemd is not required although you will have to write your own init script.

## Install:
```
git clone --recurse-submodules -j8 https://github.com/0xDEADC0DEx/duckydd
cd duckydd
mkdir build
cd build
cmake ..
make
```

If you use Systemd then you can install the project with a service file like this:

`sudo make install`

## Arguments:
```
Usage: duckydd [Options]
		-c <file>	Specify a config file path
		-d		    Daemonize the process
		-v		    Increase verbosity of the console output (The maximum verbosity is 2)
		-h		    Shows this help section
```

__Important Note:__
THE -v OPTION IS FOR DEBUGGING! 
DISABLE IT IF YOU DON'T NEED IT BECAUSE IT COULD POTENTIALY LOG AND THEIRFORE EXPOSE PASSWORDS!

## Config entries:
If you installed the project with the command above then the config file should be located under /etc/duckydd/duckydd.conf.

The file format is as follows:
\<parameter> \<option>


`minavrg 0s 7327733ns`

Sets the minimum average difference time between keystrokes. If the minium average of the
currently typed string is smaller than the set value then the score will be incremented.


`maxscore 0`

The so-called "score" of an event file is an internal variable which depicts
how dangerous the HID device is. If the daemon increments the score over the set maxscore
and a key is pressed then it will grab the file descriptor that was opened
and thereby block any further keystrokes from being received by any other program
that was listening for events. 

At the moment it is incremented if a device:
with the same major and minor id registers as a input device and a virtual com port.
types faster than the allowed maximum average.

After the keyboard has been locked you have to replug it
to unlock it.


`logpath /`

Sets the path where every log file is saved in.

If the process is passed the -d flag (daemonize) then it will also write
its log messages to a file which is called out.log in the logpath directory.

Otherwise it will just use the directory for the keylog file which is called key.log.

__Note:__ You have to set a full path because the daemon has
to be started as root. Currently the parser does not expand the string
using environment variables.


`usexkeymaps 1`

If this option is enabled the daemon will first try to initalize the x dependent keymap.
If the use of the x keymap fails then the daemon falls back to kernel keymaps.
At the moment the use of the kernel keymaps is **experimental** although alphanumberic characters
of the english language work.

## Uninstall:
```
cd bin
sudo xargs rm < install_manifest.txt
sudo rm -rf /etc/duckydd
```

## Known issues:
If you get the message "No protocol specified" when starting the daemon as a service
then you need to add the user root to the list of trusted users of the x server. This
needs to be done on boot but after the x server has started.
To do this issue the following command as the local user:

`xhost local:root`

You will have to restart the daemon after doing this.
