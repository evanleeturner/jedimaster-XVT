#include "xvt_runtime/runtime/sith.h"

static int g_setting = XVT_SITH_NONE;

void xvt_sith_set_setting(int setting)
{
	g_setting = setting == XVT_SITH_APPRENTICES || setting == XVT_SITH_LORD
			    ? setting
			    : XVT_SITH_NONE;
}

int xvt_sith_get_setting(void) { return g_setting; }

void xvt_sith_set_from_description(int described)
{
	xvt_sith_set_setting(described);
}

const char *xvt_sith_setting_name(int setting)
{
	switch (setting) {
	case XVT_SITH_APPRENTICES:
		return "apprentices";
	case XVT_SITH_LORD:
		return "lord";
	default:
		return "none";
	}
}
