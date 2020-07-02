# Ducky Detector Daemon
This is a daemon which should protect the user from pretty much every HID injection attack.
(If configured right)

## Compatibility Note:
This daemon depends on udev and the xkb extension to the x server. Systemd is not required
although you will have to write your own init script.

## Install:
```
git clone --recurse-submodules -j8 https://github.com/0xDEADC0DEx/duckydd
cd duckydd
mkdir bin
cd bin
cmake ..
make
```

If you use systemd then you can install the project with a service file like this:
`sudo make install`

## Configuration:
The config file should be located under /etc/duckydd/duckydd.conf.

__Note:__ The standard config the daemon is shipped with, should
protect against any injector which serves a virtual com port over
the same usb port which is used by the attack itself.
However if the device does __not__ serve a virtual com port
then the daemon will simply __ignore__ it.

The config file format is pretty simple.

\<parameter> \<option>

## Example config entries:
`blacklist 29`

`blacklist 97`

`blacklist 56`

`blacklist 100`

With this option you can configure which keys will lock the keyboard.

A list of all the keycodes which identify the keys can be found in
the input-event-codes header. If you don't want to search
for that header then you can use the bash script called getkey.sh.
The script will search for the header using locate and then list
all of the key macros and their keycode.


`maxtime 10s 0ns`

This will set the maximum time after which the device will be
removed from the watchlist. After that time period the daemon
will simply ignore all events that are generated from that event file.


`maxscore 0`

The so called score of an event file is an internal variable which depicts
how dangerous the event file is. If the daemon increments the score over the set maxscore
and a blacklisted key is pressed then it will grab the file descriptor that was opened
and thereby block any further keystrokes from being received from any other program
that was listening for events. 

At the moment it is only incremented if a device with the same
vendor id registers as a keyboard and a virtual com port.

Therefore if you leave it at 0 the daemon will lock all keyboards
which type a single blacklisted key and haven't timed out.
If you set it to 1 then it will only lock devices which have been registered
as a keyboard and a serial com port.


After the keyboard has been locked you have to replug it
to unlock it.

`keylogging 1`

Enables keylogging of potential attacks

__Note:__ The keylogger logs all keypresses that are read from the event file
until the specific keyboard times out. The keys pressed are then written to a
file called key.log into the path which the logpath variable is set to.

`logpath /`

Sets the path where every log file is saved in.

If the process is given the -d flag (daemonize) then it will also write
it's log messages to a file which is called out.log in the logpath directory.

Otherwise it will just use the directory for the keylog.

__Note:__ You have to set a full path because the daemon has
to be started as root. Currently the parser does not expand the string
using environment variables.

## Uninstall:
```
cd bin
sudo xargs rm < install_manifest.txt
sudo rm -rf /etc/duckydd
```

## Known issues:
If you get the message "No protocol specified" when starting the daemon as a service
then you need to add the user root to the list of trusted users of the x server.
To do this issue the following command:

`xhost local:root`
