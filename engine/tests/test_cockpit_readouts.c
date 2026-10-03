/* Checks the cockpit's numeric readouts and target panel (xvt_runtime/snapshot/cockpit_readouts.h) against
 * the promises in its header: what a recorded number holds, the 16-bit rule and its exceptions, which
 * numbers CopyState shows, the course panel, the target panel across a change of target or mode, its
 * cover, armament and order fields, and the launcher counts. The test builds its own world (an object
 * table with two main slots and one static slot, the local player in seat 0) and sets the flight text
 * globals and the palette itself; every case starts from Reset and that world.
 *
 * Not checked here: which HUD element bindings RecordCachedNumber maps to which readout, since the header
 * names the readouts but not their binding numbers; only an out-of-range binding is checked. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"

#include <string.h>

enum { BYPASS = 9, FOREGROUND = 7, BACKGROUND = 4, SHADOW = 5 };

enum { TARGET_A = 1, SIGNATURE_A = 0x0101, TARGET_B = 2, SIGNATURE_B = 0x0202 };

static ObjectRecord g_testObjects[3];
static XvtCockpitState g_state;
static uint8_t g_framebuffer[16];

/* Slots 0 and 1 are main slots and slot 2 is a static one; the local player in seat 0 targets nothing.
 * The text cursor is at (30, 40) inside the clip (10, 20) to (210, 120). */
static void Start(void)
{
	for (unsigned index = 0; index < 256; ++index) {
		g_swPalette[index] = (RgbTriplet){(uint8_t)(index & 63),
						  (uint8_t)(index >> 6), 7};
	}
	memset(g_testObjects, 0, sizeof g_testObjects);
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = 2;
	g_regionStaticObjectSlotCount = 1;
	g_testObjects[TARGET_A].objectType = 3;
	g_testObjects[TARGET_A].objectSignature = SIGNATURE_A;
	g_testObjects[TARGET_B].objectType = 4;
	g_testObjects[TARGET_B].objectSignature = SIGNATURE_B;
	memset(g_players, 0, sizeof g_players);
	g_localPlayer = 0;
	g_players[0].currentTargetObjectIdx = -1;
	memset(g_hudElementLayouts, 0, sizeof g_hudElementLayouts);

	g_flightClipLeft = 10;
	g_flightClipTop = 20;
	g_flightClipRight = 210;
	g_flightClipBottom = 120;
	g_flightCursorX = 30;
	g_flightCursorY = 40;
	g_flightFontTier = 2;
	g_flightTextColorIndex = FOREGROUND;
	g_flightTextBgColor = BACKGROUND;
	g_flightTextShadowColor = SHADOW;
	g_flightTextShadowEnabled = 1;
	g_flightTransparentColorIndex = BYPASS;
	g_flightOffscreenBuffer = NULL;
	g_flightSwFramebufferBase = g_framebuffer;

	XvtCockpitText_ResetFields();
	XvtCockpitReadouts_Reset();
}

/* A state whose instruments all show, as XvtCockpitInstruments_Build leaves it in the forward view. */
static XvtCockpitState *Shown(void)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.view.hud_state = HUD_VIEW_FORWARD;
	g_state.view.instruments_visible = 1;
	g_state.readouts.speed.visible = g_state.readouts.throttle.visible = 1;
	g_state.readouts.clock_minutes.visible =
		g_state.readouts.clock_seconds.visible = 1;
	g_state.systems.countermeasure_count.visible = 1;
	for (unsigned slot = 0; slot < 4; ++slot) {
		g_state.weapons.launchers[slot].count.visible = 1;
	}
	for (unsigned slot = 0; slot < XVT_HUD_WEAPON_SLOTS; ++slot) {
		g_state.weapons.slots[slot].charge_percent.visible = 1;
	}
	return &g_state;
}

static const XvtCockpitState *Copied(void)
{
	XvtCockpitReadouts_CopyState(Shown());
	return &g_state;
}

static void Target(int slot)
{
	g_players[0].currentTargetObjectIdx = (int16_t)slot;
}

static void Record(XvtCockpitNumberId id, unsigned value)
{
	XvtCockpitReadouts_RecordNumber(id, value, 3, 2);
}

static const XvtCockpitTextField *TextField(XvtCockpitTextFieldId id)
{
	static XvtCockpitState fields;
	memset(&fields, 0, sizeof fields);
	fields.view.hud_state = HUD_VIEW_FORWARD;
	XvtCockpitText_CopyFields(&fields);
	return &fields.text_fields[id];
}

static void CheckRecordNumber(void)
{
	Start();
	XvtCockpitReadouts_RecordNumber(XVT_COCKPIT_NUMBER_SPEED, 0x12345, 3,
					2);
	const XvtCockpitNumber *speed = &Copied()->readouts.speed;
	XVT_ASSERT_INT_EQ(speed->visible, 1);
	/* Values keep 16 bits. */
	XVT_ASSERT_INT_EQ(speed->value, 0x2345);
	XVT_ASSERT_INT_EQ(speed->field_width, 3);
	XVT_ASSERT_INT_EQ(speed->minimum_digits, 2);
	XVT_ASSERT_INT_EQ(speed->x, 30);
	XVT_ASSERT_INT_EQ(speed->y, 40);
	XVT_ASSERT_INT_EQ(speed->bounds.x, 10);
	XVT_ASSERT_INT_EQ(speed->bounds.y, 20);
	XVT_ASSERT_INT_EQ(speed->bounds.width, 200);
	XVT_ASSERT_INT_EQ(speed->bounds.height, 100);
	XVT_ASSERT_INT_EQ(speed->font_tier, 2);
	XVT_ASSERT_INT_EQ(speed->foreground, FOREGROUND);
	XVT_ASSERT_INT_EQ(speed->background, BACKGROUND);
	XVT_ASSERT_INT_EQ(speed->shadow_enabled, 1);

	/* The course score keeps all its bits. */
	XvtCockpitReadouts_RecordCourse(1, 2, 3, 4);
	Record(XVT_COCKPIT_NUMBER_COURSE_SCORE, 0x12345);
	XVT_ASSERT_INT_EQ(Copied()->proving_grounds.score.value, 0x12345);
}

static void CheckNumberAllOnes(void)
{
	Start();
	/* A 16-bit 0xFFFF is drawn in the '@' color with no shadow. */
	Record(XVT_COCKPIT_NUMBER_SPEED, 0xFFFF);
	const XvtCockpitNumber *speed = &Copied()->readouts.speed;
	XVT_ASSERT_INT_EQ(speed->foreground,
			  XvtCockpitText_ResolveColor('@', BYPASS));
	XVT_ASSERT_INT_EQ(speed->shadow_enabled, 0);
	/* Any other value keeps the live foreground. */
	Record(XVT_COCKPIT_NUMBER_SPEED, 0xFFFE);
	XVT_ASSERT_INT_EQ(Copied()->readouts.speed.foreground, FOREGROUND);
}

static void CheckRecordNumberOutOfRange(void)
{
	Start();
	XvtCockpitReadouts_RecordNumber(XVT_COCKPIT_NUMBER_COUNT, 5, 3, 2);
	XvtCockpitReadouts_RecordNumber((XvtCockpitNumberId)-1, 5, 3, 2);
	/* A binding past the element table is not a readout. */
	XvtCockpitReadouts_RecordCachedNumber(HUD_INSTRUMENT_COUNT + 40, 5, 2);
	const XvtCockpitState *state = Copied();
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 0);
	XVT_ASSERT_INT_EQ(state->readouts.throttle.visible, 0);
	XVT_ASSERT_INT_EQ(state->proving_grounds.score.visible, 0);
	XVT_ASSERT_INT_EQ(state->weapons.slots[XVT_HUD_WEAPON_SLOTS - 1]
				  .charge_percent.visible,
			  0);
}

static void CheckCopyStateNeedsRecordAndInstrument(void)
{
	Start();
	Record(XVT_COCKPIT_NUMBER_SPEED, 11);
	Record(XVT_COCKPIT_NUMBER_COUNTERMEASURES, 12);
	Record((XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 2), 13);
	Record((XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LASER_FIRST + 5), 14);

	/* Recorded and shown: visible with the recorded value. */
	const XvtCockpitState *state = Copied();
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 1);
	XVT_ASSERT_INT_EQ(state->readouts.speed.value, 11);
	XVT_ASSERT_INT_EQ(state->systems.countermeasure_count.visible, 1);
	XVT_ASSERT_INT_EQ(state->systems.countermeasure_count.value, 12);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[2].count.visible, 1);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[2].count.value, 13);
	XVT_ASSERT_INT_EQ(state->weapons.slots[5].charge_percent.visible, 1);
	XVT_ASSERT_INT_EQ(state->weapons.slots[5].charge_percent.value, 14);

	/* Shown but never recorded: hidden. */
	XVT_ASSERT_INT_EQ(state->readouts.throttle.visible, 0);
	XVT_ASSERT_INT_EQ(state->readouts.clock_minutes.visible, 0);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[1].count.visible, 0);
	XVT_ASSERT_INT_EQ(state->weapons.slots[4].charge_percent.visible, 0);

	/* Recorded but the state does not show the instrument: hidden. */
	XvtCockpitState *hidden = Shown();
	hidden->readouts.speed.visible = 0;
	hidden->systems.countermeasure_count.visible = 0;
	hidden->weapons.launchers[2].count.visible = 0;
	hidden->weapons.slots[5].charge_percent.visible = 0;
	XvtCockpitReadouts_CopyState(hidden);
	XVT_ASSERT_INT_EQ(hidden->readouts.speed.visible, 0);
	XVT_ASSERT_INT_EQ(hidden->systems.countermeasure_count.visible, 0);
	XVT_ASSERT_INT_EQ(hidden->weapons.launchers[2].count.visible, 0);
	XVT_ASSERT_INT_EQ(hidden->weapons.slots[5].charge_percent.visible, 0);
}

static void CheckCourse(void)
{
	Start();
	Record(XVT_COCKPIT_NUMBER_COURSE_LEVEL, 3);
	/* Course numbers follow the course: hidden until it is recorded. */
	XVT_ASSERT_INT_EQ(Copied()->proving_grounds.level.visible, 0);

	XvtCockpitReadouts_RecordCourse(5, 6, 70, 80);
	const XvtCockpitState *state = Copied();
	XVT_ASSERT_INT_EQ(state->proving_grounds.visible, 1);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.x, 5);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.y, 6);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.width, 70);
	XVT_ASSERT_INT_EQ(state->proving_grounds.bounds.height, 80);
	XVT_ASSERT_INT_EQ(state->proving_grounds.level.visible, 1);
	XVT_ASSERT_INT_EQ(state->proving_grounds.level.value, 3);

	/* A new update hides the course until it is recorded again. */
	XvtCockpitReadouts_BeginUpdate();
	state = Copied();
	XVT_ASSERT_INT_EQ(state->proving_grounds.visible, 0);
	XVT_ASSERT_INT_EQ(state->proving_grounds.level.visible, 0);
}

static void CheckTargetShows(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(0);
	const XvtCockpitState *state = Copied();
	XVT_ASSERT_INT_EQ(state->target.visible, 1);
	XVT_ASSERT_INT_EQ(state->target.object.slot, TARGET_A);
	XVT_ASSERT_INT_EQ(state->target.object.signature, SIGNATURE_A);
	XVT_ASSERT_INT_EQ(state->target.cmd_mode, 0);

	/* The static slot is a target too. */
	Target(TARGET_B);
	XvtCockpitReadouts_BeginTarget(0);
	XVT_ASSERT_INT_EQ(Copied()->target.object.slot, TARGET_B);

	/* Only while instruments are visible... */
	XvtCockpitState *hidden = Shown();
	hidden->view.instruments_visible = 0;
	XvtCockpitReadouts_CopyState(hidden);
	XVT_ASSERT_INT_EQ(hidden->target.visible, 0);

	/* ...and only when updated since BeginUpdate. */
	XvtCockpitReadouts_BeginUpdate();
	XVT_ASSERT_INT_EQ(Copied()->target.visible, 0);
	XvtCockpitReadouts_BeginTarget(0);
	XVT_ASSERT_INT_EQ(Copied()->target.visible, 1);
}

static void CheckTargetValuesByMode(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(0);
	Record(XVT_COCKPIT_NUMBER_TARGET_SYSTEMS, 1);
	Record(XVT_COCKPIT_NUMBER_TARGET_SHIELDS, 2);
	Record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	Record(XVT_COCKPIT_NUMBER_TARGET_RANGE, 4);
	Record(XVT_COCKPIT_NUMBER_TARGET_RANGE_FRACTION, 5);
	Record(XVT_COCKPIT_NUMBER_ORDER_RANGE, 6);
	Record(XVT_COCKPIT_NUMBER_ORDER_MINUTES, 7);

	/* Outside command mode: range and systems show, order range and time do not. */
	const XvtCockpitTarget *target = &Copied()->target;
	XVT_ASSERT_INT_EQ(target->systems.visible, 1);
	XVT_ASSERT_INT_EQ(target->shields.visible, 1);
	XVT_ASSERT_INT_EQ(target->hull.visible, 1);
	XVT_ASSERT_INT_EQ(target->hull.value, 3);
	XVT_ASSERT_INT_EQ(target->distance.visible, 1);
	XVT_ASSERT_INT_EQ(target->distance_fraction.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_distance.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 0);

	/* In command mode the other way round; shields and hull show in both. */
	XvtCockpitReadouts_BeginTarget(1);
	Record(XVT_COCKPIT_NUMBER_TARGET_SYSTEMS, 1);
	Record(XVT_COCKPIT_NUMBER_TARGET_SHIELDS, 2);
	Record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	Record(XVT_COCKPIT_NUMBER_TARGET_RANGE, 4);
	Record(XVT_COCKPIT_NUMBER_ORDER_RANGE, 6);
	Record(XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION, 8);
	Record(XVT_COCKPIT_NUMBER_ORDER_MINUTES, 7);
	Record(XVT_COCKPIT_NUMBER_ORDER_SECONDS, 9);
	target = &Copied()->target;
	XVT_ASSERT_INT_EQ(target->cmd_mode, 1);
	XVT_ASSERT_INT_EQ(target->systems.visible, 0);
	XVT_ASSERT_INT_EQ(target->distance.visible, 0);
	XVT_ASSERT_INT_EQ(target->shields.visible, 1);
	XVT_ASSERT_INT_EQ(target->hull.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_distance.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_distance.value, 6);
	XVT_ASSERT_INT_EQ(target->order_distance_fraction.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_seconds.visible, 1);
}

static void CheckTargetCover(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(0);
	Record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	XvtCockpitReadouts_RecordTargetCover(77);
	const XvtCockpitTarget *target = &Copied()->target;
	XVT_ASSERT_INT_EQ(target->visible, 1);
	XVT_ASSERT_INT_EQ(target->panel_cover, 1);
	XVT_ASSERT_INT_EQ(target->cover_binding, 77);
	XVT_ASSERT_INT_EQ(target->labels_visible, 0);
	/* The values need the panel uncovered. */
	XVT_ASSERT_INT_EQ(target->hull.visible, 0);

	/* Hiding keeps a covered panel shown... */
	XvtCockpitReadouts_HideTarget();
	XVT_ASSERT_INT_EQ(Copied()->target.visible, 1);
	/* ...and hides an uncovered one. */
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(0);
	XvtCockpitReadouts_HideTarget();
	XVT_ASSERT_INT_EQ(Copied()->target.visible, 0);
}

static void CheckHideTargetClearsFields(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(0);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_TARGET_NAME, "TIE",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t generation =
		TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->generation;
	XvtCockpitReadouts_HideTarget();
	XVT_ASSERT_INT_EQ(
		TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->caption.text[0], 0);
	XVT_ASSERT_TRUE(TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->generation >
			generation);
}

static void CheckTargetChangeClears(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(0);
	Record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	Record(XVT_COCKPIT_NUMBER_ORDER_SECONDS, 4);
	Record(XVT_COCKPIT_NUMBER_SPEED, 5);
	XvtCockpitReadouts_RecordArmament(1, 2);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_TARGET_NAME, "TIE",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t name = TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->generation;

	/* The same target in the same mode keeps everything. */
	XvtCockpitReadouts_BeginTarget(0);
	const XvtCockpitState *state = Copied();
	XVT_ASSERT_INT_EQ(state->target.hull.visible, 1);
	XVT_ASSERT_INT_EQ(state->target.armament[1].state, 2);
	XVT_ASSERT_INT_EQ(
		strcmp(TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->caption.text,
		       "TIE"),
		0);

	/* A new target clears the target, its numbers and its text fields, but not the other numbers. */
	Target(TARGET_B);
	XvtCockpitReadouts_BeginTarget(1);
	Record(XVT_COCKPIT_NUMBER_TARGET_SHIELDS, 6);
	state = Copied();
	XVT_ASSERT_INT_EQ(state->target.hull.visible, 0);
	XVT_ASSERT_INT_EQ(state->target.order_seconds.visible, 0);
	XVT_ASSERT_INT_EQ(state->target.shields.visible, 1);
	XVT_ASSERT_INT_EQ(state->target.armament[1].state, 0);
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 1);
	XVT_ASSERT_INT_EQ(
		TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->caption.text[0], 0);
	XVT_ASSERT_TRUE(TextField(XVT_COCKPIT_TEXT_TARGET_NAME)->generation >
			name);

	/* A change of mode alone clears them too. */
	XvtCockpitReadouts_BeginTarget(0);
	XVT_ASSERT_INT_EQ(Copied()->target.shields.visible, 0);
}

static void CheckCommandChangeResetsThreatCache(void)
{
	Start();
	g_hudElementStateCache[102] = g_hudElementStateCache[103] = 55;
	Target(TARGET_A);
	/* A change outside command mode leaves the cache alone. */
	XvtCockpitReadouts_BeginTarget(0);
	XVT_ASSERT_INT_EQ(g_hudElementStateCache[102], 55);
	XVT_ASSERT_INT_EQ(g_hudElementStateCache[103], 55);
	/* A change into command mode resets both entries. */
	XvtCockpitReadouts_BeginTarget(1);
	XVT_ASSERT_TRUE(g_hudElementStateCache[102] != 55);
	XVT_ASSERT_TRUE(g_hudElementStateCache[103] != 55);
	/* No change, no reset. */
	g_hudElementStateCache[102] = g_hudElementStateCache[103] = 55;
	XvtCockpitReadouts_BeginTarget(1);
	XVT_ASSERT_INT_EQ(g_hudElementStateCache[102], 55);
	XVT_ASSERT_INT_EQ(g_hudElementStateCache[103], 55);
}

static void CheckResetTargetsNoObject(void)
{
	Start();
	/* Reset clears the target, replacing what the state held. */
	XvtCockpitState *state = Shown();
	state->target.cover_binding = 99;
	state->target.type = 99;
	XvtCockpitReadouts_CopyState(state);
	XVT_ASSERT_INT_EQ(state->target.cover_binding, 0);
	XVT_ASSERT_INT_EQ(state->target.type, 0);
	XvtSnapObjectId none = state->target.object;

	/* A player with no target starts the panel on the same no-object target Reset left: no change, so a
	 * target number recorded before it survives. */
	Record(XVT_COCKPIT_NUMBER_TARGET_HULL, 3);
	XvtCockpitReadouts_BeginTarget(0);
	state = Shown();
	XvtCockpitReadouts_CopyState(state);
	XVT_ASSERT_INT_EQ(state->target.object.slot, none.slot);
	XVT_ASSERT_INT_EQ(state->target.object.signature, none.signature);
	XVT_ASSERT_INT_EQ(state->target.hull.visible, 1);
}

static void CheckArmament(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(1);
	XvtCockpitReadouts_RecordArmament(3, 2);
	XvtCockpitReadouts_RecordArmament(4, 9);
	const XvtCockpitTarget *target = &Copied()->target;
	XVT_ASSERT_INT_EQ(target->armament[3].state, 2);
	XVT_ASSERT_INT_EQ(target->armament[0].state, 0);
}

static void CheckClearOrder(void)
{
	Start();
	Target(TARGET_A);
	XvtCockpitReadouts_BeginTarget(1);
	Record(XVT_COCKPIT_NUMBER_ORDER_RANGE, 1);
	Record(XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION, 2);
	Record(XVT_COCKPIT_NUMBER_ORDER_MINUTES, 3);
	Record(XVT_COCKPIT_NUMBER_ORDER_SECONDS, 4);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR, ".",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR, ":",
				   XVT_COCKPIT_ALIGN_LEFT);

	XvtCockpitReadouts_ClearOrderRange();
	const XvtCockpitTarget *target = &Copied()->target;
	XVT_ASSERT_INT_EQ(target->order_distance.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_distance_fraction.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 1);
	XVT_ASSERT_INT_EQ(target->order_seconds.visible, 1);
	XVT_ASSERT_INT_EQ(TextField(XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR)
				  ->caption.text[0],
			  0);
	XVT_ASSERT_INT_EQ(
		strcmp(TextField(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR)
			       ->caption.text,
		       ":"),
		0);

	XvtCockpitReadouts_ClearOrderTime();
	target = &Copied()->target;
	XVT_ASSERT_INT_EQ(target->order_minutes.visible, 0);
	XVT_ASSERT_INT_EQ(target->order_seconds.visible, 0);
	XVT_ASSERT_INT_EQ(TextField(XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR)
				  ->caption.text[0],
			  0);
}

static void CheckLaunchers(void)
{
	Start();
	Record((XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 1), 7);
	Record((XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 3), 8);
	XvtCockpitNumber number;
	XvtCockpitReadouts_CopyLauncher(&number, 1);
	XVT_ASSERT_INT_EQ(number.visible, 1);
	XVT_ASSERT_INT_EQ(number.value, 7);

	XvtCockpitReadouts_ClearLauncher(1);
	XvtCockpitReadouts_CopyLauncher(&number, 1);
	XVT_ASSERT_INT_EQ(number.visible, 0);

	/* Launchers from 4 up are ignored: the copy leaves number alone and the clear touches nothing. */
	memset(&number, 0x5A, sizeof number);
	XvtCockpitNumber before;
	memcpy(&before, &number, sizeof before);
	XvtCockpitReadouts_CopyLauncher(&number, 4);
	XVT_ASSERT_INT_EQ(memcmp(&number, &before, sizeof number), 0);
	XvtCockpitReadouts_ClearLauncher(4);
	XvtCockpitReadouts_CopyLauncher(&number, 3);
	XVT_ASSERT_INT_EQ(number.visible, 1);
	XVT_ASSERT_INT_EQ(number.value, 8);
}

static void CheckResetClearsNumbers(void)
{
	Start();
	Record(XVT_COCKPIT_NUMBER_SPEED, 1);
	Record((XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 1), 2);
	XvtCockpitReadouts_RecordCourse(1, 2, 3, 4);
	XvtCockpitReadouts_Reset();
	const XvtCockpitState *state = Copied();
	XVT_ASSERT_INT_EQ(state->readouts.speed.visible, 0);
	XVT_ASSERT_INT_EQ(state->readouts.speed.value, 0);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[1].count.visible, 0);
	XVT_ASSERT_INT_EQ(state->proving_grounds.visible, 0);
}

int main(void)
{
	CheckRecordNumber();
	CheckNumberAllOnes();
	CheckRecordNumberOutOfRange();
	CheckCopyStateNeedsRecordAndInstrument();
	CheckCourse();
	CheckTargetShows();
	CheckTargetValuesByMode();
	CheckTargetCover();
	CheckHideTargetClearsFields();
	CheckTargetChangeClears();
	CheckCommandChangeResetsThreatCache();
	CheckResetTargetsNoObject();
	CheckArmament();
	CheckClearOrder();
	CheckLaunchers();
	CheckResetClearsNumbers();
	return 0;
}
