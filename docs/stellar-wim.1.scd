stellar-wim(1)

# NAME

Stellar WiM - a Wayland stacking compositor

# SYNOPSIS

*stellar-wim* [options...]

# DESCRIPTION

Stellar WiM is a lightweight wlroots-based stacking compositor for Wayland,
forked from Labwc 0.20.3. It preserves Labwc's behavior and configuration
compatibility while adding optional features.

It is light-weight and independent with a focus on simply stacking windows
well and rendering some window decorations. Where practicable it uses clients
for wall-paper, panels, screenshots and so on.

# SIGNALS

The compositor will exit or reload its configuration upon receiving SIGTERM
and SIGHUP respectively. For example:

```
kill -s <signal> $STELLAR_WIM_PID
```

Each running instance of Stellar WiM sets `STELLAR_WIM_PID` to its PID and,
for compatibility with inherited scripts, also sets `LABWC_PID`. Either
variable can be used with `--exit` and `--reconfigure`.

# OPTIONS

*-c, --config* <config-file>
	Specify a config file with path

*-C, --config-dir* <config-directory>
	Specify a config directory

*-d, --debug*
	Enable full logging, including debug information. See *ENVIRONMENT
	VARIABLES* section below for further options.

*-e, --exit*
	Exit the compositor by sending SIGTERM to `$STELLAR_WIM_PID`

*-h, --help*
	Show help message and quit

*-m, --merge-config*
	Merge user config/theme files in all XDG Base Directories

*-r, --reconfigure*
	Reload the compositor configuration by sending SIGHUP to `$STELLAR_WIM_PID`

*-s, --startup* <command>
	Run command on startup

*-S, --session* <command>
	Run command on startup and terminate compositor on exit. This is useful
	for session management as it allows the session client to terminate
	Stellar WiM by exiting itself. This is a Wayland specific use-case because
	under X, xinit starts the server and keeps it alive for as long as the
	session client. Thus either the session client starts the Window
	Manager, or the Window Manager can be launched independently first. On
	Wayland, the Compositor is both Display Server and Window Manager, so
	the described session management mechanisms do not work because the
	Compositor needs to be running before the session can function. As some
	session clients support both X11 and Wayland, this command line option
	avoids re-writes and fragmentation.

*-t, --title* <fmtstr>
	Set the window title for Stellar WiM to use when it is running in a window
	(i.e. nested in a compositor). <fmtstr> is a format string to be used as
	the window title, replacing `%o` with the name of the output
	region. This is useful when simulating multiple screens, such as with
	running Stellar WiM with the environment variable `WLR_WL_OUTPUTS=2`. In this
	case, `%o` will be unique per simulated screen.

*-v, --version*
	Show the version number and quit

*-V, --verbose*
	Enable more verbose logging

# SESSION MANAGEMENT

To enable the use of graphical clients launched via D-Bus or systemd service
activation, Stellar WiM can update both activation environments on launch. Provided
that Stellar WiM is aware of an active D-Bus user session (*i.e.*, the environment
variable `DBUS_SESSION_BUS_ADDRESS` is defined), the compositor will invoke the
commands

```
dbus-update-activation-environment
systemctl --user import-environment
```

(when available) to notify D-Bus and systemd with the values of the following
environment variables:

```
WAYLAND_DISPLAY
DISPLAY
XDG_CURRENT_DESKTOP
XDG_SESSION_TYPE
XCURSOR_SIZE
XCURSOR_THEME
LABWC_PID
```

This behavior is enabled by default whenever Stellar WiM uses the "DRM" wlroots
backend (which implies that Stellar WiM is the primary compositor on the console).
When other backends are employed (for example, when Stellar WiM runs nested in another
Wayland compositor or an X11 server), updates to the activation environment are
disabled by default. Updates to the activation environment can be forced by
setting the environment variable `LABWC_UPDATE_ACTIVATION_ENV` to one of the
truthy values `1`, `true`, `yes` or `on`; or suppressed by setting the variable
to one of the falsy values `0`, `false`, `no` or `off`.

Whenever Stellar WiM updates the activation environment on launch, it will also
attempt to clear the activation environment on exit. For D-Bus, which does not
provide a means for properly un-setting variables in the activation environment,
this is accomplished by setting the session variables to empty strings. For
systemd, the command `systemctl --user unset-environment` will be invoked to
actually remove the variables from the activation environment.

A systemd user unit named `Stellar WiM-session.target` is also shipped alongside
the compositor for users who want to integrate Stellar WiM with systemd. It binds
to the standard `graphical-session.target`, so systemd user services can
start and stop in sync with the Stellar WiM session when they declare a WantedBy
or PartOf relationship to that target. Stellar WiM does not activate the target
itself; users opt in by adding lines like the following to their
*autostart* and *shutdown* files:

```
systemctl --user --no-block start Stellar WiM-session.target
systemctl --user stop graphical-session.target
```

The example *autostart* and *shutdown* files shipped with Stellar WiM include
these commented out. To have a user service automatically started with
the session, enable it so the corresponding symlink under the
graphical-session.target.wants directory exists, for example by running
"systemctl --user enable dms.service".

# ENVIRONMENT VARIABLES

Set the environment variables listed below to enable specific debug options.
This can be done in either the *environment* file or on the command line, for
example: *LABWC_DEBUG_FOO=1 stellar-wim*.

*STELLAR_WIM_IPC_SOCKET*
	Override the complete path of the Stellar IPC control socket. By default
	the socket file in *$XDG_RUNTIME_DIR* is named
	stellar-wim-$WAYLAND_DISPLAY.sock. See stellar-wim-ipc(7).

*LABWC_DEBUG_LIBSFDO*
	Enable debug and info logging for libsfdo, for example for parsing of
	.desktop files and searching for icons. Note that libsfdo error logging
	is always enabled regardless of this environment variable but will only
	be shown with the *-V|--version* option.

*LABWC_DEBUG_DIR_CONFIG_AND_THEME*
	Increase logging of paths for config files (for example rc.xml,
	autostart, environment and menu.xml) as well as titlebar buttons.

*LABWC_DEBUG_CONFIG_NODENAMES*
	Enable logging of all nodenames (for example *policy.placement: Cascade*
	for *<placement><policy>Cascade</policy></placement>*) for config and
	menu files respectively.

*LABWC_DEBUG_KEY_STATE*
	Enable logging of press and release events for bound keys (generally
	key-combinations like *Ctrl-Alt-t*).

# SEE ALSO

stellar-wimctl(1), stellar-wim-ipc(7), stellar-wim-actions(5), stellar-wim-config(5), stellar-wim-menu(5), stellar-wim-theme(5), stellar-wim-session(7)
