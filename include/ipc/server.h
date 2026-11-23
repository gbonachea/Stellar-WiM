/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef STELLAR_IPC_SERVER_H
#define STELLAR_IPC_SERVER_H

#include <stdbool.h>
#include <stdint.h>

struct view;
struct workspace;

/* Lifecycle; called from server_start()/server_finish()/SIGHUP reload. */
void ipc_init(void);
void ipc_finish(void);
void ipc_reload(void);

/* True when the IPC server is currently listening. */
bool ipc_is_running(void);

/* Path of the Unix socket, or NULL when IPC is disabled. */
const char *ipc_socket_path(void);

/* State-change notifications. Cheap no-ops without subscribers. */
void ipc_notify_view(const char *event, struct view *view);
void ipc_notify_workspace(struct workspace *workspace);
void ipc_notify_tabs(const char *event, uint64_t group_id, uint64_t view_id);
void ipc_notify_tiling(const char *event, int managed);
void ipc_notify_profile(const char *event, const char *profile);
void ipc_notify_config(const char *event, const char *key,
	const char *value_json, bool persisted);

#endif /* STELLAR_IPC_SERVER_H */
