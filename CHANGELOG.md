# Changelog
Here all of the major changes are documented.

This project adheres to [Semantic Versioning](http://semver.org/)
and uses [Keep a Changelog](http://keepachangelog.com/) as a basis.

## [Unreleased]
#### Fixed
- Logging keystrokes to a file
- Config parser which only read the first line


## [0.3.2] - 2021-01-13
#### Added
- Issue template for bug reports
- Fix for "No protocol specified"

#### Changed
- File permissions of installed files

## [0.3.1] - 2021-01-06
#### Changed
- Replaced MAX_SIZE_PATH with PATH_MAX
- Changed readfile to use a managedBuffer

#### Fixed
- Subtraction of timespec struct


## [0.3.0] - 2021-01-04
#### Added
- New detection method which uses the average time difference between keystrokes
- Help flag
- Keylogging via kernel keytable (experimental)
- System console fallbacks as a source for keymaps
- Dynamic epoll_wait timeout
- Length check of function name and format string to _logger

#### Changed
- Moved safestringlib to lib folder

##### Config changes
- Added minavrg option (For more info have a look at README.md)
- Removed maxtime option
- Removed blacklist option
- Removed maxtime option
- Removed keylogging option

#### Removed
- Timeout of usb devices

#### Fixed
- Memory leak when reloading the config


## [0.2.1] - 2020-06-29
#### Changed
- Changed systemd install path

#### Fixed
- Added fix for "No protocol specified"
- Fixed keyspam bug


## [0.2.0] - 2020-06-15
#### Added
- Keylogging using the xkbcommon library

##### Config changes
- Added keylogging
- Added logpath
- Changed blacklist (look at README.md for more info)
- Changed format of the config file

#### Changed
- Refactored the hole project
- TTY detection now uses major and minor numbers for identification

#### Fixed
- Fixed some bugs which where introduced by this release

#### Security
- Added compile flag "-fstack-protector-strong"
- Added input sanitation to the configuration parser
- Rewrote configuration parser
