// SPDX-License-Identifier: GPL-2.0-only
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "config/rcxml.h"
#include "effects.h"
#include "ipc/server.h"
#include "labwc.h"
#include "profile.h"
#include "ssd-internal.h"
#include "tabs.h"
#include "tiling.h"
#include "view.h"

const char *
stellar_profile_name(enum stellar_profile profile)
{
	switch (profile) {
	case STELLAR_PROFILE_MINIMAL:
		return "minimal";
	case STELLAR_PROFILE_ENHANCED:
		return "enhanced";
	case STELLAR_PROFILE_CUSTOM:
	default:
		return "custom";
	}
}

bool
stellar_profile_parse(const char *name, enum stellar_profile *out)
{
	if (!name || !out) {
		return false;
	}
	while (isspace((unsigned char)*name)) {
		name++;
	}
	size_t len = strlen(name);
	while (len > 0 && isspace((unsigned char)name[len - 1])) {
		len--;
	}
	if (len == 0) {
		return false;
	}

	if (len == 7 && !strncasecmp(name, "minimal", len)) {
		*out = STELLAR_PROFILE_MINIMAL;
	} else if (len == 8 && !strncasecmp(name, "enhanced", len)) {
		*out = STELLAR_PROFILE_ENHANCED;
	} else if (len == 6 && !strncasecmp(name, "custom", len)) {
		*out = STELLAR_PROFILE_CUSTOM;
	} else {
		return false;
	}
	return true;
}

enum stellar_profile
stellar_profile_current(void)
{
	return rc.stellar.profile;
}

static void
apply_preset(enum stellar_profile profile)
{
	switch (profile) {
	case STELLAR_PROFILE_MINIMAL:
		rc.stellar.tabs_enabled = false;
		rc.stellar.tiling_enabled = false;
		/*
		 * Minimal is the low-resource profile: every optional visual
		 * effect is disabled, including the ones enabled by default.
		 */
		rc.stellar.animations_enabled = false;
		rc.stellar.opacity_enabled = false;
		rc.shadows_enabled = false;
		break;
	case STELLAR_PROFILE_ENHANCED:
		rc.stellar.tabs_enabled = true;
		rc.stellar.tiling_enabled = true;
		/* Enhanced restores the full visual experience. */
		rc.stellar.animations_enabled = true;
		rc.stellar.opacity_enabled = true;
		if (rc.stellar.opacity_level <= 0.0f) {
			rc.stellar.opacity_level = 0.9f;
		}
		rc.shadows_enabled = true;
		break;
	case STELLAR_PROFILE_CUSTOM:
	default:
		/* Individual flags from rc.xml are authoritative. */
		break;
	}
}

static void
reconcile_shadows(void)
{
	/*
	 * Shadow subtrees are built when a view gets its decorations; rebuild
	 * them when a profile re-enables shadows after they were skipped.
	 */
	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		struct ssd *ssd = view->ssd;
		if (!ssd || !ssd->shadow.tree) {
			continue;
		}
		if (rc.shadows_enabled && !ssd->shadow.subtrees[0].tree
				&& !ssd->shadow.subtrees[1].tree) {
			ssd_shadow_destroy(ssd);
			ssd_shadow_create(ssd);
		} else {
			ssd_shadow_update(ssd);
		}
	}
}

static void
reconcile_safe_state(void)
{
	/*
	 * These are the only transitions a profile performs at runtime.
	 * Renderer, adaptive sync, XWayland and protocol setup are untouched.
	 */
	if (!rc.stellar.tabs_enabled) {
		tabs_destroy_all();
		struct view *view;
		wl_list_for_each(view, &server.views, link) {
			view_update_visibility(view);
		}
	}
	effects_reconcile();
	reconcile_shadows();
	stellar_tiling_reapply();
}

void
stellar_profile_reconcile(void)
{
	apply_preset(rc.stellar.profile);
	reconcile_safe_state();
}

bool
stellar_profile_set(enum stellar_profile profile)
{
	if (profile == STELLAR_PROFILE_CUSTOM) {
		/* Use individual bindings/actions for fine-grained overrides. */
		return false;
	}

	enum stellar_profile old_profile = rc.stellar.profile;
	bool old_tabs = rc.stellar.tabs_enabled;
	bool old_tiling = rc.stellar.tiling_enabled;
	bool old_animations = rc.stellar.animations_enabled;
	bool old_opacity = rc.stellar.opacity_enabled;
	bool old_shadows = rc.shadows_enabled;

	rc.stellar.profile = profile;
	apply_preset(profile);
	reconcile_safe_state();

	if (old_profile != profile || old_tabs != rc.stellar.tabs_enabled
			|| old_tiling != rc.stellar.tiling_enabled
			|| old_animations != rc.stellar.animations_enabled
			|| old_opacity != rc.stellar.opacity_enabled
			|| old_shadows != rc.shadows_enabled) {
		wlr_log(WLR_INFO, "Stellar profile: %s",
			stellar_profile_name(profile));
		ipc_notify_profile("switch", stellar_profile_name(profile));
	}
	return true;
}
