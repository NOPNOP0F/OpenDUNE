/** @file saturn/common/loading.h A loading indicator while the game is busy.
 *
 * When the CD has been read most of the last half second, or the game
 * hasn't updated the screen for a while, the VBlank interrupt shows
 * "LOADING" with moving dots on the VDP2 overlay, in the border under the
 * picture. */

#ifndef SATURN_LOADING_H
#define SATURN_LOADING_H

/* The game is updating the screen. Call it from the video tick. */
extern void loading_alive(void);

/* Something changed on screen. Call it when the picture is updated. */
extern void loading_screen_changed(void);

/* The CD is being read (1) or not (0). */
extern void loading_disc(int reading);

/* From the VBlank interrupt: shows the indicator when the game has been
 * busy long enough. */
extern void loading_vblank(void);

#endif /* SATURN_LOADING_H */
