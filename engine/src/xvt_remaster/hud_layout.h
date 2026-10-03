#ifndef XVT_REMASTER_HUD_LAYOUT_H
#define XVT_REMASTER_HUD_LAYOUT_H

#include "xvt_runtime/snapshot/cockpit_state.h"
/* Compiles the cockpit definition's instrument elements into sprite bindings (which panel part draws each
 * widget state, where, mirrored or not) and anchors (CRT, radar, page and message placements) for one
 * view; a cache keyed by definition generation, view and installed features. */

/* One binding per widget; the ranges are per weapon slot, launcher, shield layer, threat, feature cover
 * and armament bank. */
typedef enum XvtHudSpriteRole {
	XVT_HUD_LASER_CHARGE,
	XVT_HUD_LASER_SELECTION = XVT_HUD_LASER_CHARGE + XVT_HUD_WEAPON_SLOTS,
	XVT_HUD_LASER_READY = XVT_HUD_LASER_SELECTION + XVT_HUD_WEAPON_SLOTS,
	XVT_HUD_LASER_LOCK = XVT_HUD_LASER_READY + XVT_HUD_WEAPON_SLOTS,
	XVT_HUD_LAUNCHER = XVT_HUD_LASER_LOCK + XVT_HUD_WEAPON_SLOTS,
	XVT_HUD_SHIELD = XVT_HUD_LAUNCHER + 4,
	XVT_HUD_HULL = XVT_HUD_SHIELD + 4,
	XVT_HUD_ENGINE_POWER,
	XVT_HUD_LASER_POWER,
	XVT_HUD_SHIELD_POWER,
	XVT_HUD_BEAM_POWER,
	XVT_HUD_SFOILS,
	XVT_HUD_SHIELD_DISTRIBUTION,
	XVT_HUD_BEAM,
	XVT_HUD_BEAM_ENABLED,
	XVT_HUD_TARGET_LOCK,
	XVT_HUD_COUNTERMEASURE_SELECTION,
	XVT_HUD_CRITICAL_WARNING,
	XVT_HUD_THREAT,
	XVT_HUD_FEATURE_COVER = XVT_HUD_THREAT + 4,
	XVT_HUD_TARGET_COVER = XVT_HUD_FEATURE_COVER + 13,
	XVT_HUD_TARGET_ALT_COVER,
	XVT_HUD_UNAVAILABLE_SHIELDS,
	XVT_HUD_UNAVAILABLE_BEAM,
	XVT_HUD_UNAVAILABLE_BEAM_POWER,
	XVT_HUD_CMD_ARMAMENT,
	XVT_HUD_SPRITE_ROLE_COUNT = XVT_HUD_CMD_ARMAMENT + 4
} XvtHudSpriteRole;

typedef enum XvtHudPartColor {
	XVT_HUD_PART_ORIGINAL,
	XVT_HUD_PART_MONOCHROME,
	XVT_HUD_PART_INDEXED_FADE
} XvtHudPartColor;

/* A panel bitmap to prepare: its asset binding, its color key, and its colorization: original palette
 * colors, monochrome (white, tinted at draw time with the palette color at index color), or an indexed
 * fade, each palette index shifted by color - fade. */
typedef struct XvtHudPartRequest {
	XvtCockpitAssetBinding source;
	uint16_t key;
	int16_t fade;
	uint8_t color_mode, color;
} XvtHudPartRequest;

enum { XVT_HUD_PART_CAPACITY = 1024 };

typedef struct XvtHudSpriteBinding {
	int16_t x, y, step_x;
	/* Shields: level 0..10. Beam: segment*4+charge step. Covers: intact/damaged.
	 * Other families use the original state as the part offset. */
	uint16_t first_part, part_count;
	uint8_t mirrored;
} XvtHudSpriteBinding;

typedef struct XvtHudAnchor {
	int16_t x, y;
} XvtHudAnchor;

/* The compiled layout: the view's source size, mirroring, viewport and projection offset; the base
 * artwork asset; the CRT rect, radar anchors and page and message placements; the sprite bindings and
 * the part requests they index; the definition's beam segment offsets. */
typedef struct XvtHudLayout {
	uint16_t source_width, source_height;
	uint64_t base_asset_id;
	uint8_t mirrored;
	XvtSnapRect viewport, crt;
	int16_t projection_offset_y;
	XvtHudAnchor radar[2];
	XvtSnapRect pages[MFD_PAGE_COUNT], messages[XVT_COCKPIT_MESSAGE_COUNT];
	XvtHudSpriteBinding sprites[XVT_HUD_SPRITE_ROLE_COUNT];
	uint16_t part_count;
	XvtHudPartRequest parts[XVT_HUD_PART_CAPACITY];
	int16_t beam_offsets[9][2];
} XvtHudLayout;

typedef struct XvtHudLayoutCache {
	XvtHudLayout layout;
	uint64_t definition_generation;
	XvtCockpitView view_key;
	uint32_t installed_hud_features;
	uint8_t valid;
} XvtHudLayoutCache;

/* Compile source coordinates and finite artwork states. Text and number bounds
 * are already resolved by original emission and remain in their named models. */
/* Zeroes out and fills it from state: the view's size, mirroring, viewport and projection offset; the
 * definition's beam offsets; the base asset (the view's resource descriptor's LFD, unless full
 * screen); the anchors (the CRT rect from element 2 of the instrument set, the radar from elements 0
 * and 1, the page and message placements from the state). With a valid definition, binds the weapon
 * and system widgets for the forward and HUD-only views, and the armament banks for the target-camera
 * view, each binding adding its part requests. Returns 0 for a NULL argument, a zero screen size, an
 * instrument base over 288, more laser slots than XVT_HUD_WEAPON_SLOTS, a panel binding out of range,
 * a request whose panel has no asset, or more than XVT_HUD_PART_CAPACITY requests. */
int XvtHudLayout_Compile(const XvtCockpitState *state, XvtHudLayout *out);
/* Zero-initialize/reset the cache at world replacement. Pane placements refresh
 * independently; instrument values do not invalidate the compiled bindings. */
/* Recompiles the cache's layout when the cache is invalid or the definition generation, the installed
 * features, or the view key (size, HUD state, instrument base, descriptor, mirroring, viewport,
 * offset, compact flag, laser slots) changed; otherwise refreshes only the anchors. Returns 0 for a
 * NULL argument or a failed compile, the cache unchanged. */
int XvtHudLayout_Update(XvtHudLayoutCache *cache, const XvtCockpitState *state);
/* The uniform fit of the layout's source size into width x height, centered: *scale is the smaller
 * size ratio, or 0 when a size is not positive; the offsets center the scaled source. */
void XvtHudLayout_Fit(const XvtHudLayout *layout, int width, int height,
		      float *scale, float *offset_x, float *offset_y);

#endif
