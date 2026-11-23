# Stellar WiM Settings

Qt 6 Widgets and Qt 6 Network development files and CMake are required.
This application builds independently of Stellar Gravity:

```sh
cmake -S gui -B build-gui -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-gui
./build-gui/stellar-wim-settings
```

Build a separate Debian package with `cpack --config build-gui/CPackConfig.cmake`.
Its executable and desktop entry have separate paths from those in Gravity's
package, so neither package overwrites the other. No installation is required
to run the build. Set `CMAKE_INSTALL_PREFIX` at configure time if installing
to a different prefix, so the desktop entry points to the right executable.

The application communicates with Stellar WiM over its IPC socket and never
edits the compositor configuration file directly. It uses the desktop's Qt
theme outside Gravity and a blue palette in Gravity sessions.
