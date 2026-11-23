// SPDX-License-Identifier: GPL-2.0-only
#include "ipc/protocol.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void
put_le32(uint8_t *dst, uint32_t value)
{
	dst[0] = value & 0xff;
	dst[1] = (value >> 8) & 0xff;
	dst[2] = (value >> 16) & 0xff;
	dst[3] = (value >> 24) & 0xff;
}

static uint32_t
get_le32(const uint8_t *src)
{
	return (uint32_t)src[0]
		| ((uint32_t)src[1] << 8)
		| ((uint32_t)src[2] << 16)
		| ((uint32_t)src[3] << 24);
}

bool
ipc_protocol_encode(uint8_t *buf, size_t cap, const char *payload,
	size_t payload_len, size_t *out_len)
{
	if (!buf || !payload || !out_len || payload_len > UINT32_MAX ||
			payload_len > STELLAR_IPC_MAX_RESPONSE) {
		return false;
	}
	if (cap < STELLAR_IPC_HEADER_SIZE ||
			payload_len > cap - STELLAR_IPC_HEADER_SIZE) {
		return false;
	}
	put_le32(buf, STELLAR_IPC_MAGIC);
	put_le32(buf + 4, (uint32_t)payload_len);
	memcpy(buf + STELLAR_IPC_HEADER_SIZE, payload, payload_len);
	*out_len = STELLAR_IPC_HEADER_SIZE + payload_len;
	return true;
}

bool
ipc_protocol_decode(const uint8_t *buf, size_t len,
	const char **payload, size_t *payload_len)
{
	if (!buf || !payload || !payload_len ||
			len < STELLAR_IPC_HEADER_SIZE) {
		return false;
	}
	if (get_le32(buf) != STELLAR_IPC_MAGIC) {
		return false;
	}
	uint32_t declared = get_le32(buf + 4);
	if (declared > len - STELLAR_IPC_HEADER_SIZE ||
			declared > STELLAR_IPC_MAX_RESPONSE) {
		return false;
	}
	*payload = (const char *)buf + STELLAR_IPC_HEADER_SIZE;
	*payload_len = declared;
	return true;
}

int
ipc_protocol_tokenize(char *text, char *tokens[], int max_tokens)
{
	if (!text || !tokens || max_tokens < 1) {
		return -1;
	}
	int nr = 0;
	char *p = text;
	while (*p) {
		while (*p == ' ') {
			*p++ = '\0';
		}
		if (!*p) {
			break;
		}
		if (nr >= max_tokens) {
			return -1;
		}
		tokens[nr++] = p;
		while (*p && *p != ' ') {
			if (iscntrl((unsigned char)*p)) {
				return -1; /* tabs/newlines are not allowed */
			}
			p++;
		}
	}
	return nr;
}

void
ipc_json_init(struct ipc_json *json, char *buf, size_t cap)
{
	json->data = buf;
	json->len = 0;
	json->cap = cap;
	if (cap > 0) {
		buf[0] = '\0';
	}
}

bool
ipc_json_add(struct ipc_json *json, const char *fmt, ...)
{
	if (!json->data || json->cap == 0) {
		return false;
	}
	va_list ap;
	va_start(ap, fmt);
	int written = vsnprintf(json->data + json->len, json->cap - json->len,
		fmt, ap);
	va_end(ap);
	if (written < 0 || (size_t)written >= json->cap - json->len) {
		return false;
	}
	json->len += (size_t)written;
	return true;
}

bool
ipc_json_string(struct ipc_json *json, const char *value)
{
	if (!value || !ipc_json_add(json, "\"")) {
		return false;
	}
	for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
		char escaped[7];
		const char *s = NULL;
		switch (*p) {
		case '"':  s = "\\\""; break;
		case '\\': s = "\\\\"; break;
		case '\b': s = "\\b";  break;
		case '\f': s = "\\f";  break;
		case '\n': s = "\\n";  break;
		case '\r': s = "\\r";  break;
		case '\t': s = "\\t";  break;
		default:
			if (*p < 0x20) {
				snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
				s = escaped;
			} else {
				if (!ipc_json_add(json, "%c", (char)*p)) {
					return false;
				}
			}
			break;
		}
		if (s && !ipc_json_add(json, "%s", s)) {
			return false;
		}
	}
	return ipc_json_add(json, "\"");
}

bool
ipc_response_ok(char *buf, size_t cap, const char *json)
{
	int written = snprintf(buf, cap, "OK %s", json ? json : "null");
	return written >= 0 && (size_t)written < cap;
}

bool
ipc_response_error(char *buf, size_t cap, int code, const char *message)
{
	int written = snprintf(buf, cap, "ERR %d %s", code,
		message ? message : "error");
	return written >= 0 && (size_t)written < cap;
}

bool
ipc_response_event(char *buf, size_t cap, const char *json)
{
	int written = snprintf(buf, cap, "EVENT %s", json ? json : "null");
	return written >= 0 && (size_t)written < cap;
}
