# Changelog
Here all of the major changes are documented.

This project adheres to [Semantic Versioning](http://semver.org/)
and uses [Keep a Changelog](http://keepachangelog.com/) as a basis.

## [Unreleased]
#### Added
- Help flag
- Keylogging via kernel keytable (experimental)
- System console fallbacks as a source for keymaps
- Dynamic epoll_wait timeout

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
