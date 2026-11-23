/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_IPC_PROTOCOL_H
#define STELLAR_IPC_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Stellar IPC uses a length-prefixed line protocol over a Unix socket.
 *
 *   [u32 magic][u32 length][payload]
 *
 * All integers are little-endian. Payloads are ASCII/UTF-8 text without a
 * trailing NUL. Requests are `VERB [arg ...]`, responses are either
 * `OK <json>` or `ERR <code> <message>` and notifications `EVENT <json>`.
 *
 * Limits are deliberately small: IPC is a control channel, not a transport
 * for image or clipboard data.
 */
#define STELLAR_IPC_MAGIC UINT32_C(0x5357494d) /* "SWIM" */
#define STELLAR_IPC_HEADER_SIZE 8
#define STELLAR_IPC_MAX_REQUEST 4096
#define STELLAR_IPC_MAX_RESPONSE 65536
#define STELLAR_IPC_MAX_EVENT 4096
/*
 * A GROUP request contains one verb plus up to 32 window ids; keep enough
 * headroom for future commands while retaining a hard parsing bound.
 */
#define STELLAR_IPC_MAX_GROUP_WINDOWS 32
#define STELLAR_IPC_MAX_TOKENS (STELLAR_IPC_MAX_GROUP_WINDOWS + 8)

enum stellar_ipc_error {
	STELLAR_IPC_ERR_BAD_REQUEST = 1,
	STELLAR_IPC_ERR_UNKNOWN_COMMAND = 2,
	STELLAR_IPC_ERR_NOT_FOUND = 3,
	STELLAR_IPC_ERR_INVALID_ARGUMENT = 4,
	STELLAR_IPC_ERR_INTERNAL = 5,
	STELLAR_IPC_ERR_DISABLED = 6,
};

struct ipc_json {
	char *data;
	size_t len;
	size_t cap;
};

/* Frame helpers. Return false when the payload does not fit in cap. */
bool ipc_protocol_encode(uint8_t *buf, size_t cap, const char *payload,
	size_t payload_len, size_t *out_len);
bool ipc_protocol_decode(const uint8_t *buf, size_t len,
	const char **payload, size_t *payload_len);

/* Tokenize a NUL-terminated, mutable request in place. */
int ipc_protocol_tokenize(char *text, char *tokens[], int max_tokens);

/* Bounded JSON writer (no heap allocation). */
void ipc_json_init(struct ipc_json *json, char *buf, size_t cap);
bool ipc_json_add(struct ipc_json *json, const char *fmt, ...);
bool ipc_json_string(struct ipc_json *json, const char *value);

bool ipc_response_ok(char *buf, size_t cap, const char *json);
bool ipc_response_error(char *buf, size_t cap, int code, const char *message);
bool ipc_response_event(char *buf, size_t cap, const char *json);

#endif /* STELLAR_IPC_PROTOCOL_H */
