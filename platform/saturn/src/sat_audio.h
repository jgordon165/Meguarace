/* Saturn sound: a PCM stream (the videos' soundtracks) on the SCSP. */
#ifndef SAT_AUDIO_H
#define SAT_AUDIO_H

#include <stddef.h>
#include <stdint.h>

void sat_audio_init(void);

/* A stream of 8-bit unsigned mono PCM at `rate` Hz, as HNM soundtracks are. */
void     sat_stream_begin(int rate);
/* Queues n samples. Returns how many were queued (fewer if the ring is full). */
size_t   sat_stream_push(const uint8_t* pcm, size_t n);
/* Starts playing what has been queued (call once, after some is queued). */
void     sat_stream_start(void);
void     sat_stream_stop(void);
uint32_t sat_stream_queued(void);   /* samples queued since begin */
uint32_t sat_stream_played(void);   /* samples played since start (estimated) */
int      sat_stream_rate(void);
size_t   sat_stream_space(void);    /* samples that can be queued now */

#endif
