stellar-wim-ipc(7)

# NAME

Stellar WiM control protocol - local Unix socket used by stellar-wimctl

# DESCRIPTION

Stellar WiM exposes a small, local control channel for inspecting windows,
desktops and outputs and for performing a limited set of window actions. The
channel is disabled completely with `<stellar-wim><ipc enabled="false"/></stellar-wim>`
in *rc.xml*. It is enabled by default.

The channel is intentionally not a generic remote-control interface: there is
no shell execution, no arbitrary file access and no protocol for clipboard or
image transfer.

# ADDRESS

The socket is created in *$XDG_RUNTIME_DIR* and named
stellar-wim-$WAYLAND_DISPLAY.sock (`default` is used when
*WAYLAND_DISPLAY* is unset). The environment variable
*STELLAR_WIM_IPC_SOCKET* replaces the complete path. The socket is removed on
clean shutdown.

The socket file is created with mode 0600 and the compositor additionally
checks *SO_PEERCRED*: clients with a different effective uid are rejected.
Accept is limited to 16 simultaneous clients.

Before replacing a leftover socket file, the compositor makes a conservative
non-blocking connect probe. Only a definite *ECONNREFUSED* or *ENOENT* proves
the socket is stale; inconclusive results (including *EINPROGRESS* and a full
listen backlog) are treated as "in use", so a live compositor's socket is
never unlinked.

# FRAMING

Every frame starts with an eight-byte header, encoded as little-endian
unsigned integers:

```
[u32 magic = 0x5357494d][u32 payload length][payload]
```

The magic value is the ASCII string `SWIM`. The payload is UTF-8 text without a
trailing NUL byte. Requests are at most 4096 bytes; responses at most 65536
bytes; notifications at most 4096 bytes. A client sends one request per
connection.

Requests are whitespace-separated tokens:

```
VERB [arg ...]
```

Verb matching is case-insensitive. Tabs, newlines, NUL bytes and requests
with more than *STELLAR_IPC_MAX_TOKENS* (40) tokens are rejected. *TABS GROUP*
accepts 2..32 window ids per request.

# RESPONSES

Responses and notifications use the same framing:

```
OK <json>
ERR <code> <message>
EVENT <json>
```

Error codes are:

1
	Malformed request.

2
	Unknown command.

3
	Requested object not found.

4
	Invalid argument.

5
	Internal error or response too large.

6
	Feature disabled.

JSON output is a compact array or object. Strings are escaped, and all output
is bounded by the response limit.

# REQUESTS

*PING*
	Return `{"pong":true}`.

*VERSION*
	Return version and feature information, including the top-level *profile*
	string. The *features* object contains
	*ipc*, *tabs*, *tiling*, *animations* and *notifications*. The tabs,
	tiling and animations flags reflect the current runtime state.

*CAPABILITIES*
	Return numeric capability flags. *tabs* and *tiling* are 1 while the
	respective feature is enabled and 0 otherwise; *profiles* is always 1;
	*effects* is 1 while at least one visual effect is enabled; the other
	fields describe the always-available IPC groups.

*CONFIG GET* [*<key>*]
	Without a key, return a JSON object containing the legacy shorthand fields
	(*animations*, *opacity*, *shadows*, *tabs*, *tiling*, *corner_radius*,
	*profile*, *tiling_mode*) plus every key exposed by the Configuration API.
	With a key, return that key's value, default, type, application mode and
	persistence flag. This command reads only live compositor state and never
	modifies or persists configuration.

*CONFIG LIST*
	Return an array of key descriptors. Each entry contains *key*, *section*,
	*type*, *default*, *applies* (`live` or `reload`) and, for numeric keys,
	*minimum* and *maximum*.

*CONFIG STATUS*
	Return the active *rc.xml* path, whether it is writable and persistence
	state. *dirty* reports whether work was pending when the request arrived.
	The configuration worker checks writability off the compositor thread.
	At most 32 configuration jobs may be outstanding across all clients;
	when full, the queue rejects additional jobs until space is available.

*CONFIG SET* <key> <value>
	Validate and normalize the request without blocking the compositor, then
	queue it for a single serialized writer that parses and atomically saves
	*rc.xml*. The reply is sent from the main event loop after the write
	finishes and reports *applied*, *persisted*, *reload* and *ok*
	separately. A change that contradicts a named profile marks the session
	*custom*, so a later reload does not silently restore the preset. Numeric
	values always use `.` as the decimal separator, independently of the
	compositor locale.

*CONFIG RESET* <key>
	Restore one key to its default and remove the corresponding XML entry when
	the default is represented by absence.

*CONFIG RESET SECTION* <section>
	Restore every key in a section. *all* restores the complete key table.

*CONFIG RELOAD*
	Queue a barrier behind all configuration jobs already submitted, then
	re-read *rc.xml* and the theme from the event loop when the barrier
	completes. This ensures the reload observes every queued write. Structural
	changes use this automatically.

*CONFIG KEYBINDINGS LIST*
	Return keybindings written in *rc.xml* as a JSON array like `[{"key":...,"actions":[...]}]`.

*CONFIG KEYBINDINGS SET* <key> <action>
	Add or replace a keybinding. Only compositor actions that take no
	arguments and do not execute programs are accepted: *None*, *Close*,
	*NextWindow*, *PreviousWindow*, *NextWindowImmediate*,
	*PreviousWindowImmediate*, *ToggleMaximize*, *Maximize*, *UnMaximize*,
	*ToggleFullscreen*, *ToggleDecorations*, *ToggleAlwaysOnTop*,
	*ToggleAlwaysOnBottom*, *ToggleOmnipresent*, *Iconify*, *Raise*, *Lower*,
	*ToggleShowDesktop*, *ToggleKeybinds*, *ToggleMagnify*, *ZoomIn*,
	*ZoomOut*, *StellarTabGroup*, *StellarTabNext*, *StellarTabPrevious*,
	*StellarTabDetach*, *StellarTilingToggle*, *StellarTilingReapply*,
	*StellarProfileMinimal* and *StellarProfileEnhanced*. *Execute*, *Exit*,
	*Kill*, *ShowMenu* and arbitrary command lines are rejected.

*CONFIG KEYBINDINGS REMOVE* <key>, *CONFIG KEYBINDINGS RESET*
	Remove one keybinding or all user keybindings.

*CONFIG RULES LIST*
	Return window rules as a JSON array like `[{"attributes":{...},"actions":[...]}]`.

*CONFIG RULES SET* <identifier> <property> <value>
	Add or update one property of the rule matching *identifier*. Accepted
	properties are *serverDecoration*, *skipTaskbar*, *skipWindowSwitcher*,
	*ignoreFocusRequest*, *ignoreConfigureRequest*, *allowAlwaysOnTop* and
	*fixedPosition*, accepting `yes`, `no` or `default`; *iconPriority*,
	accepting `client` or `server`; *stellarTiling*, with accepted values
	including `floating` and `managed`; and *stellarTilingMode*, accepting the
	usual tiling modes.

*CONFIG RULES REMOVE* <identifier> [*property*], *CONFIG RULES RESET*
	Remove one property, a whole rule, or every window rule.

All mutating configuration commands write through a temporary file and
*rename(2)*, preserve the file mode and emit a *config* event from the main
thread after the worker reports completion. Structural commands also schedule
a deferred reconfigure behind the queued writes.

*WINDOWS LIST*, *WINDOWS ACTIVE*
	Return window objects or an array of them.

*WINDOWS ACTIVATE* <id>, *WINDOWS MINIMIZE* <id>, *WINDOWS MAXIMIZE* <id>, *WINDOWS CLOSE* <id>
	Perform the corresponding action. The id is the numeric creation id
	reported by *WINDOWS LIST*. *CLOSE* fades the window out first when
	animations are enabled.

*WINDOWS MOVE_DESKTOP* <id> <n>
	Move a window to desktop *n*.

*DESKTOPS LIST*, *DESKTOPS SWITCH* <n>
	Inspect or switch desktops.

*OUTPUTS LIST*
	Return output geometry and state.

*PROFILE GET*
	Return `{"profile":"minimal|enhanced|custom"}`.

*PROFILE SET* <minimal|enhanced>
	Switch profiles at runtime and return `{"profile":...,"ok":true}`.
	*custom* cannot be selected here because it needs individual flags from
	*rc.xml*. The named profiles also toggle effects: *minimal* turns off
	animations, opacity and shadows; *enhanced* restores them (including an
	opacity level of 0.90 if it was zero). Only safe transitions are
	performed; renderer, adaptive sync, XWayland and protocol setup are not
	touched. Runtime switches are not persisted: write the desired profile in
	*rc.xml* to keep it across restarts. Changing the tiling mode or toggling
	tiling marks the session *custom*.

*TABS STATUS*
	Return `{"enabled":...,"groups":...}`. This command works even when tabs
	are disabled, in which case *enabled* is false.

*TABS LIST*
	Return tab groups as `[{"id":...,"active":...,"members":[...]}]`, using
	the numeric window creation ids. Requires tabs to be enabled.

*TABS GROUP* <id> [id ...]
	Create a tab group from 2..32 mapped windows on the same desktop, or add
	the listed windows to an existing group when any of them already belongs
	to one. All listed windows must be in the same group (or ungrouped) and
	the group's geometry is shared by all its members. Requires tabs to be
	enabled.

*TABS NEXT*, *TABS PREVIOUS*
	Switch the active tab of the focused window. Requires tabs to be enabled.

*TABS DETACH* <id>
	Remove a window from its tab group. Requires tabs to be enabled.

*TILING STATUS*
	Return `{"enabled":...,"managed":...,"mode":...,"master_ratio":...,"columns":...,"outputs":[...]}`.
	*mode* is one of *master-stack*, *horizontal*, *vertical*, *grid* or
	*hybrid*; *master_ratio* defaults to 0.60 and *columns* to 2. Each entry
	in *outputs* contains *name*, *managed* and the effective *mode* for that
	output, including window-rule overrides. Works even when tiling is
	disabled.

*TILING SET* <mode>
	Set the tiling mode to *master-stack*, *horizontal*, *vertical*, *grid* or
	*hybrid* (*master* is accepted as an alias) and return a JSON object
	with *mode* and *ok* fields. The new mode applies to the whole output and
	marks the session *custom*. An unknown mode returns error 4.

*TILING TOGGLE*
	Enable or disable tiling and return `{"enabled":...,"ok":true}`. When
	disabled, each managed window gets its pre-tiling geometry back.

*TILING REAPPLY*
	Recompute the layout. Requires tiling to be enabled.

*EFFECTS STATUS*
	Report visual-effect state. *animations* and *opacity* mirror the Stellar
	configuration flags; *shadows* mirrors Labwc's inherited
	*theme/dropShadows* option; *blur* is always false because wlroots 0.20
	has no scene blur API. *enabled* is true when any of those three is on.
	*active_animations*, *timer_armed*, *frame_commits* and
	*animations_started* are live counters: with effects idle no timer is
	armed and no frames are committed. *animations_started* includes
	map fade-ins, fade-outs on minimize or tab hide and compositor-requested
	closes; a view being hidden stays scene-enabled until its fade-out
	completes, so it is still visible to the scene during that interval. A
	close fade sends the protocol close only after the animation ends, so it
	never dereferences a surface the client already destroyed.
	*opacity_level* is the configured
	content opacity in the range 0.00–1.00. Animations, shadows and opacity
	are on by default in Stellar WiM and are toggled by the *minimal* and
	*enhanced* profiles; *blur* remains unavailable.

*EVENTS SUBSCRIBE*
	Keep the connection open and emit *EVENT* frames for window, desktop,
	tab, tiling and profile state changes. The first response is `OK {"subscribed":true}`.
	Tab events use `{"type":"tabs","event":...,"group":...,"view":...}`;
	tiling toggle events use `{"type":"tiling","event":"toggle","managed":...}`;
	profile switches use `{"type":"profile","event":"switch","profile":...}`.
	Configuration changes use a JSON object of the form `{"type":"config","event":...,"key":...,"value":...,"persisted":...}`.
	Slow
	subscribers are dropped after 32 queued events instead of blocking the
	compositor.

# CONFIGURATION

Add the following element inside the existing `<labwc_config>` root in
*rc.xml*:

```
<labwc_config>
  <stellar-wim>
    <ipc enabled="yes"/>
    <profile>custom</profile>
    <tabs enabled="no"/>
    <tiling enabled="no">
      <mode>master-stack</mode>
      <master-ratio>0.60</master-ratio>
      <columns>2</columns>
      <min-width>0</min-width>
      <min-height>0</min-height>
      <max-width>0</max-width>
      <max-height>0</max-height>
    </tiling>
    <effects animations="yes" opacity="yes" opacity-level="0.90"/>
  </stellar-wim>
  <windowRules>
    <windowRule identifier="foot">
      <stellarTilingMode>horizontal</stellarTilingMode>
    </windowRule>
  </windowRules>
</labwc_config>
```

The values accept the usual boolean spellings (`yes`/`no`, `true`/`false`
and `1`/`0`). This example shows the defaults: effects are on, tabs and
tiling off; the *minimal* and *enhanced* profiles also switch effects.
Changing *ipc* with SIGHUP starts or stops the socket at runtime. Changes to
*tabs*, *tiling*, the tiling options or *effects* are reflected immediately
after the reload in `VERSION` and `CAPABILITIES`. See stellar-wim-config(5)
for the accepted tiling modes, ratio/column limits and window-rule semantics.

# SECURITY CONSIDERATIONS

The socket is local to the user's runtime directory and uid-checked. Any
process running as the same user can still control the compositor; this is
equivalent to having access to the user's session and is intentional. Do not
place the socket in a world-writable directory. The path override exists for
testing and nested sessions.

# SEE ALSO

stellar-wim(1), stellar-wimctl(1), stellar-wim-config(5)
