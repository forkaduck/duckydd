# Ducky Detector Daemon
This daemon protects you from pretty much every HID injection attack.
(If configured correctly)

## Compatibility Note:
This daemon depends on Udev and the XKB extension to the x-server. Systemd is not required
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

If you use Systemd then you can install the project with a service file like this:

`sudo make install`

## Known issues:
If you get the message "No protocol specified" when starting the daemon as a service
then you need to add the user root to the list of trusted users of the x server. This
needs to be done on boot but after the x server has started.
To do this issue the following command as the local user:

`xhost local:root`


## Configuration:
The config file should be located under /etc/duckydd/duckydd.conf.

__Note:__ The standard config the daemon is shipped with should
protect against any injector which serves a virtual com port over
the same USB port which is used by the attack itself.
However, if the device does __not__ serve a virtual com port
then the daemon will simply __ignore__ it.

You can change this behavior with the `maxscore` option.

The config file format is pretty simple.

\<parameter> \<option>

__Note:__ THE -v OPTION IS FOR DEBUGGING! 
DISABLE IT IF YOU DON'T NEED IT BECAUSE IT COULD POTENTIALY LOG PASSWORDS!

## Config entries:
`minavrg 0s 7327733ns`

Sets the minimum average difference time between keystrokes. If the minium average of the
currently typed string is smaller than the set value then the score will be incremented.


`maxscore 0`

The so-called "score" of an event file is an internal variable which depicts
how dangerous the event file is. If the daemon increments the score over the set maxscore
and a blacklisted key is pressed then it will grab the file descriptor that was opened
and thereby block any further keystrokes from being received by any other program
that was listening for events. 

At the moment it is only incremented if a device with the same
major and minor id registers as a keyboard and a virtual com port.

Therefore if you leave it at 0 the daemon will lock all keyboards
which type a single blacklisted key and haven't timed out.
If you set it to 1 then it will only lock devices which registered
as a keyboard and a serial com port.

After the keyboard has been locked you have to replug it
to unlock it.

`logpath /`

Sets the path where every log file is saved in.

If the process is passed the -d flag (daemonize) then it will also write
its log messages to a file which is called out.log in the logpath directory.

Otherwise it will just use the directory for the keylog.

__Note:__ You have to set a full path because the daemon has
to be started as root. Currently the parser does not expand the string
using environment variables.

`usexkeymaps 1`

Disable this to use the kernel keytables which are set with the loadkeys program.
At the moment this is an **experimental** feature.
With it disabled you can even log attacks while the x-server is not running.

## Uninstall:
```
cd bin
sudo xargs rm < install_manifest.txt
sudo rm -rf /etc/duckydd
```
