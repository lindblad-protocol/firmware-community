/**
 * puf.h - SRAM PUF + BCH Fuzzy Extractor - LCP Level 1
 *
 * Silicon-based cryptographic identity from ESP32-S3 SRAM cells.
 * Ported 1:1 from Lindblad Firmware v6.3 legacy.
 *
 * Uses:
 *   - SRAM PUF: 256 bytes captured at power-on (__NOINIT_ATTR)
 *   - BCH(255,139,t=15) fuzzy extractor for error correction
 *   - 12-cycle enrollment to identify stable bits
 *   - Helper data stored in NVS for reconstruction
 *
 * Security guarantee: Silicon identity cannot be cloned or emulated.
 * Same physical chip always produces same PUF value.
 */

#ifndef LINDBLAD_PUF_H
#define LINDBLAD_PUF_H

#include <Arduino.h>

#define PUF_SIZE 256
#define PUF_BITS (PUF_SIZE * 8)
#define PUF_ENROLL_TARGET 12
#define PUF_HEX_LEN 17    // 16 hex chars + null terminator
#define NODE_ID_LEN 12    // "LDxxxxxxx" + null terminator

// Global SRAM PUF buffer - CRITICAL: must be __NOINIT_ATTR
// Declared in puf.cpp with __NOINIT_ATTR
extern uint8_t puf_sram[PUF_SIZE];

/**
 * Extract PUF identity.
 *
 * Behavior:
 *   - If already enrolled: reconstructs identity via BCH decode
 *   - If not enrolled: begins/continues 12-cycle enrollment
 *
 * @param node_id_out Output buffer for node ID (min 12 bytes: "LDxxxxxxx\0")
 * @param puf_hex_out Output buffer for PUF hex (min 17 bytes)
 * @return true if identity is ready, false if still enrolling
 */
bool puf_extract(char* node_id_out, char* puf_hex_out);

/**
 * Get current enrollment progress (0-12).
 * Useful for user feedback during enrollment cycles.
 */
int puf_get_enroll_count();

/**
 * Check if enrollment is complete.
 */
bool puf_is_enrolled();

/**
 * Get PUF bytes (8 bytes derived from hash) for ECDSA key derivation.
 * @param out_bytes Output buffer (must be at least 8 bytes)
 * @return true if PUF ready, false otherwise
 */
bool puf_get_bytes(uint8_t* out_bytes);

/**
 * DEV ONLY: Reset enrollment (wipe NVS).
 * Requires PUF_DEV_MODE flag in config.
 */
void puf_dev_reset();

#endif // LINDBLAD_PUF_H
