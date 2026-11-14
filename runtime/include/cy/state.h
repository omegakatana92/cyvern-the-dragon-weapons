/* Save-state serialization for the Cyvern native runtime.
 *
 * A core state is a tagged, versioned, CRC-protected little-endian blob with
 * every piece of mutable board state: the SH-2 (registers, interrupt and sleep
 * latches, cycle counters), the SH7604 on-chip registers (DIVU, FRC), main and
 * cache RAM, NVRAM, sprite/tilemap/scroll/palette RAM and registers, RAM-based
 * tiles, the persistent sprite bitmap, palette brightness state, the hit unit,
 * the YMZ280B (all eight voices), RTC registers and the scheduler counters.
 * ROM-derived data is never stored; the state carries a fingerprint of the
 * ROM set instead and is rejected when it does not match. */
#ifndef CY_STATE_H
#define CY_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "cy/board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CY_STATE_MAGIC "CYVNCORE"
#define CY_STATE_VERSION 1u

uint32_t cy_crc32(uint32_t crc, const void *data, size_t size);
uint32_t cy_board_rom_fingerprint(const cy_board *board);
uint8_t *cy_state_save(const cy_board *board, size_t *size);
int cy_state_validate(const cy_board *board, const uint8_t *data, size_t size, char *err, size_t err_size);
int cy_state_load(cy_board *board, const uint8_t *data, size_t size, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif
#endif
