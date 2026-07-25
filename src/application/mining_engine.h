/**
 * mining_engine.h - Proof of Coherence mining engine
 *
 * Handles the main mining loop:
 * - Fetch current block from fullnode
 * - Generate PoC proof
 * - Submit proof to fullnode
 * - Track statistics
 *
 * Firmware A version: simplified PoC without silicon PUF or Chua HSC.
 */

#ifndef LINDBLAD_MINING_ENGINE_H
#define LINDBLAD_MINING_ENGINE_H

#include <Arduino.h>

// ==========================================
// MINING ENGINE STATE
// ==========================================

struct MiningStats {
    uint32_t proofs_generated;
    uint32_t proofs_submitted;
    uint32_t proofs_accepted;
    uint32_t proofs_rejected;
    uint32_t submit_errors;
    uint32_t current_block;
    uint32_t last_submit_ms;
    uint64_t total_hashes;
};

// ==========================================
// PUBLIC API
// ==========================================

/**
 * Initialize the mining engine.
 * Should be called after WiFi is connected.
 *
 * @param wallet Wallet address for rewards (e.g., "0x...")
 * @param fullnode_url Full URL of LindFullnode (e.g., "http://167.99.8.29:8080")
 * @return true if initialization succeeded
 */
bool mining_engine_init(const String& wallet, const String& fullnode_url);

/**
 * Start the mining loop.
 * Must be called after init.
 */
void mining_engine_start();

/**
 * Stop the mining loop.
 */
void mining_engine_stop();

/**
 * Main tick — call this from the main loop.
 * Handles all mining operations non-blocking.
 */
void mining_engine_tick();

/**
 * Check if mining is currently active.
 */
bool mining_engine_is_active();

/**
 * Get current mining statistics.
 */
MiningStats mining_engine_get_stats();

/**
 * Print mining stats to Serial (for debug).
 */
void mining_engine_print_stats();

#endif // LINDBLAD_MINING_ENGINE_H
