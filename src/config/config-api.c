// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "config/config-api.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include "config/rcxml.h"
#include "ipc/protocol.h"
#include "ipc/server.h"
#include "labwc.h"
#include "profile.h"
#include "tiling.h"

enum cfg_type {
	CFG_BOOL,
	CFG_INT,
	CFG_FLOAT,
	CFG_PROFILE,
	CFG_TILING_MODE,
	CFG_STRING,
};

enum cfg_apply {
	CFG_LIVE,
	CFG_RELOAD,
};

struct cfg_key {
	const char *name;
	const char *section;
	enum cfg_type type;
	const char *def;
	const char *low;
	const char *high;
	enum cfg_apply apply;
	const char *xml_path;
	bool attribute;
};

/*
 * The descriptors are also what CONFIG LIST and Stellar Settings/Panel
 * consume, so bounds here are authoritative and shared with validation.
 */
static const struct cfg_key keys[] = {
	{ "effects.animations", "effects", CFG_BOOL, "true", NULL, NULL,
		CFG_LIVE, "stellar-wim/effects/animations", true },
	{ "effects.opacity", "effects", CFG_BOOL, "true", NULL, NULL,
		CFG_LIVE, "stellar-wim/effects/opacity", true },
	{ "effects.opacity-level", "effects", CFG_FLOAT, "0.90", "0.10", "1.00",
		CFG_LIVE, "stellar-wim/effects/opacity-level", true },
	{ "effects.shadows", "effects", CFG_BOOL, "true", NULL, NULL,
		CFG_LIVE, "theme/dropShadows", false },
	{ "theme.corner-radius", "theme", CFG_INT, "14", "0", "32",
		CFG_RELOAD, "theme/cornerRadius", false },
	{ "theme.name", "theme", CFG_STRING, "", NULL, NULL,
		CFG_RELOAD, "theme/name", false },
	{ "theme.show-title", "theme", CFG_BOOL, "true", NULL, NULL,
		CFG_RELOAD, "theme/titlebar/showTitle", false },
	{ "theme.keep-border", "theme", CFG_BOOL, "true", NULL, NULL,
		CFG_RELOAD, "theme/keepBorder", false },
	{ "profile", "profile", CFG_PROFILE, "custom", NULL, NULL,
		CFG_LIVE, "stellar-wim/profile", false },
	{ "tabs.enabled", "tabs", CFG_BOOL, "false", NULL, NULL,
		CFG_LIVE, "stellar-wim/tabs/enabled", true },
	{ "tiling.enabled", "tiling", CFG_BOOL, "false", NULL, NULL,
		CFG_LIVE, "stellar-wim/tiling/enabled", true },
	{ "tiling.mode", "tiling", CFG_TILING_MODE, "master-stack", NULL, NULL,
		CFG_LIVE, "stellar-wim/tiling/mode", false },
	{ "tiling.master-ratio", "tiling", CFG_FLOAT, "0.60", "0.20", "0.80",
		CFG_LIVE, "stellar-wim/tiling/master-ratio", false },
	{ "tiling.columns", "tiling", CFG_INT, "2", "1", "64",
		CFG_LIVE, "stellar-wim/tiling/columns", false },
	{ "ipc.enabled", "ipc", CFG_BOOL, "true", NULL, NULL,
		CFG_RELOAD, "stellar-wim/ipc/enabled", true },
};

static const struct cfg_key *
find_key(const char *name)
{
	if (!name) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (!strcasecmp(name, keys[i].name)) {
			return &keys[i];
		}
	}
	return NULL;
}

static const char *
type_name(enum cfg_type type)
{
	switch (type) {
	case CFG_BOOL: return "boolean";
	case CFG_INT: return "integer";
	case CFG_FLOAT: return "number";
	case CFG_PROFILE: return "profile";
	case CFG_TILING_MODE: return "tiling-mode";
	case CFG_STRING: return "string";
	}
	return "string";
}

static const char *
apply_name(enum cfg_apply apply)
{
	return apply == CFG_LIVE ? "live" : "reload";
}

static void
set_error(char *err, size_t cap, const char *fmt, ...)
{
	if (!err || cap == 0) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, cap, fmt, ap);
	va_end(ap);
}

/*
 * IPC values always use '.' as decimal separator, independently of the
 * compositor locale (which may use ',' and break strtof()).
 */
static bool
parse_float_c(const char *text, float *out)
{
	char *end = NULL;
	errno = 0;
	locale_t loc = newlocale(LC_NUMERIC_MASK, "C", NULL);
	if (!loc) {
		return false;
	}
	float value = strtof_l(text, &end, loc);
	int failed = errno || !end || *end || end == text || !isfinite(value);
	freelocale(loc);
	if (failed) {
		return false;
	}
	*out = value;
	return true;
}

static void
format_float(char *dst, size_t cap, float value)
{
	int scaled = (int)(value * 100.0f + 0.5f);
	snprintf(dst, cap, "%d.%02d", scaled / 100, scaled % 100);
}

static void
make_bool(struct config_value *out, bool value)
{
	snprintf(out->raw, sizeof(out->raw), "%s", value ? "true" : "false");
	snprintf(out->xml, sizeof(out->xml), "%s", value ? "yes" : "no");
	snprintf(out->json, sizeof(out->json), "%s", value ? "true" : "false");
}

static void
make_string(struct config_value *out, const char *value)
{
	snprintf(out->raw, sizeof(out->raw), "%s", value);
	snprintf(out->xml, sizeof(out->xml), "%s", value);
	snprintf(out->json, sizeof(out->json), "\"%s\"", value);
}

bool
config_api_current(const char *key, struct config_value *out)
{
	const struct cfg_key *k = find_key(key);
	if (!k || !out) {
		return false;
	}
	memset(out, 0, sizeof(*out));

	if (!strcasecmp(k->name, "effects.animations")) {
		make_bool(out, rc.stellar.animations_enabled);
	} else if (!strcasecmp(k->name, "effects.opacity")) {
		make_bool(out, rc.stellar.opacity_enabled);
	} else if (!strcasecmp(k->name, "effects.opacity-level")) {
		char value[64];
		format_float(value, sizeof(value), rc.stellar.opacity_level);
		make_string(out, value);
		snprintf(out->json, sizeof(out->json), "%s", value);
	} else if (!strcasecmp(k->name, "effects.shadows")) {
		make_bool(out, rc.shadows_enabled);
	} else if (!strcasecmp(k->name, "theme.corner-radius")) {
		char value[32];
		snprintf(value, sizeof(value), "%d", rc.corner_radius);
		make_string(out, value);
		snprintf(out->json, sizeof(out->json), "%s", value);
	} else if (!strcasecmp(k->name, "theme.name")) {
		make_string(out, rc.theme_name ? rc.theme_name : "");
	} else if (!strcasecmp(k->name, "theme.show-title")) {
		make_bool(out, rc.show_title);
	} else if (!strcasecmp(k->name, "theme.keep-border")) {
		make_bool(out, rc.ssd_keep_border);
	} else if (!strcasecmp(k->name, "profile")) {
		make_string(out, stellar_profile_name(rc.stellar.profile));
	} else if (!strcasecmp(k->name, "tabs.enabled")) {
		make_bool(out, rc.stellar.tabs_enabled);
	} else if (!strcasecmp(k->name, "tiling.enabled")) {
		make_bool(out, rc.stellar.tiling_enabled);
	} else if (!strcasecmp(k->name, "tiling.mode")) {
		make_string(out, stellar_tiling_mode_name(rc.stellar.tiling_mode));
	} else if (!strcasecmp(k->name, "tiling.master-ratio")) {
		float ratio = rc.stellar.tiling_master_ratio;
		if (!isfinite(ratio) || ratio < 0.2f || ratio > 0.8f) {
			ratio = 0.6f;
		}
		char value[64];
		format_float(value, sizeof(value), ratio);
		make_string(out, value);
		snprintf(out->json, sizeof(out->json), "%s", value);
	} else if (!strcasecmp(k->name, "tiling.columns")) {
		char value[32];
		snprintf(value, sizeof(value), "%d", rc.stellar.tiling_columns);
		make_string(out, value);
		snprintf(out->json, sizeof(out->json), "%s", value);
	} else if (!strcasecmp(k->name, "ipc.enabled")) {
		make_bool(out, rc.stellar.ipc_enabled);
	} else {
		return false;
	}
	return true;
}

static bool
parse_bool(const char *text, struct config_value *out)
{
	if (!strcasecmp(text, "true") || !strcasecmp(text, "yes") ||
			!strcasecmp(text, "on") || !strcmp(text, "1")) {
		make_bool(out, true);
		return true;
	}
	if (!strcasecmp(text, "false") || !strcasecmp(text, "no") ||
			!strcasecmp(text, "off") || !strcmp(text, "0")) {
		make_bool(out, false);
		return true;
	}
	return false;
}

static bool
parse_number(const struct cfg_key *k, const char *text,
	struct config_value *out)
{
	char *end = NULL;
	errno = 0;
	if (k->type == CFG_INT) {
		long value = strtol(text, &end, 10);
		if (errno || !end || *end || end == text) {
			return false;
		}
		long low = strtol(k->low, NULL, 10);
		long high = strtol(k->high, NULL, 10);
		if (value < low || value > high) {
			return false;
		}
		char value_str[32];
		snprintf(value_str, sizeof(value_str), "%ld", value);
		make_string(out, value_str);
		snprintf(out->json, sizeof(out->json), "%ld", value);
		return true;
	}
	float value;
	if (!parse_float_c(text, &value)) {
		return false;
	}
	float low, high;
	if (!parse_float_c(k->low, &low) || !parse_float_c(k->high, &high)) {
		return false;
	}
	if (value < low || value > high) {
		return false;
	}
	char value_str[64];
	format_float(value_str, sizeof(value_str), value);
	make_string(out, value_str);
	snprintf(out->json, sizeof(out->json), "%s", value_str);
	return true;
}

static bool
valid_theme_name(const char *text)
{
	if (!*text || strlen(text) > 96) {
		return false;
	}
	/* A single IPC token: spaces would be split by the wire protocol. */
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		if (isalnum(*p) || *p == '-' || *p == '_' ||
				*p == '.' || *p == '+' || *p == '(' || *p == ')') {
			continue;
		}
		return false;
	}
	return true;
}

static bool
parse_value(const struct cfg_key *k, const char *text,
	struct config_value *out)
{
	if (!text || strlen(text) >= sizeof(out->raw)) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	switch (k->type) {
	case CFG_BOOL:
		return parse_bool(text, out);
	case CFG_INT:
	case CFG_FLOAT:
		return parse_number(k, text, out);
	case CFG_PROFILE: {
		enum stellar_profile profile;
		if (!stellar_profile_parse(text, &profile)) {
			return false;
		}
		make_string(out, stellar_profile_name(profile));
		return true;
	}
	case CFG_TILING_MODE: {
		enum stellar_tiling_mode mode;
		if (!stellar_tiling_parse_mode(text, &mode)) {
			return false;
		}
		make_string(out, stellar_tiling_mode_name(mode));
		return true;
	}
	case CFG_STRING:
		if (!valid_theme_name(text)) {
			return false;
		}
		make_string(out, text);
		return true;
	}
	return false;
}

static void
apply_value(const struct cfg_key *k, const struct config_value *v)
{
	if (!strcasecmp(k->name, "profile")) {
		enum stellar_profile profile;
		if (stellar_profile_parse(v->raw, &profile) &&
				profile != STELLAR_PROFILE_CUSTOM) {
			stellar_profile_set(profile);
		} else {
			rc.stellar.profile = STELLAR_PROFILE_CUSTOM;
			stellar_profile_reconcile();
		}
		return;
	}

	/* Individual entries always switch the session to custom. */
	rc.stellar.profile = STELLAR_PROFILE_CUSTOM;

	bool value = !strcasecmp(v->xml, "yes");
	if (!strcasecmp(k->name, "effects.animations")) {
		rc.stellar.animations_enabled = value;
	} else if (!strcasecmp(k->name, "effects.opacity")) {
		rc.stellar.opacity_enabled = value;
	} else if (!strcasecmp(k->name, "effects.opacity-level")) {
		float level;
		if (parse_float_c(v->raw, &level)) {
			rc.stellar.opacity_level = level;
		}
	} else if (!strcasecmp(k->name, "effects.shadows")) {
		rc.shadows_enabled = value;
	} else if (!strcasecmp(k->name, "theme.corner-radius")) {
		rc.corner_radius = atoi(v->raw);
	} else if (!strcasecmp(k->name, "theme.name")) {
		/* Applied by the scheduled configuration reload. */
	} else if (!strcasecmp(k->name, "theme.show-title")) {
		rc.show_title = value;
	} else if (!strcasecmp(k->name, "theme.keep-border")) {
		rc.ssd_keep_border = value;
	} else if (!strcasecmp(k->name, "tabs.enabled")) {
		rc.stellar.tabs_enabled = value;
	} else if (!strcasecmp(k->name, "tiling.enabled")) {
		rc.stellar.tiling_enabled = value;
	} else if (!strcasecmp(k->name, "tiling.mode")) {
		stellar_tiling_parse_mode(v->raw, &rc.stellar.tiling_mode);
	} else if (!strcasecmp(k->name, "tiling.master-ratio")) {
		float ratio;
		if (parse_float_c(v->raw, &ratio)) {
			rc.stellar.tiling_master_ratio = ratio;
		}
	} else if (!strcasecmp(k->name, "tiling.columns")) {
		rc.stellar.tiling_columns = atoi(v->raw);
	} else if (!strcasecmp(k->name, "ipc.enabled")) {
		rc.stellar.ipc_enabled = value;
	}

	if (k->apply == CFG_LIVE) {
		stellar_profile_reconcile();
	}
}

static xmlNode *
find_child(xmlNode *parent, const char *name)
{
	for (xmlNode *child = parent->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE && child->name &&
				!strcasecmp((const char *)child->name, name)) {
			return child;
		}
	}
	return NULL;
}

static xmlNode *
ensure_child(xmlNode *parent, const char *name, int depth)
{
	xmlNode *child = find_child(parent, name);
	if (child) {
		return child;
	}
	/* Keep hand-written rc.xml files readable. */
	xmlNode *indent = xmlNewText(BAD_CAST "\n  ");
	for (int i = 1; i < depth; i++) {
		xmlNodeAddContent(indent, BAD_CAST "  ");
	}
	xmlAddChild(parent, indent);
	child = xmlNewChild(parent, NULL, BAD_CAST name, NULL);
	xmlNode *tail = xmlNewText(BAD_CAST "\n");
	for (int i = 0; i < depth - 1; i++) {
		xmlNodeAddContent(tail, BAD_CAST "  ");
	}
	xmlAddChild(parent, tail);
	return child;
}

static bool
save_xml_atomic(xmlDoc *doc, const char *path)
{
	char tmp[PATH_MAX];
	int len = snprintf(tmp, sizeof(tmp), "%s.stellar-tmp-XXXXXX", path);
	if (len <= 0 || (size_t)len >= sizeof(tmp)) {
		return false;
	}
	struct stat st = {0};
	mode_t mode = 0644;
	if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
		mode = st.st_mode & 07777;
	}

	/* Serialize in memory, then create the replacement with O_EXCL. */
	xmlChar *mem = NULL;
	int size = 0;
	xmlDocDumpFormatMemory(doc, &mem, &size, 1);
	if (size <= 0 || !mem) {
		wlr_log(WLR_ERROR, "cannot serialize %s", path);
		return false;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		xmlFree(mem);
		wlr_log_errno(WLR_ERROR, "cannot create temporary file for %s", path);
		return false;
	}
	bool ok = true;
	size_t off = 0;
	while (off < (size_t)size) {
		ssize_t n = write(fd, mem + off, (size_t)size - off);
		if (n > 0) {
			off += (size_t)n;
		} else if (n < 0 && errno == EINTR) {
			continue;
		} else {
			ok = false;
			break;
		}
	}
	xmlFree(mem);
	if (ok && fchmod(fd, mode) < 0) {
		ok = false;
	}
	if (ok && fsync(fd) < 0) {
		ok = false;
	}
	if (close(fd) < 0) {
		ok = false;
	}
	if (!ok) {
		unlink(tmp);
		wlr_log_errno(WLR_ERROR, "cannot write %s", tmp);
		return false;
	}
	if (rename(tmp, path) < 0) {
		unlink(tmp);
		wlr_log_errno(WLR_ERROR, "cannot replace %s", path);
		return false;
	}
	/* Once renamed, failure to sync the directory cannot be rolled back. */
	char directory[PATH_MAX];
	snprintf(directory, sizeof(directory), "%s", path);
	char *slash = strrchr(directory, '/');
	if (slash) {
		if (slash == directory) {
			slash[1] = '\0';
		} else {
			*slash = '\0';
		}
	} else {
		snprintf(directory, sizeof(directory), ".");
	}
	int dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0) {
		wlr_log_errno(WLR_ERROR, "cannot open configuration directory for sync");
	} else {
		if (fsync(dirfd) < 0) {
			wlr_log_errno(WLR_ERROR, "cannot sync configuration directory");
		}
		close(dirfd);
	}
	return true;
}

static bool
mutate_xml_value(xmlDoc *doc, const struct cfg_key *k,
	const char *value, bool remove)
{
	xmlNode *root = xmlDocGetRootElement(doc);
	if (!root || !root->name || strcasecmp((const char *)root->name, "labwc_config")) {
		return false;
	}

	char components[8][64];
	int nr = 0;
	char path_copy[256];
	snprintf(path_copy, sizeof(path_copy), "%s", k->xml_path);
	char *cursor = NULL;
	for (char *p = strtok_r(path_copy, "/", &cursor); p && nr < 8;
			p = strtok_r(NULL, "/", &cursor)) {
		snprintf(components[nr++], sizeof(components[0]), "%s", p);
	}
	if (nr < 1) {
		return false;
	}

	xmlNode *parent = root;
	for (int i = 0; i < nr - 1; i++) {
		parent = ensure_child(parent, components[i], i + 1);
	}
	const char *leaf = components[nr - 1];

	if (k->attribute) {
		xmlAttr *attr = xmlHasProp(parent, BAD_CAST leaf);
		if (remove) {
			if (attr) {
				xmlRemoveProp(attr);
			}
		} else if (attr && attr->children) {
			xmlNodeSetContent(attr->children, BAD_CAST value);
		} else {
			/* Recreate an attribute whose text node is missing. */
			if (attr) {
				xmlRemoveProp(attr);
			}
			xmlNewProp(parent, BAD_CAST leaf, BAD_CAST value);
		}
		/* A child element with the same name would override the attribute. */
		xmlNode *shadow = find_child(parent, leaf);
		if (shadow) {
			xmlUnlinkNode(shadow);
			xmlFreeNode(shadow);
		}
	} else {
		xmlNode *node = find_child(parent, leaf);
		if (remove) {
			if (node) {
				xmlUnlinkNode(node);
				xmlFreeNode(node);
			}
		} else if (node) {
			xmlNodeSetContent(node, BAD_CAST value);
		} else {
			xmlNode *child = xmlNewChild(parent, NULL, BAD_CAST leaf,
				BAD_CAST value);
			if (child) {
				/* Keep the surrounding indentation tidy. */
				xmlNode *prev = child->prev;
				if (prev && prev->type == XML_TEXT_NODE) {
					xmlNodeSetContent(prev, BAD_CAST "\n  ");
				}
			}
		}
		xmlAttr *shadow = xmlHasProp(parent, BAD_CAST leaf);
		if (shadow) {
			xmlRemoveProp(shadow);
		}
	}
	return true;
}

static bool
config_file_path(char *buf, size_t cap)
{
	if (rc.config_file && rc.config_file[0]) {
		snprintf(buf, cap, "%s", rc.config_file);
		return true;
	}
	const char *config_home = getenv("XDG_CONFIG_HOME");
	if (config_home && config_home[0]) {
		snprintf(buf, cap, "%s/stellar-wim/rc.xml", config_home);
	} else {
		const char *home = getenv("HOME");
		if (!home || !home[0]) {
			return false;
		}
		snprintf(buf, cap, "%s/.config/stellar-wim/rc.xml", home);
	}
	return strlen(buf) > 0 && strlen(buf) < cap;
}

/*
 * A reset uses the descriptor default. Empty-string defaults are removed from
 * rc.xml instead of being written as an empty element.
 */
static bool
default_value(const struct cfg_key *k, struct config_value *value,
	bool *remove)
{
	memset(value, 0, sizeof(*value));
	if (k->def[0] == '\0') {
		snprintf(value->json, sizeof(value->json), "\"\"");
		if (remove) {
			*remove = true;
		}
		return true;
	}
	if (!parse_value(k, k->def, value)) {
		return false;
	}
	if (remove) {
		*remove = false;
	}
	return true;
}

static bool
section_matches(const char *section, const struct cfg_key *k)
{
	return section && (!strcasecmp(section, "all") ||
		!strcasecmp(section, k->section));
}

static bool
valid_section(const char *section)
{
	if (!section || !*section) {
		return false;
	}
	if (!strcasecmp(section, "all")) {
		return true;
	}
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (!strcasecmp(section, keys[i].section)) {
			return true;
		}
	}
	return false;
}

bool
config_api_get_summary(struct ipc_json *out)
{
	struct config_value value;
	/* Legacy shorthand fields are kept for existing 0.8.x clients. */
	bool ok = ipc_json_add(out,
		"{\"animations\":%s,\"opacity\":%s,\"shadows\":%s,"
		"\"tabs\":%s,\"tiling\":%s,\"corner_radius\":%d,\"profile\":",
		rc.stellar.animations_enabled ? "true" : "false",
		rc.stellar.opacity_enabled ? "true" : "false",
		rc.shadows_enabled ? "true" : "false",
		rc.stellar.tabs_enabled ? "true" : "false",
		rc.stellar.tiling_enabled ? "true" : "false",
		rc.corner_radius)
		&& ipc_json_string(out, stellar_profile_name(rc.stellar.profile))
		&& ipc_json_add(out, ",\"tiling_mode\":")
		&& ipc_json_string(out,
			stellar_tiling_mode_name(rc.stellar.tiling_mode));
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		/* Already emitted as the legacy shorthand above. */
		if (!strcasecmp(keys[i].name, "profile")) {
			continue;
		}
		if (!config_api_current(keys[i].name, &value)) {
			return false;
		}
		ok = ok && ipc_json_add(out, ",\"%s\":", keys[i].name)
			&& ipc_json_add(out, "%s", value.json);
	}
	return ok && ipc_json_add(out, "}");
}

bool
config_api_get_key(struct ipc_json *out, const char *key, char *err,
	size_t err_cap)
{
	const struct cfg_key *k = find_key(key);
	if (!k) {
		set_error(err, err_cap, "unknown configuration key");
		return false;
	}
	struct config_value value;
	if (!config_api_current(k->name, &value)) {
		set_error(err, err_cap, "configuration value is unavailable");
		return false;
	}
	char def_json[128];
	if (k->type == CFG_BOOL || k->type == CFG_INT || k->type == CFG_FLOAT) {
		snprintf(def_json, sizeof(def_json), "%s", k->def);
	} else {
		snprintf(def_json, sizeof(def_json), "\"%s\"", k->def);
	}
	return ipc_json_add(out, "{\"key\":\"%s\",\"value\":%s,\"default\":%s,"
		"\"type\":\"%s\",\"applies\":\"%s\",\"persisted\":true}",
		k->name, value.json, def_json, type_name(k->type),
		apply_name(k->apply));
}

bool
config_api_list(struct ipc_json *out)
{
	bool ok = ipc_json_add(out, "[");
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const struct cfg_key *k = &keys[i];
		const char *def = k->def;
		char quoted[128];
		if (k->type == CFG_PROFILE || k->type == CFG_TILING_MODE ||
				k->type == CFG_STRING) {
			snprintf(quoted, sizeof(quoted), "\"%s\"", def);
			def = quoted;
		}
		ok = ok && ipc_json_add(out,
			"%s{\"key\":\"%s\",\"section\":\"%s\",\"type\":\"%s\","
			"\"default\":%s,\"applies\":\"%s\"",
			i ? "," : "", k->name, k->section, type_name(k->type),
			def, apply_name(k->apply));
		if (ok && k->low) {
			ok = ipc_json_add(out, ",\"minimum\":%s", k->low);
		}
		if (ok && k->high) {
			ok = ipc_json_add(out, ",\"maximum\":%s", k->high);
		}
		ok = ok && ipc_json_add(out, "}");
	}
	return ok && ipc_json_add(out, "]");
}

static bool
build_status(struct ipc_json *out, const char *path, bool dirty)
{
	bool writable = false;
	struct stat st = {0};
	if (stat(path, &st) == 0) {
		writable = access(path, W_OK) == 0;
	} else {
		char dir[PATH_MAX];
		snprintf(dir, sizeof(dir), "%s", path);
		char *slash = strrchr(dir, '/');
		if (slash) {
			*slash = '\0';
			writable = access(dir, W_OK) == 0;
		}
	}
	return ipc_json_add(out, "{\"config_file\":") && ipc_json_string(out, path)
		&& ipc_json_add(out, ",\"writable\":%s,"
		"\"dirty\":%s,\"persistent\":true}", writable ? "true" : "false",
		dirty ? "true" : "false");
}

/*
 * Structural settings (keybinds and window rules) need a full reconfigure, so
 * they are written atomically and the caller schedules an idle reload. Only
 * actions and properties from these tables are accepted: the IPC API never
 * accepts an arbitrary command line or XML fragment.
 */
static const char *const safe_actions[] = {
	"None", "Close", "NextWindow", "PreviousWindow",
	"NextWindowImmediate", "PreviousWindowImmediate",
	"ToggleMaximize", "Maximize", "UnMaximize", "ToggleFullscreen",
	"ToggleDecorations", "ToggleAlwaysOnTop", "ToggleAlwaysOnBottom",
	"ToggleOmnipresent", "Iconify", "Raise", "Lower",
	"ToggleShowDesktop", "ToggleKeybinds", "ToggleMagnify",
	"ZoomIn", "ZoomOut", "StellarTabGroup", "StellarTabNext",
	"StellarTabPrevious", "StellarTabDetach", "StellarTilingToggle",
	"StellarTilingReapply", "StellarProfileMinimal",
	"StellarProfileEnhanced",
};

static const char *
canonical_action(const char *text)
{
	for (size_t i = 0; i < sizeof(safe_actions) / sizeof(safe_actions[0]); i++) {
		if (!strcasecmp(text, safe_actions[i])) {
			return safe_actions[i];
		}
	}
	return NULL;
}

static bool
safe_identifier(const char *text, size_t max_len, bool allow_wildcards)
{
	if (!text || !*text || strlen(text) > max_len) {
		return false;
	}
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' ||
				(allow_wildcards && (*p == '*' || *p == '?'))) {
			continue;
		}
		return false;
	}
	return true;
}

static bool
is_rule_property(const char *property)
{
	static const char *const properties[] = {
		"serverDecoration", "skipTaskbar", "skipWindowSwitcher",
		"ignoreFocusRequest", "ignoreConfigureRequest",
		"allowAlwaysOnTop", "fixedPosition", "iconPriority",
		"stellarTiling", "stellarTilingMode",
	};
	if (!property) {
		return false;
	}
	for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
		if (!strcasecmp(property, properties[i])) {
			return true;
		}
	}
	return false;
}

static const char *
canonical_rule_property(const char *property)
{
	static const char *const properties[] = {
		"serverDecoration", "skipTaskbar", "skipWindowSwitcher",
		"ignoreFocusRequest", "ignoreConfigureRequest",
		"allowAlwaysOnTop", "fixedPosition", "iconPriority",
		"stellarTiling", "stellarTilingMode",
	};
	if (!property) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
		if (!strcasecmp(property, properties[i])) {
			return properties[i];
		}
	}
	return NULL;
}

static const char *
canonical_rule_value(const char *property, const char *value)
{
	static const char *const tri_state[] = { "yes", "no", "default" };
	static const char *const icon_priority[] = { "client", "server" };
	static const char *const tiling_policy[] = { "floating", "managed" };

	const char *const *list = NULL;
	size_t count = 0;
	if (!value || !is_rule_property(property)) {
		return NULL;
	}
	if (!strcasecmp(property, "serverDecoration") ||
			!strcasecmp(property, "skipTaskbar") ||
			!strcasecmp(property, "skipWindowSwitcher") ||
			!strcasecmp(property, "ignoreFocusRequest") ||
			!strcasecmp(property, "ignoreConfigureRequest") ||
			!strcasecmp(property, "allowAlwaysOnTop") ||
			!strcasecmp(property, "fixedPosition")) {
		list = tri_state;
		count = sizeof(tri_state) / sizeof(tri_state[0]);
	} else if (!strcasecmp(property, "iconPriority")) {
		list = icon_priority;
		count = sizeof(icon_priority) / sizeof(icon_priority[0]);
	} else if (!strcasecmp(property, "stellarTiling")) {
		list = tiling_policy;
		count = sizeof(tiling_policy) / sizeof(tiling_policy[0]);
	} else if (!strcasecmp(property, "stellarTilingMode")) {
		enum stellar_tiling_mode mode;
		if (stellar_tiling_parse_mode(value, &mode)) {
			return stellar_tiling_mode_name(mode);
		}
		return NULL;
	}
	for (size_t i = 0; i < count; i++) {
		if (!strcasecmp(value, list[i])) {
			return list[i];
		}
	}
	return NULL;
}

static bool
node_prop(xmlNode *node, const char *name, char *buf, size_t cap)
{
	if (!node) {
		return false;
	}
	xmlChar *value = xmlGetProp(node, BAD_CAST name);
	if (!value) {
		return false;
	}
	snprintf(buf, cap, "%s", (const char *)value);
	xmlFree(value);
	return true;
}

static xmlNode *
find_by_prop(xmlNode *parent, const char *element, const char *prop,
	const char *value)
{
	for (xmlNode *cur = parent->children; cur; cur = cur->next) {
		if (cur->type != XML_ELEMENT_NODE || !cur->name ||
				strcasecmp((const char *)cur->name, element)) {
			continue;
		}
		char current[128];
		if (node_prop(cur, prop, current, sizeof(current)) &&
				!strcasecmp(current, value)) {
			return cur;
		}
	}
	return NULL;
}

static void
remove_elements_named(xmlNode *parent, const char *name)
{
	xmlNode *cur = parent->children;
	while (cur) {
		xmlNode *next = cur->next;
		if (cur->type == XML_ELEMENT_NODE && cur->name &&
				!strcasecmp((const char *)cur->name, name)) {
			xmlUnlinkNode(cur);
			xmlFreeNode(cur);
		}
		cur = next;
	}
}

static void
set_prop_no_shadow(xmlNode *node, const char *name, const char *value)
{
	xmlAttr *attr = xmlHasProp(node, BAD_CAST name);
	if (attr && attr->children) {
		xmlNodeSetContent(attr->children, BAD_CAST value);
	} else if (!attr) {
		xmlNewProp(node, BAD_CAST name, BAD_CAST value);
	}
	xmlNode *shadow = find_child(node, name);
	if (shadow) {
		xmlUnlinkNode(shadow);
		xmlFreeNode(shadow);
	}
}

static void
remove_prop_no_shadow(xmlNode *node, const char *name)
{
	xmlAttr *attr = xmlHasProp(node, BAD_CAST name);
	if (attr) {
		xmlRemoveProp(attr);
	}
	xmlNode *shadow = find_child(node, name);
	if (shadow) {
		xmlUnlinkNode(shadow);
		xmlFreeNode(shadow);
	}
}

static xmlNode *
append_element(xmlNode *parent, const char *name, const char *indent)
{
	xmlNode *text = xmlNewText(BAD_CAST indent);
	xmlAddChild(parent, text);
	xmlNode *child = xmlNewChild(parent, NULL, BAD_CAST name, NULL);
	xmlNode *tail = xmlNewText(BAD_CAST "\n  ");
	xmlAddChild(parent, tail);
	return child;
}

static bool
build_keybindings_list(xmlDoc *doc, struct ipc_json *out)
{
	xmlNode *keyboard = find_child(xmlDocGetRootElement(doc), "keyboard");
	bool ok = ipc_json_add(out, "[");
	bool first = true;
	if (keyboard) {
		for (xmlNode *cur = keyboard->children; ok && cur; cur = cur->next) {
			char key[128];
			if (cur->type != XML_ELEMENT_NODE || !cur->name ||
					strcasecmp((const char *)cur->name, "keybind") ||
					!node_prop(cur, "key", key, sizeof(key))) {
				continue;
			}
			ok = ipc_json_add(out, "%s{\"key\":", first ? "" : ",")
				&& ipc_json_string(out, key)
				&& ipc_json_add(out, ",\"actions\":[");
			bool first_action = true;
			for (xmlNode *a = cur->children; ok && a; a = a->next) {
				char action[128];
				if (a->type != XML_ELEMENT_NODE || !a->name ||
						strcasecmp((const char *)a->name, "action") ||
						!node_prop(a, "name", action, sizeof(action))) {
					continue;
				}
				ok = ipc_json_add(out, "%s", first_action ? "" : ",")
					&& ipc_json_string(out, action);
				first_action = false;
			}
			ok = ok && ipc_json_add(out, "]}");
			first = false;
		}
	}
	return ok && ipc_json_add(out, "]");
}

static bool
mutate_keybindings_set(xmlDoc *doc, const char *key, const char *canonical)
{
	xmlNode *root = xmlDocGetRootElement(doc);
	xmlNode *keyboard = find_child(root, "keyboard");
	if (!keyboard) {
		keyboard = ensure_child(root, "keyboard", 1);
	}
	xmlNode *keybind = find_by_prop(keyboard, "keybind", "key", key);
	if (!keybind) {
		keybind = append_element(keyboard, "keybind", "\n    ");
		set_prop_no_shadow(keybind, "key", key);
	}
	remove_elements_named(keybind, "action");
	xmlNode *node = append_element(keybind, "action", "\n      ");
	set_prop_no_shadow(node, "name", canonical);
	return true;
}

static bool
mutate_keybindings_remove(xmlDoc *doc, const char *key)
{
	xmlNode *keyboard = find_child(xmlDocGetRootElement(doc), "keyboard");
	xmlNode *keybind = keyboard
		? find_by_prop(keyboard, "keybind", "key", key) : NULL;
	if (!keybind) {
		return false;
	}
	xmlUnlinkNode(keybind);
	xmlFreeNode(keybind);
	return true;
}

static void
mutate_keybindings_reset(xmlDoc *doc)
{
	xmlNode *keyboard = find_child(xmlDocGetRootElement(doc), "keyboard");
	if (keyboard) {
		remove_elements_named(keyboard, "keybind");
	}
}

static bool
build_rules_list(xmlDoc *doc, struct ipc_json *out)
{
	xmlNode *rules = find_child(xmlDocGetRootElement(doc), "windowRules");
	bool ok = ipc_json_add(out, "[");
	bool first = true;
	if (rules) {
		for (xmlNode *cur = rules->children; ok && cur; cur = cur->next) {
			if (cur->type != XML_ELEMENT_NODE || !cur->name ||
					strcasecmp((const char *)cur->name, "windowRule")) {
				continue;
			}
			ok = ipc_json_add(out, "%s{\"attributes\":{", first ? "" : ",");
			bool first_attr = true;
			for (xmlAttr *attr = cur->properties; ok && attr;
					attr = attr->next) {
				xmlChar *value = xmlNodeListGetString(doc,
					attr->children, 1);
				ok = ipc_json_add(out, "%s", first_attr ? "" : ",")
					&& ipc_json_string(out,
						(const char *)attr->name)
					&& ipc_json_add(out, ":")
					&& ipc_json_string(out,
						value ? (const char *)value : "");
				xmlFree(value);
				first_attr = false;
			}
			ok = ok && ipc_json_add(out, "},\"actions\":[");
			bool first_action = true;
			for (xmlNode *a = cur->children; ok && a; a = a->next) {
				char action[128];
				if (a->type != XML_ELEMENT_NODE || !a->name ||
						strcasecmp((const char *)a->name, "action") ||
						!node_prop(a, "name", action, sizeof(action))) {
					continue;
				}
				ok = ipc_json_add(out, "%s", first_action ? "" : ",")
					&& ipc_json_string(out, action);
				first_action = false;
			}
			ok = ok && ipc_json_add(out, "]}");
			first = false;
		}
	}
	return ok && ipc_json_add(out, "]");
}

static void
mutate_rules_set(xmlDoc *doc, const char *identifier, const char *name,
	const char *canonical)
{
	xmlNode *root = xmlDocGetRootElement(doc);
	xmlNode *rules = find_child(root, "windowRules");
	if (!rules) {
		rules = ensure_child(root, "windowRules", 1);
	}
	xmlNode *rule = find_by_prop(rules, "windowRule", "identifier", identifier);
	if (!rule) {
		rule = append_element(rules, "windowRule", "\n    ");
		set_prop_no_shadow(rule, "identifier", identifier);
	}
	set_prop_no_shadow(rule, name, canonical);
}

static bool
mutate_rules_remove(xmlDoc *doc, const char *identifier, const char *name)
{
	xmlNode *rules = find_child(xmlDocGetRootElement(doc), "windowRules");
	xmlNode *rule = rules
		? find_by_prop(rules, "windowRule", "identifier", identifier) : NULL;
	if (!rule) {
		return false;
	}
	if (name) {
		remove_prop_no_shadow(rule, name);
	} else {
		xmlUnlinkNode(rule);
		xmlFreeNode(rule);
	}
	return true;
}

static void
mutate_rules_reset(xmlDoc *doc)
{
	xmlNode *rules = find_child(xmlDocGetRootElement(doc), "windowRules");
	if (rules) {
		remove_elements_named(rules, "windowRule");
	}
}

/*
 * Serialized configuration worker.
 *
 * Only this thread touches libxml2 documents and the filesystem for the
 * Configuration API. The main thread captures immutable arguments before
 * enqueueing, applies rc.* state after completion, and sends IPC replies.
 */
enum cfg_job_kind {
	CFG_JOB_STATUS,
	CFG_JOB_KEYBINDINGS_LIST,
	CFG_JOB_RULES_LIST,
	CFG_JOB_SET,
	CFG_JOB_RESET,
	CFG_JOB_RESET_SECTION,
	CFG_JOB_KEYBINDINGS_SET,
	CFG_JOB_KEYBINDINGS_REMOVE,
	CFG_JOB_KEYBINDINGS_RESET,
	CFG_JOB_RULES_SET,
	CFG_JOB_RULES_REMOVE,
	CFG_JOB_RULES_RESET,
	CFG_JOB_BARRIER,
};

struct cfg_job {
	struct wl_list link;
	enum cfg_job_kind kind;
	char path[PATH_MAX];
	char key[128];
	char value[128];
	char section[64];
	char action[128];
	char identifier[128];
	char property[128];
	const struct cfg_key *desc;
	struct config_value parsed;
	bool remove;
	bool dirty;
	struct config_async_result result;
	config_async_callback cb;
	void *cb_data;
};

static pthread_mutex_t cfg_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cfg_queue_cond = PTHREAD_COND_INITIALIZER;
static struct wl_list cfg_pending_jobs;
static struct wl_list cfg_completed_jobs;
static pthread_t cfg_worker_thread;
static bool cfg_worker_started;
static bool cfg_worker_stopping;
static bool cfg_finishing;
static int cfg_completion_fd = -1;
static struct wl_event_source *cfg_completion_source;
static size_t cfg_nr_pending;
/* Main-thread state; includes completions not yet applied to the compositor. */
#define CFG_MAX_PENDING 32
static bool cfg_reload_needed;

bool
config_async_has_capacity(char *err, size_t err_cap)
{
	if (cfg_nr_pending >= CFG_MAX_PENDING) {
		set_error(err, err_cap, "configuration queue is full; try again");
		return false;
	}
	return true;
}

static void
cfg_result_error(struct cfg_job *job, enum config_async_status status,
	const char *message)
{
	job->result.status = status;
	set_error(job->result.message, sizeof(job->result.message), "%s",
		message ? message : "configuration operation failed");
}

static struct cfg_job *
cfg_new_job(enum cfg_job_kind kind, char *err, size_t err_cap)
{
	if (!config_async_has_capacity(err, err_cap)) {
		return NULL;
	}
	struct cfg_job *job = calloc(1, sizeof(*job));
	if (!job) {
		set_error(err, err_cap, "cannot allocate configuration request");
		return NULL;
	}
	job->kind = kind;
	job->result.status = CONFIG_ASYNC_OK;
	if (kind != CFG_JOB_BARRIER &&
			!config_file_path(job->path, sizeof(job->path))) {
		set_error(err, err_cap, "configuration path is unavailable");
		free(job);
		return NULL;
	}
	return job;
}

static bool
cfg_submit(struct cfg_job *job, char *err, size_t err_cap)
{
	if (!cfg_worker_started) {
		set_error(err, err_cap, "configuration worker is unavailable");
		return false;
	}
	pthread_mutex_lock(&cfg_queue_mutex);
	if (cfg_worker_stopping) {
		pthread_mutex_unlock(&cfg_queue_mutex);
		set_error(err, err_cap, "configuration worker is stopping");
		return false;
	}
	wl_list_insert(cfg_pending_jobs.prev, &job->link);
	cfg_nr_pending++;
	pthread_cond_signal(&cfg_queue_cond);
	pthread_mutex_unlock(&cfg_queue_mutex);
	return true;
}

static xmlDoc *
cfg_load_doc(struct cfg_job *job)
{
	xmlDoc *doc = xmlReadFile(job->path, NULL, XML_PARSE_NONET);
	if (!doc) {
		cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
			"cannot parse configuration file");
		return NULL;
	}
	xmlNode *root = xmlDocGetRootElement(doc);
	if (!root || !root->name ||
			strcasecmp((const char *)root->name, "labwc_config")) {
		xmlFreeDoc(doc);
		cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
			"configuration file has no <labwc_config> root");
		return NULL;
	}
	return doc;
}

static void
cfg_save_doc(struct cfg_job *job, xmlDoc *doc)
{
	if (!save_xml_atomic(doc, job->path)) {
		cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
			"cannot save configuration file");
	}
}

static void
cfg_execute_job(struct cfg_job *job)
{
	xmlDoc *doc = NULL;
	struct ipc_json out;

	switch (job->kind) {
	case CFG_JOB_STATUS:
		ipc_json_init(&out, job->result.json, sizeof(job->result.json));
		if (!build_status(&out, job->path, job->dirty)) {
			cfg_result_error(job, CONFIG_ASYNC_INTERNAL, "status too large");
		}
		return;
	case CFG_JOB_BARRIER:
		job->result.reload = true;
		return;
	case CFG_JOB_KEYBINDINGS_LIST:
	case CFG_JOB_RULES_LIST:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		ipc_json_init(&out, job->result.json, sizeof(job->result.json));
		if (job->kind == CFG_JOB_KEYBINDINGS_LIST) {
			if (!build_keybindings_list(doc, &out)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"keybinding list is too large");
			}
		} else if (!build_rules_list(doc, &out)) {
			cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
				"window rule list is too large");
		}
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_SET:
	case CFG_JOB_RESET:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		if (!mutate_xml_value(doc, job->desc, job->parsed.xml,
				job->remove)) {
			cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
				"invalid configuration descriptor");
		} else {
			cfg_save_doc(job, doc);
		}
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_RESET_SECTION:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			struct config_value value;
			bool remove = false;
			if (!section_matches(job->section, &keys[i])) {
				continue;
			}
			if (!default_value(&keys[i], &value, &remove) ||
					!mutate_xml_value(doc, &keys[i],
						value.xml, remove)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"cannot reset configuration section");
				xmlFreeDoc(doc);
				return;
			}
		}
		cfg_save_doc(job, doc);
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_KEYBINDINGS_SET:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		mutate_keybindings_set(doc, job->key, job->action);
		cfg_save_doc(job, doc);
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_KEYBINDINGS_REMOVE:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		if (!mutate_keybindings_remove(doc, job->key)) {
			cfg_result_error(job, CONFIG_ASYNC_NOT_FOUND,
				"keybinding not found");
		} else {
			cfg_save_doc(job, doc);
		}
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_KEYBINDINGS_RESET:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		mutate_keybindings_reset(doc);
		cfg_save_doc(job, doc);
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_RULES_SET:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		mutate_rules_set(doc, job->identifier, job->property, job->value);
		cfg_save_doc(job, doc);
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_RULES_REMOVE:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		if (!mutate_rules_remove(doc, job->identifier,
				job->property[0] ? job->property : NULL)) {
			cfg_result_error(job, CONFIG_ASYNC_NOT_FOUND,
				"window rule not found");
		} else {
			cfg_save_doc(job, doc);
		}
		xmlFreeDoc(doc);
		return;
	case CFG_JOB_RULES_RESET:
		doc = cfg_load_doc(job);
		if (!doc) {
			return;
		}
		mutate_rules_reset(doc);
		cfg_save_doc(job, doc);
		xmlFreeDoc(doc);
		return;
	}
}

static bool
cfg_set_json_response(struct cfg_job *job)
{
	struct ipc_json out;
	ipc_json_init(&out, job->result.json, sizeof(job->result.json));
	bool reload = job->desc->apply == CFG_RELOAD;
	job->result.reload = reload;
	if (job->kind == CFG_JOB_RESET) {
		return ipc_json_add(&out, "{\"target\":\"%s\",\"reset\":true,"
			"\"persisted\":true,\"reload\":%s,\"ok\":true}",
			job->desc->name, reload ? "true" : "false");
	}
	return ipc_json_add(&out,
		"{\"key\":\"%s\",\"value\":%s,\"applied\":%s,"
		"\"persisted\":true,\"reload\":%s,\"ok\":true}",
		job->desc->name, job->parsed.json,
		reload ? "false" : "true", reload ? "true" : "false");
}

static bool
cfg_structural_response(struct cfg_job *job, const char *subject)
{
	struct ipc_json out;
	ipc_json_init(&out, job->result.json, sizeof(job->result.json));
	job->result.reload = true;
	return ipc_json_add(&out, "{\"subject\":") &&
		ipc_json_string(&out, subject ? subject : "") &&
		ipc_json_add(&out,
			",\"persisted\":true,\"reload\":true,\"ok\":true}");
}

static void
cfg_complete_job(struct cfg_job *job, bool invoke_callback)
{
	char event[512];

	if (job->result.status == CONFIG_ASYNC_OK) {
		switch (job->kind) {
		case CFG_JOB_SET:
		case CFG_JOB_RESET:
			apply_value(job->desc, &job->parsed);
			config_api_notify("changed", job->desc->name,
				job->parsed.json, true);
			if (!cfg_set_json_response(job)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_RESET_SECTION:
			for (size_t i = 0;
					i < sizeof(keys) / sizeof(keys[0]); i++) {
				struct config_value value;
				bool remove = false;
				if (!section_matches(job->section, &keys[i]) ||
						!default_value(&keys[i], &value,
							&remove)) {
					continue;
				}
				apply_value(&keys[i], &value);
				config_api_notify("changed", keys[i].name,
					value.json, true);
			}
			job->result.reload = true;
			{
				struct ipc_json out;
				ipc_json_init(&out, job->result.json,
					sizeof(job->result.json));
				if (!ipc_json_add(&out,
						"{\"target\":\"%s\",\"reset\":true,"
						"\"persisted\":true,\"reload\":true,\"ok\":true}",
						job->section)) {
					cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
						"configuration response too large");
				}
			}
			break;
		case CFG_JOB_KEYBINDINGS_SET:
			snprintf(event, sizeof(event),
				"{\"key\":\"%s\",\"action\":\"%s\"}",
				job->key, job->action);
			config_api_notify("keybindings-set", job->key, event, true);
			if (!cfg_structural_response(job, job->key)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_KEYBINDINGS_REMOVE:
			config_api_notify("keybindings-remove", job->key,
				"null", true);
			if (!cfg_structural_response(job, job->key)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_KEYBINDINGS_RESET:
			config_api_notify("keybindings-reset", "keybindings",
				"null", true);
			if (!cfg_structural_response(job, "keybindings")) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_RULES_SET:
			snprintf(event, sizeof(event),
				"{\"identifier\":\"%s\",\"property\":\"%s\","
				"\"value\":\"%s\"}",
				job->identifier, job->property, job->value);
			config_api_notify("rules-set", job->identifier, event, true);
			if (!cfg_structural_response(job, job->identifier)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_RULES_REMOVE:
			if (job->property[0]) {
				snprintf(event, sizeof(event),
					"{\"identifier\":\"%s\",\"property\":\"%s\"}",
					job->identifier, job->property);
			} else {
				snprintf(event, sizeof(event),
					"{\"identifier\":\"%s\",\"property\":null}",
					job->identifier);
			}
			config_api_notify(job->property[0]
					? "rules-property-remove" : "rules-remove",
				job->identifier, event, true);
			if (!cfg_structural_response(job, job->identifier)) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_RULES_RESET:
			config_api_notify("rules-reset", "windowRules", "null", true);
			if (!cfg_structural_response(job, "windowRules")) {
				cfg_result_error(job, CONFIG_ASYNC_INTERNAL,
					"configuration response too large");
			}
			break;
		case CFG_JOB_KEYBINDINGS_LIST:
		case CFG_JOB_RULES_LIST:
		case CFG_JOB_BARRIER:
		case CFG_JOB_STATUS:
			break;
		}
	}

	if (invoke_callback && job->cb) {
		job->cb(&job->result, job->cb_data);
	}
}

static void
cfg_process_completed(bool invoke_callback)
{
	struct wl_list done;
	struct cfg_job *job, *tmp;
	uint64_t count;

	while (read(cfg_completion_fd, &count, sizeof(count)) == sizeof(count)) {
		/* One read drains all completion notifications. */
	}

	wl_list_init(&done);
	pthread_mutex_lock(&cfg_queue_mutex);
	wl_list_insert_list(&done, &cfg_completed_jobs);
	wl_list_init(&cfg_completed_jobs);
	pthread_mutex_unlock(&cfg_queue_mutex);

	wl_list_for_each_safe(job, tmp, &done, link) {
		wl_list_remove(&job->link);
		if (cfg_nr_pending > 0) {
			cfg_nr_pending--;
		}
		cfg_complete_job(job, invoke_callback);
		cfg_reload_needed |= job->result.reload;
		if (!invoke_callback) {
			/* IPC clients are already gone during shutdown. */
			free(job->cb_data);
		}
		free(job);
	}

	if (cfg_reload_needed && cfg_nr_pending == 0 && invoke_callback && !cfg_finishing) {
		cfg_reload_needed = false;
		server_reload_config_and_theme();
	}
}

static int
cfg_completion_handle_fd(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	(void)data;
	cfg_process_completed(true);
	return 0;
}

static void *
cfg_worker_main(void *data)
{
	(void)data;
	while (true) {
		struct cfg_job *job = NULL;
		pthread_mutex_lock(&cfg_queue_mutex);
		while (wl_list_empty(&cfg_pending_jobs) && !cfg_worker_stopping) {
			pthread_cond_wait(&cfg_queue_cond, &cfg_queue_mutex);
		}
		if (wl_list_empty(&cfg_pending_jobs) && cfg_worker_stopping) {
			pthread_mutex_unlock(&cfg_queue_mutex);
			break;
		}
		job = wl_container_of(cfg_pending_jobs.next, job, link);
		wl_list_remove(&job->link);
		pthread_mutex_unlock(&cfg_queue_mutex);

		cfg_execute_job(job);

		pthread_mutex_lock(&cfg_queue_mutex);
		wl_list_insert(cfg_completed_jobs.prev, &job->link);
		pthread_mutex_unlock(&cfg_queue_mutex);
		ssize_t signaled;
		do {
			signaled = write(cfg_completion_fd, &(uint64_t){1}, sizeof(uint64_t));
		} while (signaled < 0 && errno == EINTR);
		if (signaled < 0 && errno != EAGAIN) {
			wlr_log_errno(WLR_ERROR,
				"cannot notify configuration completion");
		}
	}
	return NULL;
}

bool
config_async_init(void)
{
	if (cfg_worker_started) {
		return true;
	}
	wl_list_init(&cfg_pending_jobs);
	wl_list_init(&cfg_completed_jobs);
	cfg_worker_stopping = false;
	cfg_finishing = false;
	cfg_nr_pending = 0;
	cfg_reload_needed = false;
	xmlInitParser();

	cfg_completion_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (cfg_completion_fd < 0) {
		wlr_log_errno(WLR_ERROR, "cannot create configuration worker event");
		return false;
	}
	cfg_completion_source = wl_event_loop_add_fd(server.wl_event_loop,
		cfg_completion_fd, WL_EVENT_READABLE, cfg_completion_handle_fd, NULL);
	if (!cfg_completion_source) {
		wlr_log(WLR_ERROR, "cannot add configuration worker to event loop");
		close(cfg_completion_fd);
		cfg_completion_fd = -1;
		return false;
	}
	if (pthread_create(&cfg_worker_thread, NULL, cfg_worker_main, NULL) != 0) {
		wlr_log(WLR_ERROR, "cannot start configuration worker thread");
		wl_event_source_remove(cfg_completion_source);
		cfg_completion_source = NULL;
		close(cfg_completion_fd);
		cfg_completion_fd = -1;
		return false;
	}
	cfg_worker_started = true;
	return true;
}

void
config_async_finish(void)
{
	if (!cfg_worker_started) {
		return;
	}
	cfg_finishing = true;
	pthread_mutex_lock(&cfg_queue_mutex);
	cfg_worker_stopping = true;
	pthread_cond_broadcast(&cfg_queue_cond);
	pthread_mutex_unlock(&cfg_queue_mutex);
	pthread_join(cfg_worker_thread, NULL);
	cfg_worker_started = false;

	if (cfg_completion_source) {
		wl_event_source_remove(cfg_completion_source);
		cfg_completion_source = NULL;
	}
	if (cfg_completion_fd >= 0) {
		cfg_process_completed(false);
		close(cfg_completion_fd);
		cfg_completion_fd = -1;
	}
}

bool
config_async_defer_reload_if_busy(void)
{
	if (cfg_nr_pending) {
		cfg_reload_needed = true;
		return true;
	}
	return false;
}

bool
config_api_async_reload(char *err, size_t err_cap)
{
	struct cfg_job *job = cfg_new_job(CFG_JOB_BARRIER, err, err_cap);
	if (!job || !cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_status_async(char *err, size_t err_cap, config_async_callback cb,
	void *cb_data)
{
	struct cfg_job *job = cfg_new_job(CFG_JOB_STATUS, err, err_cap);
	if (!job) {
		return false;
	}
	job->dirty = cfg_nr_pending != 0;
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_set_async(const char *key, const char *value, char *err,
	size_t err_cap, config_async_callback cb, void *cb_data)
{
	const struct cfg_key *k = find_key(key);
	if (!k) {
		set_error(err, err_cap, "unknown configuration key");
		return false;
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_SET, err, err_cap);
	if (!job) {
		return false;
	}
	if (!parse_value(k, value, &job->parsed)) {
		set_error(err, err_cap, "invalid value for %s (expected %s)",
			k->name, type_name(k->type));
		free(job);
		return false;
	}
	job->desc = k;
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_reset_async(const char *key, char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	const struct cfg_key *k = find_key(key);
	if (!k) {
		set_error(err, err_cap, "unknown configuration key");
		return false;
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_RESET, err, err_cap);
	if (!job) {
		return false;
	}
	if (!default_value(k, &job->parsed, &job->remove)) {
		set_error(err, err_cap, "invalid default for key");
		free(job);
		return false;
	}
	job->desc = k;
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_reset_section_async(const char *section, char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	if (!valid_section(section)) {
		set_error(err, err_cap, "unknown configuration section");
		return false;
	}
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		struct config_value value;
		if (section_matches(section, &keys[i]) &&
				!default_value(&keys[i], &value, NULL)) {
			set_error(err, err_cap, "invalid default for %s", keys[i].name);
			return false;
		}
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_RESET_SECTION, err, err_cap);
	if (!job) {
		return false;
	}
	snprintf(job->section, sizeof(job->section), "%s", section);
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_keybindings_list_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	struct cfg_job *job = cfg_new_job(CFG_JOB_KEYBINDINGS_LIST, err, err_cap);
	if (!job) {
		return false;
	}
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_keybindings_set_async(const char *key, const char *action,
	char *err, size_t err_cap, config_async_callback cb, void *cb_data)
{
	const char *canonical = action ? canonical_action(action) : NULL;
	if (!safe_identifier(key, 63, false) || !canonical) {
		set_error(err, err_cap, "invalid keybinding or unsafe action");
		return false;
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_KEYBINDINGS_SET, err, err_cap);
	if (!job) {
		return false;
	}
	snprintf(job->key, sizeof(job->key), "%s", key);
	snprintf(job->action, sizeof(job->action), "%s", canonical);
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_keybindings_remove_async(const char *key, char *err,
	size_t err_cap, config_async_callback cb, void *cb_data)
{
	if (!safe_identifier(key, 63, false)) {
		set_error(err, err_cap, "invalid keybinding");
		return false;
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_KEYBINDINGS_REMOVE, err, err_cap);
	if (!job) {
		return false;
	}
	snprintf(job->key, sizeof(job->key), "%s", key);
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_keybindings_reset_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	struct cfg_job *job = cfg_new_job(CFG_JOB_KEYBINDINGS_RESET, err, err_cap);
	if (!job) {
		return false;
	}
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_rules_list_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	struct cfg_job *job = cfg_new_job(CFG_JOB_RULES_LIST, err, err_cap);
	if (!job) {
		return false;
	}
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_rules_set_async(const char *identifier, const char *property,
	const char *value, char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	const char *canonical = value ? canonical_rule_value(property, value) : NULL;
	const char *name = canonical_rule_property(property);
	if (!safe_identifier(identifier, 127, true) || !canonical || !name) {
		set_error(err, err_cap, "invalid rule identifier, property or value");
		return false;
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_RULES_SET, err, err_cap);
	if (!job) {
		return false;
	}
	snprintf(job->identifier, sizeof(job->identifier), "%s", identifier);
	snprintf(job->property, sizeof(job->property), "%s", name);
	snprintf(job->value, sizeof(job->value), "%s", canonical);
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_rules_remove_async(const char *identifier, const char *property,
	char *err, size_t err_cap, config_async_callback cb, void *cb_data)
{
	if (!safe_identifier(identifier, 127, true) ||
			(property && !is_rule_property(property))) {
		set_error(err, err_cap, "invalid rule identifier or property");
		return false;
	}
	struct cfg_job *job = cfg_new_job(CFG_JOB_RULES_REMOVE, err, err_cap);
	if (!job) {
		return false;
	}
	snprintf(job->identifier, sizeof(job->identifier), "%s", identifier);
	if (property) {
		snprintf(job->property, sizeof(job->property), "%s",
			canonical_rule_property(property));
	}
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

bool
config_api_rules_reset_async(char *err, size_t err_cap,
	config_async_callback cb, void *cb_data)
{
	struct cfg_job *job = cfg_new_job(CFG_JOB_RULES_RESET, err, err_cap);
	if (!job) {
		return false;
	}
	job->cb = cb;
	job->cb_data = cb_data;
	if (!cfg_submit(job, err, err_cap)) {
		free(job);
		return false;
	}
	return true;
}

void
config_api_notify(const char *event, const char *key,
	const char *value_json, bool persisted)
{
	ipc_notify_config(event, key, value_json ? value_json : "null", persisted);
}
