/** @file saturn/common/loading.h A loading indicator while the game is busy.
 *
 * When the game hasn't updated the screen for a while (reading from the
 * disc, as a rule), the VBlank interrupt shows "LOADING" with moving dots
 * on the VDP2 overlay, in the border under the picture. */

#ifndef SATURN_LOADING_H
#define SATURN_LOADING_H

/* The game is updating the screen: takes the indicator away. Call it from
 * the video tick. */
extern void loading_alive(void);

/* From the VBlank interrupt: shows the indicator when the game has been
 * busy long enough. */
extern void loading_vblank(void);

#endif /* SATURN_LOADING_H */
