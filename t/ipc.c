// SPDX-License-Identifier: GPL-2.0-only
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
#include "ipc/protocol.h"

static void
test_frame_roundtrip(void **state)
{
	(void)state;
	unsigned char frame[128];
	size_t len = 0;
	assert_true(ipc_protocol_encode(frame, sizeof(frame), "VERSION", 7, &len));
	assert_int_equal(len, STELLAR_IPC_HEADER_SIZE + 7);

	const char *payload = NULL;
	size_t payload_len = 0;
	assert_true(ipc_protocol_decode(frame, len, &payload, &payload_len));
	assert_int_equal(payload_len, 7);
	assert_memory_equal(payload, "VERSION", 7);
}

static void
test_frame_rejects_bad_magic_and_truncation(void **state)
{
	(void)state;
	unsigned char frame[32] = {0};
	size_t len = 0;
	assert_true(ipc_protocol_encode(frame, sizeof(frame), "PING", 4, &len));

	frame[0] ^= 0xff;
	const char *payload;
	size_t payload_len;
	assert_false(ipc_protocol_decode(frame, len, &payload, &payload_len));
	frame[0] ^= 0xff;

	/* Declared payload longer than received data */
	frame[4] = 100;
	assert_false(ipc_protocol_decode(frame, len, &payload, &payload_len));

	/* Declared payload larger than the protocol cap */
	frame[4] = 0xff;
	frame[5] = 0xff;
	frame[6] = 0xff;
	frame[7] = 0x7f;
	assert_false(ipc_protocol_decode(frame, len, &payload, &payload_len));
}

static void
test_tokenize(void **state)
{
	(void)state;
	char request[] = "WINDOWS  ACTIVATE  42";
	char *tokens[STELLAR_IPC_MAX_TOKENS];
	int nr = ipc_protocol_tokenize(request, tokens,
		STELLAR_IPC_MAX_TOKENS);
	assert_int_equal(nr, 3);
	assert_string_equal(tokens[0], "WINDOWS");
	assert_string_equal(tokens[1], "ACTIVATE");
	assert_string_equal(tokens[2], "42");

	char invalid[] = "WINDOWS\tLIST";
	assert_int_equal(ipc_protocol_tokenize(invalid, tokens,
		STELLAR_IPC_MAX_TOKENS), -1);

	char too_many[STELLAR_IPC_MAX_TOKENS * 4 + 1];
	too_many[0] = '\0';
	for (int i = 0; i <= STELLAR_IPC_MAX_TOKENS; i++) {
		strcat(too_many, "a ");
	}
	assert_int_equal(ipc_protocol_tokenize(too_many, tokens,
		STELLAR_IPC_MAX_TOKENS), -1);
}

static void
test_json_escape(void **state)
{
	(void)state;
	char buf[256];
	struct ipc_json json;
	ipc_json_init(&json, buf, sizeof(buf));
	assert_true(ipc_json_string(&json, "a\"b\\c\nd\t\x01"));
	assert_string_equal(buf, "\"a\\\"b\\\\c\\nd\\t\\u0001\"");
}

static void
test_json_bounded(void **state)
{
	(void)state;
	char buf[8];
	struct ipc_json json;
	ipc_json_init(&json, buf, sizeof(buf));
	assert_false(ipc_json_add(&json, "%s", "abcdefghijklmnop"));
}

static void
test_responses(void **state)
{
	(void)state;
	char buf[128];
	assert_true(ipc_response_ok(buf, sizeof(buf), "{\"ok\":true}"));
	assert_string_equal(buf, "OK {\"ok\":true}");
	assert_true(ipc_response_error(buf, sizeof(buf), 3, "not found"));
	assert_string_equal(buf, "ERR 3 not found");
	assert_true(ipc_response_event(buf, sizeof(buf), "{\"type\":\"view\"}"));
	assert_string_equal(buf, "EVENT {\"type\":\"view\"}");
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_frame_roundtrip),
		cmocka_unit_test(test_frame_rejects_bad_magic_and_truncation),
		cmocka_unit_test(test_tokenize),
		cmocka_unit_test(test_json_escape),
		cmocka_unit_test(test_json_bounded),
		cmocka_unit_test(test_responses),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
