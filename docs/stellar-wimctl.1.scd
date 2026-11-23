stellar-wimctl(1)

# NAME

stellar-wimctl - control a running Stellar WiM instance

# SYNOPSIS

*stellar-wimctl* <command> [arguments...]

# DESCRIPTION

*stellar-wimctl* is a small client for the Stellar IPC control socket. It does
not start a shell, evaluate arbitrary commands or read user configuration; it
sends one bounded request and prints the JSON response.

See stellar-wim-ipc(7) for the wire format and security model.

# COMMANDS

*version*
	Return compositor version, upstream version, wlroots version and active
	feature flags.

*capabilities*
	Return the numeric capabilities currently implemented by the compositor.

*config get* [*<key>*]
	Read the configuration summary or one validated key with its metadata.

*config list*
	List configurable keys, types, defaults and bounds.

*config status*
	Show the active *rc.xml* path and whether it is writable.

*config set* <key> <value>
	Validate, save and apply one configuration value.

*config reset* <key>
	Restore one key to its default.

*config reset section* <name>
	Restore a section, or *all* sections, to their defaults.

*config reload*
	Re-read *rc.xml* and the theme without restarting the compositor.

*config keybindings list*, *config keybindings set* <key> <action>, *config keybindings remove* <key>, *config keybindings reset*
	Inspect or change safe, argument-free keybindings. Program execution is
	not exposed through the API.

*config rules list*, *config rules set* <id> <property> <value>, *config rules remove* <id> [*property*], *config rules reset*
	Inspect or change validated window-rule properties.

*ping*
	Check that the IPC socket is reachable.

*windows list*
	List all known windows with id, title, app id, workspace, output,
	geometry and state.

*windows active*
	Return the active window, or *null* when no window is focused.

*windows activate* <id>
	Focus and raise the window with the given numeric id.

*windows minimize* <id>
	Minimize the window with the given numeric id.

*windows maximize* <id>
	Maximize the window with the given numeric id.

*windows close* <id>
	Close the window with an animation first when animations are enabled.

*windows move-desktop* <id> <n>
	Move a window to desktop *n*. Desktop numbers are 1-based.

*desktops list*
	List desktops with index, name and active state.

*desktops switch* <n>
	Switch to desktop *n*. Desktop numbers are 1-based.

*outputs list*
	List connected outputs with layout box, refresh rate and enabled state.

*profile get*
	Report the active profile (*minimal*, *enhanced* or *custom*).

*profile set* <minimal|enhanced>
	Switch profiles at runtime using only safe transitions. The named
	profiles also toggle effects: *minimal* disables animations, opacity and
	shadows, while *enhanced* restores them. Runtime switches are not
	persisted; put the desired profile in *rc.xml* to make it permanent.

*tabs status*
	Report whether tab groups are supported and how many groups exist.

*tabs list*
	List tab groups with their id, active window and member ids.

*tabs group* <id> [id ...]
	Group 2..32 mapped windows into a tab group. If a listed window already
	belongs to a group, the other windows extend that group.

*tabs next*, *tabs previous*
	Activate the next or previous tab of the focused window.

*tabs detach* <id>
	Remove a window from its tab group.

*tiling status*
	Report whether tiling is enabled, how many windows are managed, the
	global *mode*, *master_ratio*, *columns* and the effective mode plus
	managed-window count for every output.

*tiling set* <mode>
	Set the tiling mode to *master-stack*, *horizontal*, *vertical*, *grid*
	or *hybrid* and recompute the layout. The session becomes *custom*.

*tiling toggle*
	Enable or disable tiling. Disabling restores pre-tiling geometry.

*tiling reapply*
	Recompute the tiling layout.

*effects status*
	Report animation, opacity, shadow and blur flags plus live animation
	counters. *animations_started* includes fade-outs on minimize, tab hide
	and compositor-requested close. See stellar-wim-ipc(7) for the exact
	fields.

*events subscribe*
	Print state-change notifications until the connection is closed.

# ENVIRONMENT

*XDG_RUNTIME_DIR*
	Runtime directory used to locate the default control socket. Required
	unless *STELLAR_WIM_IPC_SOCKET* is set.

*WAYLAND_DISPLAY*
	Used to derive the default socket name
	stellar-wim-$WAYLAND_DISPLAY.sock.

*STELLAR_WIM_IPC_SOCKET*
	Override the complete socket path. Useful for tests and nested sessions.

# EXIT STATUS

0
	The request succeeded.

1
	The compositor returned an error or the socket could not be used.

2
	The command line was invalid.

# SEE ALSO

stellar-wim(1), stellar-wim-ipc(7)
