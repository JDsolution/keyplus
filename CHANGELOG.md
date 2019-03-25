0.3.4 (unreleased)
==================

* `mouse` mouse buttons are now remappable by layouts
* `mouse` better support for extra mouse buttons
* `mouse` added `keyplus-cli hidpp list-features` for printing mouse capabilites
* `mouse` added HID++ 2.0 allowing software to control/read from a connected mouse

* `layout` make id field optional for non-split boards
* `layout` added `has_mouse_layers` for enabling mouse layers

* `firmware` matrix scanning for EFM8

* `keyplus-flasher` when a device has a critical error, perform a full reset
* `keyplus-flasher` throw error when trying to use matrix scanning when a
    device does not support it.

0.3.3
==================

* `keyplus-flasher` switch from pyside to PyQt5 for GUI. This makes it possible
    to run the Windows version with Python3 instead of Python2.7. It should
    also make it easier to run on MacOS.
* `keyplus-flasher` fix some crashes on Windows

0.3.2
==================

* `firmware` fixed media keys
* `firmware` clean up source code tree. Standalone ports (e.g. `xmega`,
  `nRF24LU1`, `atmega32u4`) are moved to the `ports` directory. And source
  code inside these directories is cleaned up by moving all *.c, *.h files
  to their own `src` directory.
  now moved to the `ports` directory.
* `firmware` the device name string is now stored as UTF16-LE
* `firmware` some linker script fixes for some large flash xmega chips
* `firmware` added alternative shared HID definition for microcontrollers that
  only have a small number of endpoints. Will be useful for ATmega32u2 and EFM8,
  since they only have ~4 endpoints.
* `firmware` add nRF24LU1+ and EFM8 to travis build targets
* `firmware` basis for efm8 support

* `keyplus-flasher` efm8 factory bootloader support
* `keyplus-flasher` add unicode support to yaml files


0.3.1
==================

* `layout` add support for `keycodes` section in the layout file for defining
  custom special keycodes
* `layout/keycodes` add support for `hold` key in `keycodes` section
* `layout/keycodes` add more alternative names to some keycodes: `trns`, `space`, 's_l0`,
  `s_lctl`, `int1`. Changed `lang_1` to `lang1` etc.
* `layout/keycodes` add alternative activation method for hold keys. Instead of
  using a delay to activate the hold key, allow the hold key to be activated
  when other keys are pressed.

* `firmware` basic atmega32u4 support
* `firmware` fixed issue that caused updates to the encryption key to
  require a reset
* `firmware` made the maximum number of supported rows configurable at compile
  time, and increased the default value from 10 to 18.
* `firmware` added place holder mouse keys implementation (non-configurable)

* `keyplus-flasher` more error messages
* `keyplus-flasher` fixed labels not updating properly
* `keyplus-flasher` fixed issue requiring user to manual press program button
  when updating firmware

0.3.0
==================

Changes to layout and config files:
-----------------------------------

* `layout` format is incompatible with previous versions
* `layout` files now have options to enable/disable I²C, nRF24 and mouse
  support.  By default these options are now turned off. So if you used any
  of these features they will need to be re-enabled in the layout file. See
  the example layout files for details.
* `layout` the keycode `menu`, HID keycode `KC_MENU`, has now been renamed to
  `hid_menu`. The keycode `menu` in layout files now maps to the HID keycode
  `KC_APPLICATION` as this is what most people expect.

Internal changes:
-----------------

* `python-api` new python API added. It is available in the `keyplus` package
  on PyPI.
* `python-api` rework all the code for parsing layout files.
* `python-api` add commands to read the layout and settings form the device.
* `python-api` add commands to deserialize layouts read from devices

* `keyplus-cli` add command to erase all layouts and settings
* `keyplus-cli` add commands to dump the raw hex of the layout and settings section
* `keyplus-cli` is now able to update RF, layout, and ID separately.

* `keyplus-flasher` add support for kp_boot_32u4 bootloader

* `firmware` add new `chip_id` concept to firmware settings allowing the
  `python-api` to discover what microcontroller the keyboard is using.
* `firmware` rework flash writing commands so that is possible to update only a
  portion of the layout.
* `firmware` layout updates can now be performed without the USB device
  needing to be reset
* `firmware-xmega` now utilizes the IRQ pin in receive mode
* `firmware-xmega` add arbitrary pin mapping to matrix scanning algorithm. It
  should now be possible to use any pins as row/columns.
* `firmware-xmega` add new matrix scanning modes: `col_row`, `pin_gnd`,
  `pin_vcc`. The `col_row` mode allows the direction of diodes to be flipped.
  The `pin_gnd` and `pin_vcc` modes allow for direct wiring of switches to GPIO
  pin and one of either GND (`pin_gnd`) or VCC (`pin_vcc`).

Bug fixes:
----------

* `firmware` fixes bug where it was possible to read the encryption key from the
  device.
* `firmware` fixes some bugs that would allow the nRF24 code to accept invalid
  packets.
* `firmware` I²C split keyboards now disable their nRF24 module if they are
  not connected to a USB port. This was causing dropped packets before.

*******************************************************************************

0.2.2 (2018-01-27)
==================

* Add error detection and recovery system
* Add ability for controllers to enter recover mode when it detect
  corrupt or invalid settings. Recover mode disables all features except those
  necessary to upload a new layout.

Start of change log.
====================
