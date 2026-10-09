/* Prints what the engine decodes from a movie and its subtitle file, so that
 * another reader of the same files can be checked against the engine without
 * printing every pixel. Build with -DXVT_BUILD_TOOLS=ON; it links the engine
 * library.
 *
 * Usage: movie_dump ROOT frames NAME
 *        movie_dump ROOT subtitles NAME
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. NAME is a movie's name as the game asks for it (Opening, IMP1SND,
 * Flyby1a); the tool opens movies/NAME.smk or movies/NAME.txt as the movie
 * task does, without its Flyby1a stand-in for a missing network movie.
 *
 * frames opens the movie with Aeron's video player, the one the movie task
 * plays it with, paused and without an audio device, so the decode thread
 * fills the frame queue and drops the sound, and takes each decoded frame off
 * the queue's head in turn. After the sheet's first lines (kind, name, file):
 *
 *   container NAME
 *   video CODEC width=W height=H display=DWxDH rate=NUM/DEN
 *   audio CODEC rate=R channels=C      (or: audio none)
 *   duration_us D                      (-1 when unknown)
 *   frame I pts_us=P duration_us=Q rgba=HASH
 *   ...
 *   frames N
 *   error "TEXT"                       (only when the player fails)
 *
 * I counts from 1 as the player counts. HASH is the CRC-32 of the frame as
 * the player hands it on (polynomial 0xEDB88320, initial and final value
 * 0xFFFFFFFF, as zlib's crc32): W times H pixels of 4 bytes, red, green, blue,
 * alpha, rows top to bottom; it prints as 8 hex digits.
 *
 * subtitles reads movies/NAME.txt through movie_read_subtitle_cue, the movie
 * task's reader, one record per line, until the reader reports no number:
 *
 *   cue frame=F "LINE1" "LINE2" "LINE3"
 *   ...
 *   end cues=N
 *
 * A name with no subtitle file prints only the first lines and "missing".
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it. Exit
 * status: 0 when the sheet printed in full; 1 when the movie does not resolve,
 * the player fails, or a write fails; 2 for bad arguments. */
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aeron/video.h"
#include "asset_dump.h"
#include "video_internal.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/movie.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/storage/storage.h"

enum {
	PATH_CAPACITY = 1024,
	SUBTITLE_LINE_CAPACITY = 256,
	SUBTITLE_END = 0xFFFF,
};

static const uint32_t CRC32_POLYNOMIAL = 0xEDB88320u;

/* The CRC-32 of size bytes from bytes, as zlib's crc32 computes it. */
static uint32_t crc32_of(const uint8_t *bytes, size_t size)
{
	static uint32_t table[256];
	if (table[1] == 0) {
		for (uint32_t entry = 0; entry < 256; ++entry) {
			uint32_t value = entry;
			for (int bit = 0; bit < 8; ++bit) {
				value = (value & 1u) != 0
						? (value >> 1) ^
							  CRC32_POLYNOMIAL
						: value >> 1;
			}
			table[entry] = value;
		}
	}
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t index = 0; index < size; ++index) {
		crc = table[(crc ^ bytes[index]) & 0xFFu] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFu;
}

/* Prints the player's stream lines once its decode thread has opened the
 * file. */
static void print_info(const AeronVideoInfo *info)
{
	printf("container %s\n", info->container);
	if (info->has_video) {
		printf("video %s width=%d height=%d display=%dx%d rate=%d/%d\n",
		       info->video_codec, info->width, info->height,
		       info->display_width, info->display_height,
		       info->frame_rate_num, info->frame_rate_den);
	} else {
		printf("video none\n");
	}
	if (info->has_audio) {
		printf("audio %s rate=%d channels=%d\n", info->audio_codec,
		       info->audio_sample_rate, info->audio_channels);
	} else {
		printf("audio none\n");
	}
	printf("duration_us %lld\n", (long long)info->duration_us);
}

/* Takes every decoded frame off the paused player's queue, printing one line
 * each, and wakes the decode thread after each so it decodes on. Returns 0
 * after an "error" line when the player fails. */
static int drain_frames(AeronVideoPlayer *player)
{
	const size_t frame_bytes =
		(size_t)player->info.width * (size_t)player->info.height * 4u;
	uint64_t count = 0;
	SDL_LockMutex(player->lock);
	for (;;) {
		while (player->frame_count == 0 && !player->worker_eof &&
		       !player->worker_failed) {
			SDL_WaitCondition(player->condition, player->lock);
		}
		if (player->frame_count == 0) {
			break;
		}
		const AeronDecodedVideoFrame *frame =
			&player->frames[player->frame_head];
		printf("frame %llu pts_us=%lld duration_us=%lld "
		       "rgba=%08x\n",
		       (unsigned long long)frame->index,
		       (long long)frame->pts_us, (long long)frame->duration_us,
		       (unsigned int)crc32_of(frame->pixels, frame_bytes));
		player->frame_head =
			(player->frame_head + 1u) % player->queue_capacity;
		--player->frame_count;
		++count;
		SDL_BroadcastCondition(player->condition);
	}
	const int failed = player->worker_failed;
	SDL_UnlockMutex(player->lock);
	printf("frames %llu\n", (unsigned long long)count);
	if (failed) {
		printf("error ");
		asset_dump_quote(Aeron_VideoGetError(player), PATH_CAPACITY);
		printf("\n");
		return 0;
	}
	return 1;
}

/* The frames sheet for movie name. Returns 0 when it does not resolve or
 * the player fails. */
static int dump_frames(const char *name)
{
	char relative[PATH_CAPACITY];
	char path[PATH_CAPACITY];
	snprintf(relative, sizeof(relative), "movies/%s.smk", name);
	if (!asset_dump_print_header("frames", relative) ||
	    xvt_storage_resolve_asset(relative, path, sizeof(path)) != 1) {
		return 0;
	}
	AeronVideoOpenDesc desc = {0};
	desc.vfs = xvt_storage_vfs();
	desc.root = AERON_VFS_ROOT_ASSET;
	desc.path = path;
	desc.autoplay = 0;
	AeronVideoPlayer *player = Aeron_VideoOpen(&desc);
	if (player == NULL) {
		printf("error \"the player does not open\"\n");
		return 0;
	}
	SDL_LockMutex(player->lock);
	while (!player->worker_ready && !player->worker_failed) {
		SDL_WaitCondition(player->condition, player->lock);
	}
	const AeronVideoInfo info = player->info;
	const int ready = player->worker_ready;
	SDL_UnlockMutex(player->lock);
	int result = 0;
	if (ready) {
		print_info(&info);
		result = drain_frames(player);
	} else {
		printf("error ");
		asset_dump_quote(Aeron_VideoGetError(player), PATH_CAPACITY);
		printf("\n");
	}
	Aeron_VideoClose(player);
	return result;
}

/* The subtitles sheet for movie name: every record movie_read_subtitle_cue
 * reads from movies/NAME.txt. Returns 0 when the movie's file does not
 * resolve. */
static int dump_subtitles(const char *name)
{
	char relative[PATH_CAPACITY];
	snprintf(relative, sizeof(relative), "movies/%s.txt", name);
	char resolved[PATH_CAPACITY];
	if (!asset_dump_resolve(relative, resolved, sizeof(resolved))) {
		printf("kind subtitles\nname ");
		asset_dump_quote(relative, PATH_CAPACITY);
		printf("\nmissing\n");
		return 1;
	}
	if (!asset_dump_print_header("subtitles", relative)) {
		return 0;
	}
	g_movie_subtitle_file = file_open(relative, "r");
	if (g_movie_subtitle_file == NULL) {
		printf("missing\n");
		return 1;
	}
	char lines[3][SUBTITLE_LINE_CAPACITY];
	unsigned int count = 0;
	for (;;) {
		const unsigned int frame =
			movie_read_subtitle_cue(lines[0], lines[1], lines[2]);
		if (frame == SUBTITLE_END) {
			break;
		}
		printf("cue frame=%u", frame);
		for (int line = 0; line < 3; ++line) {
			printf(" ");
			asset_dump_quote(lines[line], SUBTITLE_LINE_CAPACITY);
		}
		printf("\n");
		++count;
	}
	file_close(g_movie_subtitle_file);
	g_movie_subtitle_file = NULL;
	printf("end cues=%u\n", count);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc != 4 || (strcmp(argv[2], "frames") != 0 &&
			  strcmp(argv[2], "subtitles") != 0)) {
		fprintf(stderr, "Usage: %s ROOT frames|subtitles NAME\n",
			argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	if (!asset_dump_bind_root("movie_dump", argv[1])) {
		return 1;
	}
	const int printed = strcmp(argv[2], "frames") == 0
				    ? dump_frames(argv[3])
				    : dump_subtitles(argv[3]);
	return !printed || fflush(stdout) != 0;
}
