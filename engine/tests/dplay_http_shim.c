/* Aeron's thread, lock and clock for a test program that builds
 * aeron/src/compat/dplay_http.c alone, without the rest of Aeron or SDL:
 * the lobby pin check builds that file against each system's own curl.
 * POSIX threads outside Windows, the Windows API on it. DpWake, which wakes
 * the game's network loop, does nothing here: the test polls DpHttp_Take. */
#ifndef _WIN32
/* clock_gettime and CLOCK_MONOTONIC under -std=c99. */
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdlib.h>

#include "aeron/sync.h"
#include "aeron/time.h"

#ifdef _WIN32
#include <windows.h>

struct AeronMutex {
	CRITICAL_SECTION section;
};

struct AeronThread {
	HANDLE handle;
	AeronThreadFunc function;
	void *user;
	int result;
};

static DWORD WINAPI thread_start(LPVOID data)
{
	AeronThread *thread = data;
	thread->result = thread->function(thread->user);
	return 0;
}

AeronThread *Aeron_ThreadCreate(const char *name, AeronThreadFunc function,
				void *user)
{
	(void)name;
	AeronThread *thread = calloc(1, sizeof(*thread));
	if (!thread) {
		return NULL;
	}
	thread->function = function;
	thread->user = user;
	thread->handle = CreateThread(NULL, 0, thread_start, thread, 0, NULL);
	if (!thread->handle) {
		free(thread);
		return NULL;
	}
	return thread;
}

int Aeron_ThreadJoin(AeronThread *thread)
{
	WaitForSingleObject(thread->handle, INFINITE);
	CloseHandle(thread->handle);
	int result = thread->result;
	free(thread);
	return result;
}

AeronMutex *Aeron_MutexCreate(void)
{
	AeronMutex *mutex = calloc(1, sizeof(*mutex));
	if (mutex) {
		InitializeCriticalSection(&mutex->section);
	}
	return mutex;
}

void Aeron_MutexLock(AeronMutex *mutex)
{
	EnterCriticalSection(&mutex->section);
}

void Aeron_MutexUnlock(AeronMutex *mutex)
{
	LeaveCriticalSection(&mutex->section);
}

void Aeron_MutexDestroy(AeronMutex *mutex)
{
	DeleteCriticalSection(&mutex->section);
	free(mutex);
}

uint64_t Aeron_NowUs(void)
{
	LARGE_INTEGER count, frequency;
	QueryPerformanceCounter(&count);
	QueryPerformanceFrequency(&frequency);
	return (uint64_t)(count.QuadPart / frequency.QuadPart * 1000000 +
			  count.QuadPart % frequency.QuadPart * 1000000 /
				  frequency.QuadPart);
}

#else
#include <pthread.h>
#include <time.h>

struct AeronMutex {
	pthread_mutex_t mutex;
};

struct AeronThread {
	pthread_t handle;
	AeronThreadFunc function;
	void *user;
	int result;
};

static void *thread_start(void *data)
{
	AeronThread *thread = data;
	thread->result = thread->function(thread->user);
	return NULL;
}

AeronThread *Aeron_ThreadCreate(const char *name, AeronThreadFunc function,
				void *user)
{
	(void)name;
	AeronThread *thread = calloc(1, sizeof(*thread));
	if (!thread) {
		return NULL;
	}
	thread->function = function;
	thread->user = user;
	if (pthread_create(&thread->handle, NULL, thread_start, thread)) {
		free(thread);
		return NULL;
	}
	return thread;
}

int Aeron_ThreadJoin(AeronThread *thread)
{
	pthread_join(thread->handle, NULL);
	int result = thread->result;
	free(thread);
	return result;
}

AeronMutex *Aeron_MutexCreate(void)
{
	AeronMutex *mutex = calloc(1, sizeof(*mutex));
	if (mutex && pthread_mutex_init(&mutex->mutex, NULL)) {
		free(mutex);
		return NULL;
	}
	return mutex;
}

void Aeron_MutexLock(AeronMutex *mutex) { pthread_mutex_lock(&mutex->mutex); }

void Aeron_MutexUnlock(AeronMutex *mutex)
{
	pthread_mutex_unlock(&mutex->mutex);
}

void Aeron_MutexDestroy(AeronMutex *mutex)
{
	pthread_mutex_destroy(&mutex->mutex);
	free(mutex);
}

uint64_t Aeron_NowUs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
}
#endif

void DpWake(void) {}
