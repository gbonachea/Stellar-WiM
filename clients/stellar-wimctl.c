// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "ipc/protocol.h"

static const char usage_text[] =
"Usage: stellar-wimctl <command> [arguments]\n"
"\n"
"Commands:\n"
"  version                       Compositor and protocol version\n"
"  capabilities                  Supported IPC capabilities\n"
"  config get                   Read current configuration summary\n"
"  config get <key>             Read one configuration value and its metadata\n"
"  config list                  List configurable keys, types and bounds\n"
"  config status                Show the active rc.xml path and writability\n"
"  config set <key> <value>     Validate, save and apply a configuration value\n"
"  config reset <key>           Restore one key to its default\n"
"  config reset section <name>  Restore a section (or 'all') to defaults\n"
"  config reload                Re-read rc.xml without restarting\n"
"  config keybindings list      List managed keybindings as JSON\n"
"  config keybindings set <key> <action>\n"
"                               Add/replace a keybind using a safe action\n"
"  config keybindings remove <key>\n"
"                               Remove a keybinding\n"
"  config keybindings reset     Remove all user keybindings\n"
"  config rules list            List window rules as JSON\n"
"  config rules set <id> <prop> <value>\n"
"                               Add/update a validated window rule property\n"
"  config rules remove <id> [prop]\n"
"                               Remove a window rule or one property\n"
"  config rules reset           Remove all window rules\n"
"  ping                          Check that IPC is reachable\n"
"  windows list                  List all windows as JSON\n"
"  windows active                Active window as JSON (or null)\n"
"  windows activate <id>         Focus and raise a window\n"
"  windows minimize <id>         Minimize a window\n"
"  windows maximize <id>         Maximize a window\n"
"  windows close <id>            Close a window (with fade when enabled)\n"
"  windows move-desktop <id> <n> Move window to desktop n (1-based)\n"
"  desktops list                 List desktops as JSON\n"
"  desktops switch <n>           Switch to desktop n (1-based)\n"
"  outputs list                  List outputs as JSON\n"
"  profile get                   Report the active profile\n"
"  profile set <minimal|enhanced> Switch preset safely at runtime\n"
"  tabs status                   Report tab-group support and group count\n"
"  tabs list                     List tab groups as JSON\n"
"  tabs group <id> <id> [id...] Group two or more windows into a tab group\n"
"  tabs next                     Activate the next tab\n"
"  tabs previous                 Activate the previous tab\n"
"  tabs detach <id>              Remove a window from its tab group\n"
"  tiling status                 Report mode, per-output mode and windows\n"
"  tiling set <mode>             Set master-stack|horizontal|vertical|grid|hybrid\n"
"  tiling toggle                 Enable or disable tiling\n"
"  tiling reapply                Recompute the tiling layout\n"
"  effects status                Report visual effect state\n"
"  events subscribe              Print state-change events until disconnected\n"
"\n"
"Environment:\n"
"  XDG_RUNTIME_DIR               Runtime directory (required)\n"
"  WAYLAND_DISPLAY               Used to locate the compositor socket\n"
"  STELLAR_WIM_IPC_SOCKET        Override the socket path\n";

static bool
sanitize_display(const char *input, char *out, size_t cap)
{
	size_t len = 0;
	if (!input || !*input) {
		input = "default";
	}
	for (const char *p = input; *p && len + 1 < cap; p++) {
		bool valid = (*p >= 'a' && *p <= 'z') ||
			(*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') ||
			*p == '-' || *p == '_';
		if (valid) {
			out[len++] = *p;
		}
	}
	out[len] = '\0';
	return len > 0;
}

static bool
resolve_socket(char *buf, size_t cap)
{
	const char *override = getenv("STELLAR_WIM_IPC_SOCKET");
	if (override && *override) {
		if (strlen(override) >= cap) {
			return false;
		}
		strcpy(buf, override);
		return true;
	}
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !*runtime) {
		fprintf(stderr, "stellar-wimctl: XDG_RUNTIME_DIR is unset\n");
		return false;
	}
	char display[64];
	if (!sanitize_display(getenv("WAYLAND_DISPLAY"), display, sizeof(display))) {
		return false;
	}
	int written = snprintf(buf, cap, "%s/stellar-wim-%s.sock", runtime, display);
	return written > 0 && (size_t)written < cap;
}

static int
connect_socket(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "stellar-wimctl: socket path too long\n");
		close(fd);
		return -1;
	}
	strcpy(addr.sun_path, path);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "stellar-wimctl: cannot connect to %s: %s\n",
			path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static bool
read_exact(int fd, void *buf, size_t len)
{
	unsigned char *p = buf;
	while (len > 0) {
		ssize_t n = recv(fd, p, len, 0);
		if (n > 0) {
			p += n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			continue;
		}
		return false;
	}
	return true;
}

static char *
read_frame(int fd)
{
	unsigned char header[STELLAR_IPC_HEADER_SIZE];
	if (!read_exact(fd, header, sizeof(header))) {
		fprintf(stderr, "stellar-wimctl: connection closed\n");
		return NULL;
	}
	uint32_t magic = (uint32_t)header[0]
		| ((uint32_t)header[1] << 8)
		| ((uint32_t)header[2] << 16)
		| ((uint32_t)header[3] << 24);
	if (magic != STELLAR_IPC_MAGIC) {
		fprintf(stderr, "stellar-wimctl: invalid frame header\n");
		return NULL;
	}
	size_t payload_len = (size_t)header[4]
		| ((size_t)header[5] << 8)
		| ((size_t)header[6] << 16)
		| ((size_t)header[7] << 24);
	if (payload_len == 0 || payload_len > STELLAR_IPC_MAX_RESPONSE) {
		fprintf(stderr, "stellar-wimctl: invalid frame length\n");
		return NULL;
	}
	char *text = calloc(payload_len + 1, 1);
	if (!text || !read_exact(fd, text, payload_len)) {
		free(text);
		return NULL;
	}
	return text;
}

static bool
send_request(int fd, const char *request)
{
	unsigned char frame[STELLAR_IPC_HEADER_SIZE + STELLAR_IPC_MAX_REQUEST];
	size_t frame_len = 0;
	size_t len = strlen(request);
	if (len > STELLAR_IPC_MAX_REQUEST ||
			!ipc_protocol_encode(frame, sizeof(frame), request, len,
				&frame_len)) {
		return false;
	}
	size_t sent = 0;
	while (sent < frame_len) {
		ssize_t n = send(fd, frame + sent, frame_len - sent, MSG_NOSIGNAL);
		if (n > 0) {
			sent += (size_t)n;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			continue;
		}
		return false;
	}
	return true;
}

/* Build a whitespace-normalized request; reject control characters. */
static bool
build_request(char *dst, size_t cap, int argc, char **argv)
{
	size_t len = 0;
	for (int i = 0; i < argc; i++) {
		const char *arg = argv[i];
		if (!*arg) {
			return false;
		}
		for (const unsigned char *p = (const unsigned char *)arg; *p; p++) {
			if (*p <= 0x20 || *p == 0x7f) {
				return false;
			}
		}
		size_t arg_len = strlen(arg);
		if (len + arg_len + 2 > cap) {
			return false;
		}
		if (len > 0) {
			dst[len++] = ' ';
		}
		memcpy(dst + len, arg, arg_len);
		len += arg_len;
	}
	dst[len] = '\0';
	return true;
}

static int
print_response(const char *response)
{
	if (!response) {
		return 1;
	}
	if (strncmp(response, "OK ", 3) == 0) {
		printf("%s\n", response + 3);
		fflush(stdout);
		return 0;
	}
	if (strncmp(response, "ERR ", 4) == 0) {
		fprintf(stderr, "stellar-wimctl: %s\n", response + 4);
		return 1;
	}
	fprintf(stderr, "stellar-wimctl: malformed response\n");
	return 1;
}

/*
 * The server matches verbs case-insensitively, so the CLI keeps the original
 * case of arguments. Uppercasing them would corrupt configuration keys,
 * theme names and rule identifiers.
 */
static int
build_client_request(char *dst, size_t cap, int argc, char **argv)
{
	char joined[STELLAR_IPC_MAX_REQUEST + 1];
	if (!build_request(joined, sizeof(joined), argc, argv) ||
			strlen(joined) >= cap) {
		return -1;
	}
	strcpy(dst, joined);
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
		fputs(usage_text, stdout);
		return argc < 2 ? 2 : 0;
	}

	char request[STELLAR_IPC_MAX_REQUEST + 1];
	if (build_client_request(request, sizeof(request), argc - 1,
			argv + 1) < 0) {
		fputs(usage_text, stderr);
		return 2;
	}

	/* Validate the command before opening a socket. */
	char *tokens[STELLAR_IPC_MAX_TOKENS];
	char copy[sizeof(request)];
	strcpy(copy, request);
	int nr = ipc_protocol_tokenize(copy, tokens, STELLAR_IPC_MAX_TOKENS);
	if (nr < 1) {
		fputs(usage_text, stderr);
		return 2;
	}

	char socket_path[PATH_MAX];
	if (!resolve_socket(socket_path, sizeof(socket_path))) {
		return 1;
	}
	int fd = connect_socket(socket_path);
	if (fd < 0) {
		return 1;
	}
	if (!send_request(fd, request)) {
		close(fd);
		fprintf(stderr, "stellar-wimctl: failed to send request\n");
		return 1;
	}

	char *response = read_frame(fd);
	int status = print_response(response);
	free(response);

	bool subscribe = strcasecmp(request, "events subscribe") == 0;
	if (status == 0 && subscribe) {
		while ((response = read_frame(fd)) != NULL) {
			printf("%s\n", response);
			fflush(stdout);
			free(response);
		}
	}
	close(fd);
	return status;
}
