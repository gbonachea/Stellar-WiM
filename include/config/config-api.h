/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_CONFIG_API_H
#define STELLAR_CONFIG_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ipc/protocol.h"

/*
 * Stellar Configuration API.
 *
 * Every value exposed here has an explicit descriptor (type, default, bounds
 * and whether it can be applied without restarting the compositor). The
 * compositor is the only writer of rc.xml: changes are validated on the main
 * thread, normalized, written through a temporary file + rename by a single
 * worker thread, and then applied to live state on the main thread.
 */
struct config_value {
	char raw[128];   /* canonical value, safe to place in JSON as a string */
	char xml[128];   /* representation written to rc.xml */
	char json[128];  /* representation written to JSON (quoted when needed) */
};

enum config_async_status {
	CONFIG_ASYNC_OK = 0,
	CONFIG_ASYNC_INVALID_ARGUMENT,
	CONFIG_ASYNC_NOT_FOUND,
	CONFIG_ASYNC_INTERNAL,
};

struct config_async_result {
	enum config_async_status status;
	char message[256];
	char json[STELLAR_IPC_MAX_RESPONSE];
	/* Set when a full reconfigure is still required after the write. */
	bool reload;
};

typedef void (*config_async_callback)(const struct config_async_result *result,
	void *data);

/* Start/stop the serialized configuration worker. Call on the main thread. */
bool config_async_init(void);
void config_async_finish(void);
bool config_async_defer_reload_if_busy(void);
bool config_async_has_capacity(char *err, size_t err_cap);

/*
 * Enqueue a reload barrier. Any configuration jobs submitted before this call
 * are applied first; the reload itself is scheduled on the main thread once
 * the queue is drained.
 */
bool config_api_async_reload(char *err, size_t err_cap);

bool config_api_get_summary(struct ipc_json *out);
bool config_api_get_key(struct ipc_json *out, const char *key, char *err,
	size_t err_cap);
bool config_api_list(struct ipc_json *out);
bool config_api_status_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);

/*
 * Validation and argument copying happen synchronously. Disk work happens in
 * the worker; the callback runs on the main thread after live state and
 * notifications have been updated. A false return means the request was
 * rejected before being queued and no callback will be invoked.
 */
bool config_api_set_async(const char *key, const char *value, char *err,
	size_t err_cap, config_async_callback cb, void *cb_data);
bool config_api_reset_async(const char *key, char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);
bool config_api_reset_section_async(const char *section, char *err,
	size_t err_cap, config_async_callback cb, void *cb_data);

/* Safe management of structural rc.xml sections. */
bool config_api_keybindings_list_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);
bool config_api_keybindings_set_async(const char *key, const char *action,
	char *err, size_t err_cap, config_async_callback cb, void *cb_data);
bool config_api_keybindings_remove_async(const char *key, char *err,
	size_t err_cap, config_async_callback cb, void *cb_data);
bool config_api_keybindings_reset_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);

bool config_api_rules_list_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);
bool config_api_rules_set_async(const char *identifier, const char *property,
	const char *value, char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);
bool config_api_rules_remove_async(const char *identifier,
	const char *property, char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);
bool config_api_rules_reset_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data);

/* Current value of a key in the same format used by CONFIG GET <key>. */
bool config_api_current(const char *key, struct config_value *out);

/* Emit a `config` event to all subscribed IPC clients. */
void config_api_notify(const char *event, const char *key,
	const char *value_json, bool persisted);

#endif /* STELLAR_CONFIG_API_H */
