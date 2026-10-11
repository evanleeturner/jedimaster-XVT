#include "xvt_runtime/runtime/launch_result.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/* The status word of an exit code, or NULL. */
static const char *status_word(int exit_code)
{
	switch (exit_code) {
	case XVT_LAUNCH_EXIT_FLOWN:
		return "flown";
	case XVT_LAUNCH_EXIT_REFUSED:
		return "refused";
	case XVT_LAUNCH_EXIT_NOT_REACHED:
		return "not_reached";
	case XVT_LAUNCH_EXIT_FLIGHT_FAILED:
		return "failed";
	default:
		return NULL;
	}
}

/* The outcome word of an outcome code, or NULL. */
static const char *outcome_word(int outcome)
{
	switch (outcome) {
	case XVT_LAUNCH_OUTCOME_NONE:
		return "none";
	case XVT_LAUNCH_OUTCOME_COMPLETED:
		return "completed";
	case XVT_LAUNCH_OUTCOME_NOT_COMPLETED:
		return "not_completed";
	default:
		return NULL;
	}
}

size_t xvt_launch_result_format(const struct xvt_launch_result *r, char *out,
				size_t size)
{
	const char *status = status_word(r->exit_code);
	const char *outcome = outcome_word(r->outcome);
	if (!status || !outcome || size == 0) {
		return 0;
	}
	cJSON *root = cJSON_CreateObject();
	if (!root) {
		return 0;
	}
	cJSON_AddNumberToObject(root, "schema_version", 1);
	cJSON_AddStringToObject(root, "status", status);
	cJSON_AddNumberToObject(root, "exit_code", r->exit_code);
	cJSON_AddStringToObject(root, "outcome", outcome);
	cJSON_AddNumberToObject(root, "score", r->score);
	cJSON_AddNumberToObject(root, "kills", r->kills);
	cJSON_AddNumberToObject(root, "losses", r->losses);
	cJSON_AddNumberToObject(root, "flight_seconds", r->flight_seconds);
	cJSON_AddStringToObject(root, "why", r->why);
	char *text = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	size_t length = text ? strlen(text) : 0;
	if (length == 0 || length + 2 > size) {
		length = 0;
	} else {
		memcpy(out, text, length);
		out[length++] = '\n';
		out[length] = '\0';
	}
	cJSON_free(text);
	return length;
}

int xvt_launch_result_write(const struct xvt_launch_result *r, const char *path)
{
	char text[1024];
	size_t length = xvt_launch_result_format(r, text, sizeof(text));
	if (length == 0) {
		return 0;
	}
	FILE *file = fopen(path, "wb");
	if (!file) {
		return 0;
	}
	int written = fwrite(text, 1, length, file) == length;
	return fclose(file) == 0 && written;
}
