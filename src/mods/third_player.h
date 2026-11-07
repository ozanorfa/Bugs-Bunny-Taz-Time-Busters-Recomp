/*
 * Third player input (third_player.c, plugin bbt.third-character.p3).
 */
#ifndef BBT_THIRD_PLAYER_H
#define BBT_THIRD_PLAYER_H

#include <stdint.h>

/* Guest address of the third input record (laid out like the game's input
 * slots, +0x00..+0x0D), refreshed once per game pad poll from seat 2; 0
 * while the p3 plugin has not been activated. Emulation thread. */
uint32_t bbt_p3_record(void);

#endif /* BBT_THIRD_PLAYER_H */
