// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "ipc/server.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/version.h>
#include <wlr/util/log.h>
#include "config/config-api.h"
#include "config/rcxml.h"
#include "ipc/protocol.h"
#include "effects.h"
#include "labwc.h"
#include "output.h"
#include "profile.h"
#include "tabs.h"
#include "tiling.h"
#include "view.h"
#include "workspaces.h"

#define MAX_CLIENTS 16
#define MAX_EVENTS_PER_CLIENT 32
#define MAX_DISPLAY_LEN 64

struct ipc_event {
	struct wl_list link;
	size_t len;
	char data[STELLAR_IPC_MAX_EVENT + STELLAR_IPC_HEADER_SIZE];
};

struct ipc_client {
	int fd;
	struct wl_event_source *source;
	struct wl_list link;
	uint64_t id;
	uid_t uid;

	unsigned char in[STELLAR_IPC_HEADER_SIZE + STELLAR_IPC_MAX_REQUEST];
	size_t in_len;
	size_t need;

	unsigned char out[STELLAR_IPC_HEADER_SIZE + STELLAR_IPC_MAX_RESPONSE];
	size_t out_len;
	size_t out_off;
	bool close_after_flush;
	bool subscribed;
	/* One configuration job at a time keeps this client's requests ordered. */
	bool write_pending;

	struct wl_list events;
	size_t nr_events;
};

static struct wl_list clients = {0};
static struct wl_event_source *listen_source;
static int listen_fd = -1;
static char socket_path[PATH_MAX];
static bool running;
static bool shutdown_pending;
static bool shutting_down;
static size_t nr_clients;
static uint64_t next_client_id = 1;

static void client_destroy(struct ipc_client *client);
static struct ipc_client *find_client_by_id(uint64_t id);
static void ipc_stop_listening(void);
static void ipc_shutdown_now(void);

static uint32_t
frame_magic(const unsigned char *buf)
{
	return (uint32_t)buf[0]
		| ((uint32_t)buf[1] << 8)
		| ((uint32_t)buf[2] << 16)
		| ((uint32_t)buf[3] << 24);
}

static uint32_t
frame_length(const unsigned char *buf)
{
	return (uint32_t)buf[4]
		| ((uint32_t)buf[5] << 8)
		| ((uint32_t)buf[6] << 16)
		| ((uint32_t)buf[7] << 24);
}

/*
 * A leftover socket file is safe to remove only when nobody is listening on
 * it. Never unlink a live compositor's control socket just because a second
 * instance derived the same path.
 */
static bool
socket_path_in_use(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		/* Failure to probe is not proof that the socket is stale. */
		return true;
	}
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	bool in_use = false;
	if (strlen(path) < sizeof(addr.sun_path)) {
		strcpy(addr.sun_path, path);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
			in_use = true;
		} else if (errno != ECONNREFUSED && errno != ENOENT) {
			/* Includes EINPROGRESS and a full listen backlog (EAGAIN).
			 * Never unlink a potentially live socket on an inconclusive probe. */
			in_use = true;
		}
	}
	close(fd);
	return in_use;
}

static int
workspace_index(const struct workspace *workspace)
{
	int index = 0;
	struct workspace *w;
	wl_list_for_each(w, &server.workspaces.all, link) {
		index++;
		if (w == workspace) {
			return index;
		}
	}
	return 0;
}

static struct workspace *
workspace_from_index(const char *text)
{
	char *end = NULL;
	errno = 0;
	long value = strtol(text, &end, 10);
	if (errno || !end || *end || value < 1 || value > INT_MAX) {
		return NULL;
	}
	int index = (int)value;
	struct workspace *w;
	wl_list_for_each(w, &server.workspaces.all, link) {
		if (--index == 0) {
			return w;
		}
	}
	return NULL;
}

static struct view *
view_from_id(const char *text)
{
	char *end = NULL;
	errno = 0;
	unsigned long long id = strtoull(text, &end, 10);
	if (errno || !end || *end || text[0] == '-') {
		return NULL;
	}
	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		if (view->creation_id == id) {
			return view;
		}
	}
	return NULL;
}

static bool
json_view(struct ipc_json *json, struct view *view)
{
	return json->data != NULL
		&& ipc_json_add(json, "{\"id\":%llu,\"type\":\"%s\",",
			(unsigned long long)view->creation_id,
			view->type == LAB_XDG_SHELL_VIEW ? "xdg" : "xwayland")
		&& ipc_json_add(json, "\"title\":")
		&& ipc_json_string(json, view->title ? view->title : "")
		&& ipc_json_add(json, ",\"app_id\":")
		&& ipc_json_string(json, view->app_id ? view->app_id : "")
		&& ipc_json_add(json,
			",\"workspace\":%d,\"output\":",
			workspace_index(view->workspace))
		&& ipc_json_string(json,
			view->output && view->output->wlr_output
				? view->output->wlr_output->name : "")
		&& ipc_json_add(json,
			",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
			"\"mapped\":%s,\"minimized\":%s,\"maximized\":%s,"
			"\"fullscreen\":%s,\"active\":%s}",
			view->current.x, view->current.y,
			view->current.width, view->current.height,
			view->mapped ? "true" : "false",
			view->minimized ? "true" : "false",
			view->maximized != VIEW_AXIS_NONE ? "true" : "false",
			view->fullscreen ? "true" : "false",
			server.active_view == view ? "true" : "false");
}

static void
queue_response(struct ipc_client *client, const char *payload, size_t len,
	bool close_after_flush)
{
	size_t frame_len = 0;
	if (!ipc_protocol_encode(client->out, sizeof(client->out),
			payload, len, &frame_len)) {
		const char *err = "ERR 5 internal error";
		len = strlen(err);
		if (!ipc_protocol_encode(client->out, sizeof(client->out),
				err, len, &frame_len)) {
			client_destroy(client);
			return;
		}
		close_after_flush = true;
	}
	client->out_len = frame_len;
	client->out_off = 0;
	client->close_after_flush = close_after_flush;
	wl_event_source_fd_update(client->source,
		WL_EVENT_READABLE | WL_EVENT_WRITABLE);
}

static void
send_error(struct ipc_client *client, int code, const char *message)
{
	char response[STELLAR_IPC_MAX_RESPONSE];
	if (ipc_response_error(response, sizeof(response), code, message)) {
		queue_response(client, response, strlen(response), true);
	} else {
		client_destroy(client);
	}
}

static void
send_json(struct ipc_client *client, const char *json, bool keep_open)
{
	char response[STELLAR_IPC_MAX_RESPONSE];
	if (ipc_response_ok(response, sizeof(response), json)) {
		queue_response(client, response, strlen(response), !keep_open);
	} else {
		send_error(client, STELLAR_IPC_ERR_INTERNAL, "response too large");
	}
}

static bool
token_is(const char *token, const char *word)
{
	return strcasecmp(token, word) == 0;
}

struct config_reply_token {
	uint64_t client_id;
};

static struct config_reply_token *
new_config_token(struct ipc_client *client)
{
	struct config_reply_token *token = calloc(1, sizeof(*token));
	if (token) {
		token->client_id = client->id;
	}
	return token;
}

static int
config_status_to_error(enum config_async_status status)
{
	switch (status) {
	case CONFIG_ASYNC_INVALID_ARGUMENT:
		return STELLAR_IPC_ERR_INVALID_ARGUMENT;
	case CONFIG_ASYNC_NOT_FOUND:
		return STELLAR_IPC_ERR_NOT_FOUND;
	case CONFIG_ASYNC_INTERNAL:
	default:
		return STELLAR_IPC_ERR_INTERNAL;
	}
}

/*
 * Configuration completions always run on the main thread. A client may have
 * disconnected while the worker was busy; in that case the state change is
 * still applied, but there is nobody left to answer.
 */
static void
config_async_done(const struct config_async_result *result, void *data)
{
	struct config_reply_token *token = data;
	struct ipc_client *client = find_client_by_id(token->client_id);
	free(token);
	if (!client) {
		return;
	}
	client->write_pending = false;
	if (result->status == CONFIG_ASYNC_OK) {
		send_json(client, result->json, false);
	} else {
		send_error(client, config_status_to_error(result->status),
			result->message);
	}
}

/*
 * Returns a token that must be freed by the caller. Setting write_pending
 * makes this client stop reading until the worker callback arrives, which
 * preserves request order and prevents a second reply from overwriting the
 * first one.
 */
static struct config_reply_token *
begin_config_async(struct ipc_client *client)
{
	client->write_pending = true;
	/* Level-triggered READABLE would spin while this client is paused. */
	wl_event_source_fd_update(client->source, 0);
	struct config_reply_token *token = new_config_token(client);
	if (!token) {
		client->write_pending = false;
		wl_event_source_fd_update(client->source, WL_EVENT_READABLE);
	}
	return token;
}

static void
abort_config_async(struct ipc_client *client, struct config_reply_token *token)
{
	client->write_pending = false;
	wl_event_source_fd_update(client->source, WL_EVENT_READABLE);
	free(token);
}

static void
dispatch(struct ipc_client *client, const unsigned char *payload, size_t len)
{
	if (len == 0 || len > STELLAR_IPC_MAX_REQUEST ||
			memchr(payload, '\0', len)) {
		send_error(client, STELLAR_IPC_ERR_BAD_REQUEST, "invalid payload");
		return;
	}

	char request[STELLAR_IPC_MAX_REQUEST + 1];
	memcpy(request, payload, len);
	request[len] = '\0';

	char *tokens[STELLAR_IPC_MAX_TOKENS];
	int nr = ipc_protocol_tokenize(request, tokens,
		STELLAR_IPC_MAX_TOKENS);
	if (nr < 1) {
		send_error(client, STELLAR_IPC_ERR_BAD_REQUEST, "empty request");
		return;
	}

	char json[STELLAR_IPC_MAX_RESPONSE];
	struct ipc_json out;
	ipc_json_init(&out, json, sizeof(json));

	/*
	 * Stellar Configuration API. Values are validated, written atomically
	 * to rc.xml and, where possible, applied without restarting the
	 * compositor. Every change is announced to subscribed clients.
	 */
	if (token_is(tokens[0], "CONFIG") && nr >= 2) {
		char err[256] = {0};

		ipc_json_init(&out, json, sizeof(json));
		if (token_is(tokens[1], "GET") && nr == 2) {
			if (config_api_get_summary(&out)) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"configuration response too large");
			}
			return;
		}
		if (token_is(tokens[1], "GET") && nr == 3) {
			if (config_api_get_key(&out, tokens[2], err, sizeof(err))) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_NOT_FOUND, err);
			}
			return;
		}
		if (token_is(tokens[1], "LIST") && nr == 2) {
			if (config_api_list(&out)) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"configuration list too large");
			}
			return;
		}
		if (!config_async_has_capacity(err, sizeof(err))) {
			send_error(client, STELLAR_IPC_ERR_INTERNAL, err);
			return;
		}
		if (token_is(tokens[1], "STATUS") && nr == 2) {
			struct config_reply_token *token = begin_config_async(client);
			if (!token) {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"cannot allocate configuration request");
			} else if (!config_api_status_async(err, sizeof(err),
					config_async_done, token)) {
				abort_config_async(client, token);
				send_error(client, STELLAR_IPC_ERR_INTERNAL, err);
			}
			return;
		}
		if (token_is(tokens[1], "SET") && nr == 4) {
			struct config_reply_token *token = begin_config_async(client);
			if (!token) {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"cannot allocate configuration request");
				return;
			}
			if (!config_api_set_async(tokens[2], tokens[3], err,
					sizeof(err), config_async_done, token)) {
				abort_config_async(client, token);
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT, err);
			}
			return;
		}
		if (token_is(tokens[1], "RESET") && (nr == 3 || nr == 4)) {
			bool section = nr == 4;
			if (section && !token_is(tokens[2], "SECTION")) {
				send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
					"expected CONFIG RESET SECTION <name>");
				return;
			}
			const char *target = section ? tokens[3] : tokens[2];
			struct config_reply_token *token = begin_config_async(client);
			if (!token) {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"cannot allocate configuration request");
				return;
			}
			bool queued = section
				? config_api_reset_section_async(target, err,
					sizeof(err), config_async_done, token)
				: config_api_reset_async(target, err, sizeof(err),
					config_async_done, token);
			if (!queued) {
				abort_config_async(client, token);
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT, err);
			}
			return;
		}
		if (token_is(tokens[1], "KEYBINDINGS") && nr >= 3) {
			struct config_reply_token *token;
			bool queued = false;
			if (token_is(tokens[2], "LIST") && nr == 3) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_keybindings_list_async(err,
					sizeof(err), config_async_done, token);
			} else if (token_is(tokens[2], "SET") && nr == 5) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_keybindings_set_async(tokens[3],
					tokens[4], err, sizeof(err),
					config_async_done, token);
			} else if (token_is(tokens[2], "REMOVE") && nr == 4) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_keybindings_remove_async(tokens[3],
					err, sizeof(err), config_async_done, token);
			} else if (token_is(tokens[2], "RESET") && nr == 3) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_keybindings_reset_async(err,
					sizeof(err), config_async_done, token);
			} else {
				send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
					"unknown keybindings command");
				return;
			}
			if (!queued) {
				abort_config_async(client, token);
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT, err);
			}
			return;
		}
		if (token_is(tokens[1], "RULES") && nr >= 3) {
			struct config_reply_token *token;
			bool queued = false;
			if (token_is(tokens[2], "LIST") && nr == 3) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_rules_list_async(err, sizeof(err),
					config_async_done, token);
			} else if (token_is(tokens[2], "SET") && nr == 6) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_rules_set_async(tokens[3], tokens[4],
					tokens[5], err, sizeof(err),
					config_async_done, token);
			} else if (token_is(tokens[2], "REMOVE") &&
					(nr == 4 || nr == 5)) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_rules_remove_async(tokens[3],
					nr == 5 ? tokens[4] : NULL, err, sizeof(err),
					config_async_done, token);
			} else if (token_is(tokens[2], "RESET") && nr == 3) {
				token = begin_config_async(client);
				if (!token) {
					send_error(client, STELLAR_IPC_ERR_INTERNAL,
						"cannot allocate configuration request");
					return;
				}
				queued = config_api_rules_reset_async(err, sizeof(err),
					config_async_done, token);
			} else {
				send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
					"unknown rules command");
				return;
			}
			if (!queued) {
				abort_config_async(client, token);
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT, err);
			}
			return;
		}
		if (token_is(tokens[1], "RELOAD") && nr == 2) {
			if (config_api_async_reload(err, sizeof(err))) {
				send_json(client, "{\"ok\":true,\"reload\":true}", false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL, err);
			}
			return;
		}
		send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
			"unknown config command");
		return;
	}

	if (token_is(tokens[0], "PING") && nr == 1) {
		send_json(client, "{\"pong\":true}", false);
		return;
	}

	if (token_is(tokens[0], "VERSION") && nr == 1) {
		ipc_json_init(&out, json, sizeof(json));
		bool ok = ipc_json_add(&out,
			"{\"version\":\"%s\",\"upstream\":\"Labwc 0.20.3\","
			"\"wlroots\":\"%d.%d.%d\",\"xwayland\":%s,"
			"\"socket\":", STELLAR_WIM_VERSION,
			wlr_version_get_major(), wlr_version_get_minor(),
			wlr_version_get_micro(),
#if HAVE_XWAYLAND
			"true"
#else
			"false"
#endif
			)
			&& ipc_json_string(&out, socket_path)
			&& ipc_json_add(&out, ",\"features\":{"
				"\"ipc\":true,\"tabs\":%s,\"tiling\":%s,"
				"\"animations\":%s,\"notifications\":true},"
				"\"profile\":",
				rc.stellar.tabs_enabled ? "true" : "false",
				rc.stellar.tiling_enabled ? "true" : "false",
				rc.stellar.animations_enabled ? "true" : "false")
			&& ipc_json_string(&out,
				stellar_profile_name(rc.stellar.profile))
			&& ipc_json_add(&out, "}");
		if (ok) {
			send_json(client, json, false);
		} else {
			send_error(client, STELLAR_IPC_ERR_INTERNAL, "version too large");
		}
		return;
	}

	if (token_is(tokens[0], "CAPABILITIES") && nr == 1) {
		bool effects = rc.stellar.animations_enabled
			|| rc.stellar.opacity_enabled || rc.shadows_enabled;
		snprintf(json, sizeof(json),
			"{\"ipc\":1,\"windows\":1,\"desktops\":1,"
			"\"outputs\":1,\"notifications\":1,\"effects\":%d,"
			"\"tabs\":%d,\"tiling\":%d,\"profiles\":1}",
			effects ? 1 : 0,
			rc.stellar.tabs_enabled ? 1 : 0,
			rc.stellar.tiling_enabled ? 1 : 0);
		send_json(client, json, false);
		return;
	}

	if (token_is(tokens[0], "EFFECTS") && nr == 2 &&
			token_is(tokens[1], "STATUS")) {
		bool any = rc.stellar.animations_enabled
			|| rc.stellar.opacity_enabled || rc.shadows_enabled;
		/* Integer formatting keeps the JSON valid under any LC_NUMERIC. */
		int level = (int)(effects_target_opacity() * 100.0f + 0.5f);
		snprintf(json, sizeof(json),
			"{\"animations\":%s,\"opacity\":%s,"
			"\"shadows\":%s,\"blur\":false,\"enabled\":%s,"
			"\"active_animations\":%d,\"timer_armed\":%s,"
			"\"frame_commits\":%llu,\"animations_started\":%llu,"
			"\"opacity_level\":%d.%02d}",
			rc.stellar.animations_enabled ? "true" : "false",
			rc.stellar.opacity_enabled ? "true" : "false",
			rc.shadows_enabled ? "true" : "false",
			any ? "true" : "false",
			effects_active_animations(),
			effects_timer_armed() ? "true" : "false",
			(unsigned long long)effects_frame_commits(),
			(unsigned long long)effects_animations_started(),
			level / 100, level % 100);
		send_json(client, json, false);
		return;
	}

	if (token_is(tokens[0], "EVENTS") && nr == 2 &&
			token_is(tokens[1], "SUBSCRIBE")) {
		client->subscribed = true;
		send_json(client, "{\"subscribed\":true}", true);
		return;
	}

	if (token_is(tokens[0], "WINDOWS") && nr >= 2) {
		if (token_is(tokens[1], "LIST") && nr == 2) {
			bool first = true;
			bool ok = ipc_json_add(&out, "[");
			struct view *view;
			wl_list_for_each(view, &server.views, link) {
				ok = ok && ipc_json_add(&out, first ? "" : ",")
					&& json_view(&out, view);
				first = false;
			}
			ok = ok && ipc_json_add(&out, "]");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"window list too large");
			}
			return;
		}
		if (token_is(tokens[1], "ACTIVE") && nr == 2) {
			bool ok = ipc_json_add(&out, "null");
			if (server.active_view) {
				ok = json_view(&out, server.active_view);
			}
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"active view too large");
			}
			return;
		}
		if (nr == 3) {
			struct view *view = view_from_id(tokens[2]);
			if (!view) {
				send_error(client, STELLAR_IPC_ERR_NOT_FOUND,
					"window not found");
				return;
			}
			if (token_is(tokens[1], "ACTIVATE")) {
				desktop_focus_view(view, true);
			} else if (token_is(tokens[1], "MINIMIZE")) {
				view_minimize(view, true);
			} else if (token_is(tokens[1], "MAXIMIZE")) {
				view_maximize(view, VIEW_AXIS_BOTH);
			} else if (token_is(tokens[1], "CLOSE")) {
				view_close(view);
			} else {
				send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
					"unknown windows command");
				return;
			}
			snprintf(json, sizeof(json), "{\"id\":%llu,\"ok\":true}",
				(unsigned long long)view->creation_id);
			send_json(client, json, false);
			return;
		}
		if (nr == 4 && token_is(tokens[1], "MOVE_DESKTOP")) {
			struct view *view = view_from_id(tokens[2]);
			struct workspace *ws = workspace_from_index(tokens[3]);
			if (!view || !ws) {
				send_error(client, STELLAR_IPC_ERR_NOT_FOUND,
					"window or desktop not found");
				return;
			}
			view_move_to_workspace(view, ws);
			snprintf(json, sizeof(json),
				"{\"id\":%llu,\"workspace\":%d,\"ok\":true}",
				(unsigned long long)view->creation_id,
				workspace_index(ws));
			send_json(client, json, false);
			return;
		}
		send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
			"unknown windows command");
		return;
	}

	if (token_is(tokens[0], "DESKTOPS") && nr >= 2) {
		if (token_is(tokens[1], "LIST") && nr == 2) {
			bool first = true;
			bool ok = ipc_json_add(&out, "[");
			struct workspace *ws;
			wl_list_for_each(ws, &server.workspaces.all, link) {
				ok = ok && ipc_json_add(&out,
					"%s{\"index\":%d,\"name\":",
					first ? "" : ",", workspace_index(ws))
					&& ipc_json_string(&out, ws->name ? ws->name : "")
					&& ipc_json_add(&out, ",\"active\":%s}",
						server.workspaces.current == ws
							? "true" : "false");
				first = false;
			}
			ok = ok && ipc_json_add(&out, "]");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"desktop list too large");
			}
			return;
		}
		if (token_is(tokens[1], "SWITCH") && nr == 3) {
			struct workspace *ws = workspace_from_index(tokens[2]);
			if (!ws) {
				send_error(client, STELLAR_IPC_ERR_NOT_FOUND,
					"desktop not found");
				return;
			}
			workspaces_switch_to(ws, true);
			snprintf(json, sizeof(json), "{\"index\":%d,\"ok\":true}",
				workspace_index(ws));
			send_json(client, json, false);
			return;
		}
		send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
			"unknown desktops command");
		return;
	}

	if (token_is(tokens[0], "OUTPUTS") && nr == 2 &&
			token_is(tokens[1], "LIST")) {
		bool first = true;
		bool ok = ipc_json_add(&out, "[");
		struct output *output;
		wl_list_for_each(output, &server.outputs, link) {
			struct wlr_box box = {0};
			wlr_output_layout_get_box(server.output_layout,
				output->wlr_output, &box);
			const struct wlr_output_mode *mode =
				output->wlr_output->current_mode;
			ok = ok && ipc_json_add(&out,
				"%s{\"name\":", first ? "" : ",")
				&& ipc_json_string(&out,
					output->wlr_output->name
						? output->wlr_output->name : "")
				&& ipc_json_add(&out,
					",\"x\":%d,\"y\":%d,\"width\":%d,"
					"\"height\":%d,\"refresh\":%d,\"enabled\":%s}",
					box.x, box.y, box.width, box.height,
					mode ? mode->refresh : 0,
					output->wlr_output->enabled ? "true" : "false");
			first = false;
		}
		ok = ok && ipc_json_add(&out, "]");
		if (ok) {
			send_json(client, json, false);
		} else {
			send_error(client, STELLAR_IPC_ERR_INTERNAL,
				"output list too large");
		}
		return;
	}

	if (token_is(tokens[0], "PROFILE") && nr >= 2) {
		if (token_is(tokens[1], "GET") && nr == 2) {
			ipc_json_init(&out, json, sizeof(json));
			bool ok = ipc_json_add(&out, "{\"profile\":")
				&& ipc_json_string(&out,
					stellar_profile_name(rc.stellar.profile))
				&& ipc_json_add(&out, "}");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"profile too large");
			}
			return;
		}
		if (token_is(tokens[1], "SET") && nr == 3) {
			enum stellar_profile profile;
			if (!stellar_profile_parse(tokens[2], &profile) ||
					profile == STELLAR_PROFILE_CUSTOM) {
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT,
					"expected minimal or enhanced");
				return;
			}
			if (!stellar_profile_set(profile)) {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"profile switch failed");
				return;
			}
			ipc_json_init(&out, json, sizeof(json));
			bool ok = ipc_json_add(&out, "{\"profile\":")
				&& ipc_json_string(&out, stellar_profile_name(profile))
				&& ipc_json_add(&out, ",\"ok\":true}");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"profile too large");
			}
			return;
		}
		send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
			"unknown profile command");
		return;
	}

	if (token_is(tokens[0], "TABS") && nr >= 2) {
		if (token_is(tokens[1], "STATUS") && nr == 2) {
			snprintf(json, sizeof(json),
				"{\"enabled\":%s,\"groups\":%zu}",
				rc.stellar.tabs_enabled ? "true" : "false",
				tabs_count_groups());
			send_json(client, json, false);
			return;
		}
		if (!rc.stellar.tabs_enabled) {
			send_error(client, STELLAR_IPC_ERR_DISABLED,
				"tabs are disabled");
			return;
		}
		if (token_is(tokens[1], "LIST") && nr == 2) {
			bool first_group = true;
			bool ok = ipc_json_add(&out, "[");
			struct tab_group *group;
			const struct wl_list *groups = tabs_groups();
			wl_list_for_each(group, groups, link) {
				ok = ok && ipc_json_add(&out,
					"%s{\"id\":%llu,\"active\":",
					first_group ? "" : ",",
					(unsigned long long)group->id);
				struct view *active = tabs_group_active_view(group);
				ok = ok && ipc_json_add(&out, "%llu",
					active ? (unsigned long long)active->creation_id : 0)
					&& ipc_json_add(&out, ",\"members\":[");
				bool first_member = true;
				struct view *member;
				wl_list_for_each(member, &group->members, stellar_tab_link) {
					ok = ok && ipc_json_add(&out, "%s%llu",
						first_member ? "" : ",",
						(unsigned long long)member->creation_id);
					first_member = false;
				}
				ok = ok && ipc_json_add(&out, "]}");
				first_group = false;
			}
			ok = ok && ipc_json_add(&out, "]");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"tab list too large");
			}
			return;
		}
		if (token_is(tokens[1], "GROUP") && nr >= 4) {
			size_t count = (size_t)(nr - 2);
			if (count > STELLAR_IPC_MAX_GROUP_WINDOWS) {
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT,
					"too many windows");
				return;
			}
			struct view *views[STELLAR_IPC_MAX_GROUP_WINDOWS] = {0};
			for (size_t i = 0; i < count; i++) {
				views[i] = view_from_id(tokens[i + 2]);
				if (!views[i]) {
					send_error(client, STELLAR_IPC_ERR_NOT_FOUND,
						"window not found");
					return;
				}
			}
			if (!tabs_group_views(views, count)) {
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT,
					"cannot group those windows");
				return;
			}
			snprintf(json, sizeof(json),
				"{\"id\":%llu,\"ok\":true}",
				(unsigned long long)views[0]->stellar_tab_group->id);
			send_json(client, json, false);
			return;
		}
		if ((token_is(tokens[1], "NEXT") ||
				token_is(tokens[1], "PREVIOUS")) && nr == 2) {
			struct view *view = server.active_view;
			if (!view || !tabs_view_group(view)) {
				send_error(client, STELLAR_IPC_ERR_NOT_FOUND,
					"active window is not in a tab group");
				return;
			}
			tabs_switch(view, token_is(tokens[1], "NEXT"));
			send_json(client, "{\"ok\":true}", false);
			return;
		}
		if (token_is(tokens[1], "DETACH") && nr == 3) {
			struct view *view = view_from_id(tokens[2]);
			if (!view || !tabs_view_group(view)) {
				send_error(client, STELLAR_IPC_ERR_NOT_FOUND,
					"window is not in a tab group");
				return;
			}
			tabs_detach(view);
			send_json(client, "{\"ok\":true}", false);
			return;
		}
		send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
			"unknown tabs command");
		return;
	}

	if (token_is(tokens[0], "TILING") && nr >= 2) {
		if (token_is(tokens[1], "STATUS") && nr == 2) {
			int managed = 0;
			struct view *view;
			wl_list_for_each(view, &server.views, link) {
				if (stellar_tiling_is_managed(view)) {
					managed++;
				}
			}
			float ratio = rc.stellar.tiling_master_ratio;
			if (!isfinite(ratio) || ratio < 0.2f || ratio > 0.8f) {
				ratio = 0.6f;
			}
			ipc_json_init(&out, json, sizeof(json));
			bool ok = ipc_json_add(&out, "{\"enabled\":%s,"
				"\"managed\":%d,\"mode\":",
				stellar_tiling_is_enabled() ? "true" : "false",
				managed)
				&& ipc_json_string(&out,
					stellar_tiling_mode_name(rc.stellar.tiling_mode))
				&& ipc_json_add(&out, ",\"master_ratio\":%d.%02d,"
					"\"columns\":%d,\"outputs\":[",
					(int)ratio,
					(int)(ratio * 100.0f + 0.5f) % 100,
					rc.stellar.tiling_columns);
			bool first = true;
			struct output *output;
			wl_list_for_each(output, &server.outputs, link) {
				if (!ok) {
					break;
				}
				int output_managed = 0;
				wl_list_for_each(view, &server.views, link) {
					if (stellar_tiling_is_managed(view) &&
							view->output == output) {
						output_managed++;
					}
				}
				const char *name = output->wlr_output
					? output->wlr_output->name : "unknown";
				ok = ipc_json_add(&out, "%s{\"name\":", first ? "" : ",")
					&& ipc_json_string(&out, name)
					&& ipc_json_add(&out, ",\"managed\":%d,\"mode\":",
						output_managed)
					&& ipc_json_string(&out,
						stellar_tiling_mode_name(
							stellar_tiling_output_mode(output)))
					&& ipc_json_add(&out, "}");
				first = false;
			}
			ok = ok && ipc_json_add(&out, "]}");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"tiling status too large");
			}
			return;
		}
		if (token_is(tokens[1], "TOGGLE") && nr == 2) {
			stellar_tiling_toggle();
			ipc_json_init(&out, json, sizeof(json));
			bool ok = ipc_json_add(&out,
				"{\"enabled\":%s,\"ok\":true}",
				stellar_tiling_is_enabled() ? "true" : "false");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"tiling toggle too large");
			}
			return;
		}
		if (token_is(tokens[1], "SET") && nr == 3) {
			enum stellar_tiling_mode mode;
			if (!stellar_tiling_parse_mode(tokens[2], &mode)) {
				send_error(client, STELLAR_IPC_ERR_INVALID_ARGUMENT,
					"unknown tiling mode");
				return;
			}
			rc.stellar.tiling_mode = mode;
			rc.stellar.profile = STELLAR_PROFILE_CUSTOM;
			stellar_tiling_reapply();
			ipc_json_init(&out, json, sizeof(json));
			bool ok = ipc_json_add(&out, "{\"mode\":")
				&& ipc_json_string(&out,
					stellar_tiling_mode_name(mode))
				&& ipc_json_add(&out, ",\"ok\":true}");
			if (ok) {
				send_json(client, json, false);
			} else {
				send_error(client, STELLAR_IPC_ERR_INTERNAL,
					"tiling status too large");
			}
			return;
		}
		if (token_is(tokens[1], "REAPPLY") && nr == 2) {
			if (!rc.stellar.tiling_enabled) {
				send_error(client, STELLAR_IPC_ERR_DISABLED,
					"tiling is disabled");
				return;
			}
			stellar_tiling_reapply();
			send_json(client, "{\"ok\":true}", false);
			return;
		}
		send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
			"unknown tiling command");
		return;
	}

	send_error(client, STELLAR_IPC_ERR_UNKNOWN_COMMAND,
		"unknown command");
}

static void
ipc_stop_listening(void)
{
	if (listen_source) {
		wl_event_source_remove(listen_source);
		listen_source = NULL;
	}
	if (listen_fd >= 0) {
		close(listen_fd);
		listen_fd = -1;
	}
	if (socket_path[0]) {
		unlink(socket_path);
		socket_path[0] = '\0';
	}
}

static void
ipc_shutdown_now(void)
{
	if (!running) {
		return;
	}
	shutdown_pending = false;
	shutting_down = true;
	ipc_stop_listening();
	struct ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &clients, link) {
		client_destroy(client);
	}
	shutting_down = false;
	running = false;
	wlr_log(WLR_INFO, "Stellar IPC stopped");
}

static void
client_destroy(struct ipc_client *client)
{
	if (!client) {
		return;
	}
	wl_list_remove(&client->link);
	nr_clients--;
	if (client->source) {
		wl_event_source_remove(client->source);
	}
	close(client->fd);

	struct ipc_event *event, *tmp;
	wl_list_for_each_safe(event, tmp, &client->events, link) {
		wl_list_remove(&event->link);
		free(event);
	}
	free(client);

	/*
	 * ipc.enabled=false is applied on reload. Stop accepting connections at
	 * once, but keep existing clients until their reply and queued events
	 * have been written to the socket.
	 */
	if (!shutting_down && shutdown_pending && running &&
			wl_list_empty(&clients)) {
		ipc_shutdown_now();
	}
}

static struct ipc_client *
find_client_by_id(uint64_t id)
{
	struct ipc_client *client;
	wl_list_for_each(client, &clients, link) {
		if (client->id == id) {
			return client;
		}
	}
	return NULL;
}

static void
queue_next_event(struct ipc_client *client)
{
	if (client->out_len || client->close_after_flush ||
			client->write_pending || wl_list_empty(&client->events)) {
		return;
	}
	struct ipc_event *event = wl_container_of(client->events.next, event, link);
	wl_list_remove(&event->link);
	client->nr_events--;
	memcpy(client->out, event->data, event->len);
	client->out_len = event->len;
	client->out_off = 0;
	free(event);
	wl_event_source_fd_update(client->source,
		WL_EVENT_READABLE | WL_EVENT_WRITABLE);
}

static int
client_handle_fd(int fd, uint32_t mask, void *data)
{
	struct ipc_client *client = data;
	assert(fd == client->fd);

	/* Drain pending input first: a client may send and close immediately. */
	if (mask & WL_EVENT_WRITABLE && client->out_len > 0) {
		ssize_t n = send(client->fd, client->out + client->out_off,
			client->out_len - client->out_off, MSG_NOSIGNAL);
		if (n > 0) {
			client->out_off += (size_t)n;
			if (client->out_off == client->out_len) {
				client->out_len = 0;
				if (client->close_after_flush) {
					client_destroy(client);
					return 0;
				}
				queue_next_event(client);
				if (client->out_len == 0) {
					wl_event_source_fd_update(client->source,
						WL_EVENT_READABLE);
				}
			}
		} else if (n < 0 && errno != EAGAIN && errno != EINTR) {
			client_destroy(client);
			return 0;
		}
	}

	if (mask & WL_EVENT_READABLE && client->out_len == 0 &&
			!client->write_pending) {
		while (true) {
			if (client->in_len < STELLAR_IPC_HEADER_SIZE) {
				client->need = STELLAR_IPC_HEADER_SIZE;
			} else if (client->need == STELLAR_IPC_HEADER_SIZE) {
				/* Header is complete but the payload is not here yet. */
				uint32_t declared = frame_length(client->in);
				if (frame_magic(client->in) != STELLAR_IPC_MAGIC ||
						declared == 0 ||
						declared > STELLAR_IPC_MAX_REQUEST) {
					client_destroy(client);
					return 0;
				}
				client->need = STELLAR_IPC_HEADER_SIZE + declared;
			}
			if (client->in_len >= client->need &&
					client->need > STELLAR_IPC_HEADER_SIZE) {
				const char *payload;
				size_t payload_len;
				if (!ipc_protocol_decode(client->in,
						client->in_len, &payload, &payload_len)) {
					client_destroy(client);
					return 0;
				}
				dispatch(client,
					(const unsigned char *)payload, payload_len);
				client->in_len = 0;
				client->need = STELLAR_IPC_HEADER_SIZE;
				if (client->out_len || client->close_after_flush ||
						client->write_pending) {
					break;
				}
				continue;
			}
			if (client->in_len >= sizeof(client->in)) {
				client_destroy(client);
				return 0;
			}
			ssize_t n = recv(client->fd,
				client->in + client->in_len,
				client->need - client->in_len, 0);
			if (n > 0) {
				client->in_len += (size_t)n;
				continue;
			}
			if (n == 0) {
				client_destroy(client);
				return 0;
			}
			if (errno == EAGAIN || errno == EINTR) {
				break;
			}
			client_destroy(client);
			return 0;
		}
	}

	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		client_destroy(client);
	}
	return 0;
}

static int
handle_listen_fd(int fd, uint32_t mask, void *data)
{
	while (true) {
		int client_fd = accept4(fd, NULL, NULL,
			SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (client_fd < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				return 0;
			}
			wlr_log_errno(WLR_ERROR, "stellar IPC accept failed");
			return 0;
		}

		struct ucred cred = {0};
		socklen_t cred_len = sizeof(cred);
		if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED,
				&cred, &cred_len) < 0 || cred_len != sizeof(cred)
				|| cred.uid != geteuid()) {
			wlr_log(WLR_INFO, "rejected IPC client with different uid");
			close(client_fd);
			continue;
		}
		if (nr_clients >= MAX_CLIENTS) {
			wlr_log(WLR_INFO, "too many IPC clients");
			close(client_fd);
			continue;
		}

		struct ipc_client *client = calloc(1, sizeof(*client));
		if (!client) {
			close(client_fd);
			continue;
		}
		client->fd = client_fd;
		client->id = next_client_id++;
		if (next_client_id == 0) {
			next_client_id = 1;
		}
		client->uid = cred.uid;
		client->need = STELLAR_IPC_HEADER_SIZE;
		wl_list_init(&client->events);
		client->source = wl_event_loop_add_fd(server.wl_event_loop,
			client_fd, WL_EVENT_READABLE, client_handle_fd, client);
		if (!client->source) {
			free(client);
			close(client_fd);
			continue;
		}
		wl_list_insert(&clients, &client->link);
		nr_clients++;
	}
}

static void
sanitize_display_name(const char *display, char *dst, size_t cap)
{
	size_t i = 0;
	if (!display) {
		display = "default";
	}
	for (const char *p = display; *p && i + 1 < cap && i < MAX_DISPLAY_LEN; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
				(*p >= '0' && *p <= '9') || *p == '-' || *p == '_') {
			dst[i++] = *p;
		}
	}
	dst[i] = '\0';
	if (i == 0) {
		snprintf(dst, cap, "default");
	}
}

static bool
compute_socket_path(void)
{
	const char *override = getenv("STELLAR_WIM_IPC_SOCKET");
	if (override && override[0]) {
		if (strlen(override) >= sizeof(socket_path)) {
			return false;
		}
		strcpy(socket_path, override);
		return true;
	}

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !*runtime) {
		wlr_log(WLR_ERROR, "XDG_RUNTIME_DIR is unset; disabling IPC");
		return false;
	}
	char display[MAX_DISPLAY_LEN + 1];
	sanitize_display_name(getenv("WAYLAND_DISPLAY"), display, sizeof(display));
	int written = snprintf(socket_path, sizeof(socket_path),
		"%s/stellar-wim-%s.sock", runtime, display);
	return written > 0 && (size_t)written < sizeof(socket_path);
}

static bool
ipc_start_listening(void)
{
	if (!compute_socket_path()) {
		return false;
	}

	struct stat st;
	if (lstat(socket_path, &st) == 0) {
		if (!S_ISSOCK(st.st_mode)) {
			wlr_log(WLR_ERROR, "IPC socket path is not a socket: %s",
				socket_path);
			return false;
		}
		if (socket_path_in_use(socket_path)) {
			wlr_log(WLR_ERROR, "IPC socket already in use: %s",
				socket_path);
			return false;
		}
		if (unlink(socket_path) < 0) {
			wlr_log_errno(WLR_ERROR, "cannot remove stale IPC socket %s",
				socket_path);
			return false;
		}
	} else if (errno != ENOENT) {
		wlr_log_errno(WLR_ERROR, "cannot inspect IPC socket path");
		return false;
	}

	listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (listen_fd < 0) {
		wlr_log_errno(WLR_ERROR, "cannot create IPC socket");
		return false;
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	if (strlen(socket_path) >= sizeof(addr.sun_path)) {
		wlr_log(WLR_ERROR, "IPC socket path is too long");
		goto error;
	}
	strcpy(addr.sun_path, socket_path);

	mode_t old_umask = umask(077);
	int rc_bind = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
	umask(old_umask);
	if (rc_bind < 0) {
		wlr_log_errno(WLR_ERROR, "cannot bind IPC socket %s", socket_path);
		goto error;
	}
	/* fchmod() does not change the inode of an AF_UNIX socket on Linux. */
	if (chmod(socket_path, 0600) < 0 || listen(listen_fd, 8) < 0) {
		wlr_log_errno(WLR_ERROR, "cannot secure/listen on IPC socket");
		goto error;
	}

	listen_source = wl_event_loop_add_fd(server.wl_event_loop, listen_fd,
		WL_EVENT_READABLE, handle_listen_fd, NULL);
	if (!listen_source) {
		wlr_log(WLR_ERROR, "cannot add IPC socket to event loop");
		goto error;
	}

	running = true;
	wlr_log(WLR_INFO, "Stellar IPC listening on %s", socket_path);
	return true;

error:
	close(listen_fd);
	listen_fd = -1;
	unlink(socket_path);
	socket_path[0] = '\0';
	return false;
}

void
ipc_init(void)
{
	if (running) {
		return;
	}
	wl_list_init(&clients);
	if (!rc.stellar.ipc_enabled) {
		wlr_log(WLR_INFO, "Stellar IPC disabled by configuration");
		return;
	}
	ipc_start_listening();
}

void
ipc_finish(void)
{
	ipc_shutdown_now();
}

void
ipc_reload(void)
{
	bool want = rc.stellar.ipc_enabled;
	if (!running) {
		if (want) {
			ipc_init();
		}
		return;
	}
	if (!want) {
		if (wl_list_empty(&clients)) {
			ipc_finish();
			return;
		}
		shutdown_pending = true;
		ipc_stop_listening();
		wlr_log(WLR_INFO,
			"Stellar IPC will stop after pending replies are flushed");
		return;
	}
	if (shutdown_pending) {
		/* Re-enabled before the draining clients disconnected. */
		shutdown_pending = false;
		ipc_start_listening();
	}
}

bool
ipc_is_running(void)
{
	return running;
}

const char *
ipc_socket_path(void)
{
	return running ? socket_path : NULL;
}

static void
notify_all(const char *json)
{
	char payload[STELLAR_IPC_MAX_EVENT];
	if (!ipc_response_event(payload, sizeof(payload), json)) {
		return;
	}
	size_t len = strlen(payload);
	size_t frame_len = 0;
	unsigned char frame[STELLAR_IPC_HEADER_SIZE + STELLAR_IPC_MAX_EVENT];
	if (!ipc_protocol_encode(frame, sizeof(frame), payload, len, &frame_len)) {
		return;
	}

	struct ipc_client *client;
	wl_list_for_each(client, &clients, link) {
		if (!client->subscribed) {
			continue;
		}
		if (client->nr_events >= MAX_EVENTS_PER_CLIENT) {
			continue; /* slow subscribers are dropped, never block the compositor */
		}
		struct ipc_event *event = calloc(1, sizeof(*event));
		if (!event) {
			continue;
		}
		memcpy(event->data, frame, frame_len);
		event->len = frame_len;
		wl_list_insert(client->events.prev, &event->link);
		client->nr_events++;
		queue_next_event(client);
	}
}

void
ipc_notify_view(const char *event, struct view *view)
{
	if (!running || wl_list_empty(&clients) || !view) {
		return;
	}
	char json[STELLAR_IPC_MAX_EVENT - 32];
	struct ipc_json out;
	ipc_json_init(&out, json, sizeof(json));
	bool ok = ipc_json_add(&out, "{\"type\":\"view\",\"event\":")
		&& ipc_json_string(&out, event ? event : "update")
		&& ipc_json_add(&out, ",\"view\":")
		&& json_view(&out, view);
	if (ok) {
		notify_all(json);
	}
}

void
ipc_notify_workspace(struct workspace *workspace)
{
	if (!running || wl_list_empty(&clients) || !workspace) {
		return;
	}
	char json[STELLAR_IPC_MAX_EVENT - 32];
	struct ipc_json out;
	ipc_json_init(&out, json, sizeof(json));
	bool ok = ipc_json_add(&out,
		"{\"type\":\"workspace\",\"event\":\"switch\",\"index\":%d,\"name\":",
		workspace_index(workspace))
		&& ipc_json_string(&out, workspace->name ? workspace->name : "")
		&& ipc_json_add(&out, "}");
	if (ok) {
		notify_all(json);
	}
}

void
ipc_notify_tabs(const char *event, uint64_t group_id, uint64_t view_id)
{
	if (!running || wl_list_empty(&clients)) {
		return;
	}
	char json[STELLAR_IPC_MAX_EVENT - 32];
	snprintf(json, sizeof(json),
		"{\"type\":\"tabs\",\"event\":\"%s\","
		"\"group\":%llu,\"view\":%llu}",
		event ? event : "update",
		(unsigned long long)group_id, (unsigned long long)view_id);
	notify_all(json);
}

void
ipc_notify_tiling(const char *event, int managed)
{
	if (!running || wl_list_empty(&clients)) {
		return;
	}
	char json[STELLAR_IPC_MAX_EVENT - 32];
	snprintf(json, sizeof(json),
		"{\"type\":\"tiling\",\"event\":\"%s\",\"managed\":%d}",
		event ? event : "update", managed);
	notify_all(json);
}

void
ipc_notify_profile(const char *event, const char *profile)
{
	if (!running || wl_list_empty(&clients)) {
		return;
	}
	char json[STELLAR_IPC_MAX_EVENT - 32];
	snprintf(json, sizeof(json),
		"{\"type\":\"profile\",\"event\":\"%s\",\"profile\":\"%s\"}",
		event ? event : "update", profile ? profile : "custom");
	notify_all(json);
}

void
ipc_notify_config(const char *event, const char *key,
	const char *value_json, bool persisted)
{
	if (!running || wl_list_empty(&clients)) {
		return;
	}
	char json[STELLAR_IPC_MAX_EVENT - 32];
	snprintf(json, sizeof(json),
		"{\"type\":\"config\",\"event\":\"%s\",\"key\":\"%s\","
		"\"value\":%s,\"persisted\":%s}",
		event ? event : "changed", key ? key : "",
		value_json ? value_json : "null", persisted ? "true" : "false");
	notify_all(json);
}
