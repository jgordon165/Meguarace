/* HNM video playback on the Saturn, streamed from MEGARACE.DAT. */
#ifndef SAT_HNM_H
#define SAT_HNM_H

enum { SAT_HNM_DONE = 0, SAT_HNM_SKIPPED = 1, SAT_HNM_SKIP_ALL = 2 };

/* Called while a video plays (after sat_pad_update): return 0 to go on,
 * SAT_HNM_SKIPPED to end this video, SAT_HNM_SKIP_ALL to end a sequence. */
typedef int (*sat_hnm_poll)(void);

/* Plays archive entry `entry`. Returns SAT_HNM_* or a negative error. */
int sat_hnm_play(int entry, sat_hnm_poll poll);

/* What the last play reached (for the status screen). */
typedef struct {
    int frames, errors, late, rate, stalls;
} sat_hnm_stats;
const sat_hnm_stats* sat_hnm_last(void);

#endif
