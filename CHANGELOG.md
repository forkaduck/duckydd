# Changelog
Here all of the major changes are documented.

This project adheres to [Semantic Versioning](http://semver.org/)
and uses [Keep a Changelog](http://keepachangelog.com/) as a basis.

## [Unreleased]

## [0.2.2] - 2020-06-29
#### Changed
- changed systemd install path

#### Fixed
- Added fix for "No protocol specified"
- Fixed keyspam bug

## [0.2.0] - 2020-06-15
#### Added
- Keylogging using the xkbcommon library

##### Config changes
- added keylogging
- added logpath
- changed blacklist (look at README.md for more info)
- changed format of the config file

#### Changed
- Refactored the hole project
- tty detection now uses major and minor numbers for identification

#### Fixed
- Fixed some bugs which where introduced by this release

#### Security
- added compile flag "-fstack-protector-strong"
- added input sanitation to the configuration parser
- rewrote configuration parser
