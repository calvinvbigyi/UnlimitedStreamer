/**
 * Copyright (c) 2006-2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "../../Task.h"
#include "../include/libspotify/api.h"

#include "audio.h"


/* --- Data --- */
/// The application key is specific to each project, and allows Spotify
/// to produce statistics on how our service is used.
extern const uint8_t g_appkey[];
/// The size of the application key.
extern const size_t g_appkey_size;

/// The output queue for audo data
static audio_fifo_t g_audiofifo;
/// Synchronization mutex for the main thread
static pthread_mutex_t g_notify_mutex;
/// Synchronization condition variable for the main thread
static pthread_cond_t g_notify_cond;
/// Synchronization variable telling the main thread to process events
static int g_notify_do;
/// Non-zero when a track has ended and the jukebox has not yet started a new one
static int g_playback_done;
/// The global session handle
static sp_session *g_sess;
/// Handle to the curren track
static sp_track *g_currenttrack;
/// global trackUri as a string
static char * g_trackUri;
/// global flag for song playing status
static int play_status;

static int counter = 0;
/* ---------------------------  SESSION CALLBACKS  ------------------------- */
/**
 * This callback is called when an attempt to login has succeeded or failed.
 *
 * @sa sp_session_callbacks#logged_in
 */
static void logged_in(sp_session *sess, sp_error error)
{
	sp_link *link;
	if (SP_ERROR_OK != error)
	{
		fprintf(stderr, "Login failed: %s\n",
		        sp_error_message(error));
		return;
	}

	printf("Loading track\n");

	if (g_trackUri != 0)
	{
		printf("New track uri: %s\n", g_trackUri);
		link = sp_link_create_from_string(g_trackUri);
		sp_track_add_ref(g_currenttrack = sp_link_as_track(link));
		sp_link_release(link);
		if (sp_track_error(g_currenttrack) == SP_ERROR_OK)
		{
			printf("Now playing \"%s\"...\n", sp_track_name(g_currenttrack));
			fflush(stdout);

			sp_session_player_load(g_sess, g_currenttrack);
			sp_session_player_play(g_sess, 1);
		}
		else
		{
			printf("%s\n", sp_error_message(sp_track_error(g_currenttrack)));
		}

	}
	else
	{
		printf("Please enter a valid spotify track uri\n");
	}


	/* Track not loaded? Then we need to wait for the metadata to
	       load before we can start playback (see below) */
}

/**
 * Callback called when libspotify has new metadata available
 *
 * @sa sp_session_callbacks#metadata_updated
 */
static void metadata_updated(sp_session *sess)
{
	puts("Metadata updated, trying to start playback");

	if (sp_track_error(g_currenttrack) != SP_ERROR_OK)
		return;

	printf("Now playing \"%s\"...\n", sp_track_name(g_currenttrack));
	fflush(stdout);
	sp_session_player_load(g_sess, g_currenttrack);
	sp_session_player_play(g_sess, 1);

}

/**
 * This callback is called from an internal libspotify thread to ask
 * us to reiterate the main loop.
 *
 * We notify the main thread using a condition variable and a protected variable.
 *
 * @sa sp_session_callbacks#notify_main_thread
 */
static void notify_main_thread(sp_session *sess)
{
	pthread_mutex_lock(&g_notify_mutex);
	g_notify_do = 1;
	pthread_cond_signal(&g_notify_cond);
	pthread_mutex_unlock(&g_notify_mutex);
}

/**
 * This callback is used from libspotify whenever there is PCM data available.
 *
 * @sa sp_session_callbacks#music_delivery
 */
static int music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{

	audio_fifo_t *af = &g_audiofifo;
	audio_fifo_data_t *afd;
	size_t s;

	if (num_frames == 0)
		return 0; // Audio discontinuity, do nothing

	pthread_mutex_lock(&af->mutex);

	/* Buffer one second of audio */
	if (af->qlen > format->sample_rate)
	{
		pthread_mutex_unlock(&af->mutex);

		return 0;
	}

	s = num_frames * sizeof(int16_t) * format->channels;

	afd = (audio_fifo_data_t *) malloc(sizeof(audio_fifo_data_t) + s);

	memcpy(afd->samples, frames, s);

	// let's to up sampling here
	// int16_t pSrc;
	// int16_t pDest[0];

	// pSrc = (int16_t *) malloc(sizeof(int16_t) * num_frames * format->channels);

	// memcpy(pSrc, frames, s);

	// // printf("number of frames: %d\n", num_frames);
	// // printf("sample rates %d\n:", format->sample_rate);
	// size_t src_i, dest_i, new_s, left;
	// // New size for the upsampled pcm bytes
	// new_s = num_frames * sizeof(int16_t) * format->channels * 48000 / 44100;

	// for (dest_i = 0; dest_i < new_s; dest_i++)
	// {
	// 	src_i = dest_i * 44100 / 48000;
	// 	left = pSrc[src_i];
	// 	pDest[dest_i] = left;
	// }


	// memcpy(afd->samples, pDest, new_s);
	afd->nsamples = num_frames;
	// Device right now needs to play audio in 48000Hz sample rate
	// Configure the sample rate to be 48000Hz
	afd->rate = 48000;
	afd->channels = format->channels;

	TAILQ_INSERT_TAIL(&af->q, afd, link);
	af->qlen += num_frames;

	pthread_cond_signal(&af->cond);
	pthread_mutex_unlock(&af->mutex);

	return num_frames;
}


/**
 * This callback is used from libspotify when the current track has ended
 *
 * @sa sp_session_callbacks#end_of_track
 */
static void end_of_track(sp_session *sess)
{
	pthread_mutex_lock(&g_notify_mutex);
	g_playback_done = 1;

	pthread_cond_signal(&g_notify_cond);
	pthread_mutex_unlock(&g_notify_mutex);
}

/**
 * Notification that some other connection has started playing on this account.
 * Playback has been stopped.
 *
 * @sa sp_session_callbacks#play_token_lost
 */
static void play_token_lost(sp_session *sess)
{
	audio_fifo_flush(&g_audiofifo);

	if (g_currenttrack != NULL)
	{
		sp_session_player_unload(g_sess);
		g_currenttrack = NULL;
	}
}

static void log_message(sp_session *session, const char *msg)
{
	puts(msg);
}

static sp_session_callbacks callbacks;
/* -------------------------  END SESSION CALLBACKS  ----------------------- */


/**
 * A track has ended. Remove it from the playlist.
 *
 * Called from the main loop when the music_delivery() callback has set g_playback_done.
 */
static void track_ended(void)
{
	if (g_currenttrack != NULL)
	{
		sp_session_player_unload(g_sess);
		sp_track_release(g_currenttrack);
		g_currenttrack = NULL;
	}
}

/**
 * A track has been stopped.
 * Release the current track
 */
void stop_track(void)
{
	if (g_currenttrack != NULL)
	{
		sp_session_player_unload(g_sess);
		sp_track_release(g_currenttrack);
		g_currenttrack = NULL;
	}
}

/**
 * A track has been paused.
 * sp_session_player_play set the flag to 0 to pause music, the global session will keep tracks where 
 * the track is paused.
 */
void pause_track(void)
{
	if (g_currenttrack != NULL  && g_sess != NULL)
	{
		printf("Music Paused!\n");
		sp_session_player_play(g_sess, 0);
	}
}

/**
 * A track has been resumed.
 * sp_session_player_play set the flag to 0 to pause music, the global session will keep tracks where 
 * the track is resumed.
 */

void resume_track(void)
{
	if (g_currenttrack != NULL && g_sess != NULL)
	{
		printf("Music Resumed!\n");
		sp_session_player_play(g_sess, 1);
	}
}


/**
 * Main method that contains the main event loop. 
 *
 * 
 */

int libspotify_play(const char * _userName, const char * _passWord, char * trackUri, Task * pTask)
{
	sp_session *sp;
	sp_session_config spConfig;
	sp_error err;

	memset(&spConfig, 0, sizeof(spConfig));

	if (!_userName || !_passWord) {
		printf("Please input your Spotify UserName and Password\n");
		return 0;
	}
	const char *username = _userName;
	const char *password = _passWord;
	callbacks.logged_in = &logged_in;
	callbacks.notify_main_thread = &notify_main_thread;
	callbacks.music_delivery = &music_delivery;
	callbacks.metadata_updated = &metadata_updated;
	callbacks.play_token_lost = &play_token_lost;
	callbacks.log_message = &log_message;
	callbacks.end_of_track = &end_of_track;

	spConfig.api_version = SPOTIFY_API_VERSION;
	spConfig.cache_location = "tmp";
	spConfig.settings_location = "tmp";
	spConfig.application_key = g_appkey;
	spConfig.application_key_size = g_appkey_size; // Set in main()
	spConfig.user_agent = "VBTX";
	spConfig.callbacks = &callbacks;

	// audio_init should only be called once, because audio_init creates a thread to create alsa handlers 
	// to directly use speaker hardware and getting stream audio data. There's no need to init it twice.
	if (counter == 0)
	{
		audio_init(&g_audiofifo);
		counter = 1;
	}
	/* Create session */
	err = sp_session_create(&spConfig, &sp);
	printf("create session seg fault\n");
	if (SP_ERROR_OK != err)
	{
		fprintf(stderr, "Unable to create session: %s\n",
		        sp_error_message(err));
		return 0;
	}

	sp_session_login(sp, username, password, 0, NULL);

	g_sess = sp;
	int next_timeout = 0;
	play_status = 0;
	pthread_mutex_init(&g_notify_mutex, NULL);
	pthread_cond_init(&g_notify_cond, NULL);
	pthread_mutex_lock(&g_notify_mutex);
	g_trackUri = trackUri;
	pthread_cond_signal(&g_notify_cond);
	pthread_mutex_unlock(&g_notify_mutex);
	for (;;) {
		if (pTask->shouldStop())
		{
			stop_track();
			g_playback_done = 0;
			g_trackUri = NULL;
			play_status = 1;
			printf("Music Stopped!\n");
			printf("Releasing session...\n");
			sp_session_release(g_sess);
			printf("Session released!\n");
			break;
		}

		while (g_trackUri == 0)
		{
			pthread_cond_wait(&g_notify_cond, &g_notify_mutex);
		}
		g_notify_do = 0;

		if (g_playback_done)
		{
			track_ended();
			g_playback_done = 0;
			g_trackUri = NULL;
			play_status = 2;
			printf("Music is done\n");
			printf("Releasing session...\n");
			sleep(5);
			bool k = g_sess == NULL;
			printf("global session is not null? %d\n", k);
			sp_session_release(g_sess);
			printf("Session released!\n");
			break;
		}

		do
		{
			sp_session_process_events(g_sess, &next_timeout);
		} while (next_timeout == 0);
	}

	return play_status;
}