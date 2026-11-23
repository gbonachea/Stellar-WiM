STELLAR-WIM-SESSION(7)
======================

# NAME

stellar-wim-session - session integration and coexistence with Labwc

# DESCRIPTION

Stellar WiM installs its own display-manager session, portal configuration and
optional systemd user target. None of these files replaces a Labwc file, so
both compositors can be installed at the same time and selected independently
from the login screen.

# FILES

*datadir/wayland-sessions/stellar-wim.desktop*
	Starts the stellar-wim executable as a Wayland session. The *DesktopNames*
	entry is stellar-wim;wlroots, which lets toolkits identify the compositor
	family. Display managers that update the activation environment and call
	the session target will also pick up the compositor's exported variables.

*datadir/xdg-desktop-portal/stellar-wim-portals.conf*
	Prefers the wlroots portal implementation. It selects no implementation
	for *Inhibit* because the compositor implements the inhibitor protocol
	itself.

*systemd user unit directory/stellar-wim-session.target*
	An optional synchronization point for user services that declare
	WantedBy=graphical-session.target. It is installed only when the systemd
	pkg-config module is available at build time. In distributions where the
	target is absent, graphical services still start normally; the target is a
	convenience, not a requirement.

*datadir/doc/stellar-wim/*
	Example rc.xml, menu.xml, environment, autostart, shutdown, xinitrc and
	theming files. They are documentation only and are never copied into a
	user's configuration directory automatically.

# CONFIGURATION

The configuration directory is $XDG_CONFIG_HOME/stellar-wim, normally
~/.config/stellar-wim. An existing ~/.config/labwc tree is read as a fallback
only when no Stellar configuration exists, so an existing Labwc setup keeps
working after installation. See stellar-wim-config(5).

The IPC socket name is derived from $WAYLAND_DISPLAY and lives in
$XDG_RUNTIME_DIR. It can be overridden with $STELLAR_WIM_IPC_SOCKET. See
stellar-wim-ipc(7).

# PACKAGING

Everything is installed by a single meson install step. Debian/Ubuntu source
package metadata is provided in the debian directory; it does not declare a
conflict with the labwc package. See packaging/README.md and
packaging/build-deb.sh in the source tree.

# SEE ALSO

stellar-wim(1), stellar-wim-config(5), stellar-wim-ipc(7), stellar-wimctl(1)
