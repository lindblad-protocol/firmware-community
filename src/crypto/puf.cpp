/**
 * puf.cpp - SRAM PUF + BCH Fuzzy Extractor implementation
 *
 * Ported 1:1 from Lindblad Firmware v6.3 legacy.
 * Implements code-offset secure sketch using BCH(255,139,t=15).
 */

#include "puf.h"
#include "../../include/config.h"
#include "../../include/bch.h"

#include <Preferences.h>
#include <mbedtls/sha256.h>

// ============================================
// SRAM PUF STORAGE
// ============================================

// CRITICAL: __NOINIT_ATTR keeps this uninitialized across reboots
// The initial garbage state IS the PUF signature
__NOINIT_ATTR uint8_t puf_sram[PUF_SIZE];

// ============================================
// INTERNAL STATE
// ============================================

static bool s_enrolled = false;
static int s_enroll_count = 0;
static uint8_t s_puf_bytes[8];   // 8 bytes derived from hash for ECDSA seed
static bool s_bytes_ready = false;

// ============================================
// INTERNAL HELPERS (ported from v6.3)
// ============================================

static int pufCollectStable(const uint8_t* mask, int* idx, int maxn) {
    int n = 0;
    for (int bit = 0; bit < PUF_BITS && n < maxn; bit++) {
        if (mask[bit >> 3] & (1 << (bit & 7))) idx[n++] = bit;
    }
    return n;
}

static void pufReadStable(const uint8_t* puf, const int* idx, int n, unsigned char* bits) {
    for (int i = 0; i < n; i++) {
        int b = idx[i];
        bits[i] = (puf[b >> 3] >> (b & 7)) & 1;
    }
}

static void pufMakeNodeId(const unsigned char* msg, char* node_id_out, char* puf_hex_out) {
    // Pack BCH_K bits into bytes
    unsigned char packed[(BCH_K + 7) / 8];
    memset(packed, 0, sizeof(packed));
    for (int i = 0; i < BCH_K; i++) {
        if (msg[i]) packed[i >> 3] |= (1 << (i & 7));
    }

    // SHA-256 of the packed message
    unsigned char h[32];
    mbedtls_sha256(packed, sizeof(packed), h, 0);

    // NODE_ID format: LD + 5 hex chars
    snprintf(node_id_out, NODE_ID_LEN, "LD%02X%02X%X", h[0], h[1], (h[2] >> 4) & 0xF);

    // PUF hex: 16 hex chars (first 8 bytes of hash)
    snprintf(puf_hex_out, PUF_HEX_LEN, "%02X%02X%02X%02X%02X%02X%02X%02X",
             h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);

    // Store first 8 bytes for ECDSA seed
    memcpy(s_puf_bytes, h, 8);
    s_bytes_ready = true;
}

// ============================================
// PUBLIC API IMPLEMENTATION
// ============================================

bool puf_extract(char* node_id_out, char* puf_hex_out) {
    // Capture SRAM snapshot immediately
    static uint8_t snap[PUF_SIZE];
    memcpy(snap, puf_sram, PUF_SIZE);

    Preferences p;
    p.begin("pufbch", false);

#ifdef PUF_DEV_FORCE_REENROLL
    p.clear();
    p.end();
    strcpy(node_id_out, "LD-WIPED");
    Serial.println("[PUF] Enrollment WIPED (DEV mode). Re-flash without flag.");
    return false;
#endif

    bool done = p.getBool("done", false);
    static int idx[BCH_N];

    // === RECONSTRUCTION PATH ===
    if (done) {
        static uint8_t maskbuf[PUF_SIZE];
        static unsigned char helper[BCH_N], rbits[BCH_N], cw[BCH_N], msg[BCH_K];

        p.getBytes("mask", maskbuf, PUF_SIZE);
        p.getBytes("helper", helper, BCH_N);

        int n = pufCollectStable(maskbuf, idx, BCH_N);
        pufReadStable(snap, idx, n, rbits);

        // Compute codeword: c' = helper XOR puf_bits
        for (int i = 0; i < BCH_N; i++) cw[i] = helper[i] ^ rbits[i];

        // BCH decode corrects up to 15 errors
        bch_decode(cw, msg);

        char reconstructed_id[NODE_ID_LEN];
        char reconstructed_hex[PUF_HEX_LEN];
        pufMakeNodeId(msg, reconstructed_id, reconstructed_hex);

        // HYBRID IDENTITY RESOLUTION
        // Check if we have a cached identity from a previous stable boot
        String cached_id = p.getString("cached_id", "");
        String cached_hex = p.getString("cached_hex", "");

        Serial.printf("[PUF] Silicon reconstruction: %s\n", reconstructed_id);

        if (cached_id.length() > 0) {
            if (cached_id == String(reconstructed_id)) {
                // Cache matches reconstruction: perfect case
                strcpy(node_id_out, reconstructed_id);
                strcpy(puf_hex_out, reconstructed_hex);
                Serial.printf("[PUF] NODE_ID: %s (silicon + cache match)\n", node_id_out);
            } else {
                // Layout changed: cache wins (stable ID preserved)
                strcpy(node_id_out, cached_id.c_str());
                strcpy(puf_hex_out, cached_hex.c_str());
                Serial.printf("[PUF] WARN: silicon (%s) != cache (%s), using CACHED\n",
                              reconstructed_id, node_id_out);
            }
        } else {
            // First boot with cached mechanism: store current for future
            strcpy(node_id_out, reconstructed_id);
            strcpy(puf_hex_out, reconstructed_hex);
            p.putString("cached_id", reconstructed_id);
            p.putString("cached_hex", reconstructed_hex);
            Serial.printf("[PUF] NODE_ID: %s (silicon, cached for stability)\n", node_id_out);
        }

        p.end();
        s_enrolled = true;
        Serial.printf("[PUF] Hex: %s\n", puf_hex_out);
        return true;
    }

    // === ENROLLMENT PATH ===
    int count = p.getInt("count", 0);
    s_enroll_count = count;

    static uint8_t ref[PUF_SIZE];
    static uint8_t flips[PUF_BITS];

    // First enrollment cycle: save reference
    if (count == 0) {
        memcpy(ref, snap, PUF_SIZE);
        memset(flips, 0, sizeof(flips));
        p.putBytes("ref", ref, PUF_SIZE);
        p.putBytes("flips", flips, sizeof(flips));
        p.putInt("count", 1);
        p.end();
        s_enroll_count = 1;
        Serial.printf("[PUF] ENROLLMENT 1/%d. Power-cycle (unplug/replug 5s).\n", PUF_ENROLL_TARGET);
        strcpy(node_id_out, "LD-ENROLL");
        strcpy(puf_hex_out, "0000000000000000");
        return false;
    }

    // Subsequent cycles: count bit flips vs reference
    p.getBytes("ref", ref, PUF_SIZE);
    p.getBytes("flips", flips, sizeof(flips));

    for (int bit = 0; bit < PUF_BITS; bit++) {
        int rv = (ref[bit >> 3] >> (bit & 7)) & 1;
        int cv = (snap[bit >> 3] >> (bit & 7)) & 1;
        if (rv != cv) flips[bit]++;
    }

    count++;
    p.putBytes("flips", flips, sizeof(flips));
    p.putInt("count", count);
    s_enroll_count = count;

    if (count < PUF_ENROLL_TARGET) {
        p.end();
        Serial.printf("[PUF] ENROLLMENT %d/%d. Power-cycle again.\n", count, PUF_ENROLL_TARGET);
        strcpy(node_id_out, "LD-ENROLL");
        strcpy(puf_hex_out, "0000000000000000");
        return false;
    }

    // === FINALIZE ENROLLMENT ===
    Serial.println("[PUF] Finalizing enrollment (computing stable bits + BCH)...");
    Serial.flush();
    delay(100);

    static uint8_t maskbuf[PUF_SIZE];
    int n = 0;
    int stable = 0;

    // Try thresholds 0, 1, then 2 to find BCH_N stable bits
    // BCH(255,139,t=15) can correct up to 15 errors, so <=2 flips in 12 cycles is safe
    for (int thresh = 0; thresh <= 2 && n < BCH_N; thresh++) {
        memset(maskbuf, 0, PUF_SIZE);
        stable = 0;
        for (int bit = 0; bit < PUF_BITS; bit++) {
            if (flips[bit] <= thresh) {
                maskbuf[bit >> 3] |= (1 << (bit & 7));
                stable++;
            }
        }
        n = pufCollectStable(maskbuf, idx, BCH_N);
        Serial.printf("[PUF] threshold<=%d flips: %d usable bits\n", thresh, stable);
        Serial.flush();
    }

    if (n < BCH_N) {
        p.end();
        Serial.printf("[PUF] NOT ENOUGH stable bits even relaxed (%d < %d).\n", n, BCH_N);
        strcpy(node_id_out, "LD-ENROLL");
        strcpy(puf_hex_out, "0000000000000000");
        s_enroll_count = PUF_ENROLL_TARGET;
        return false;
    }

    // Generate random message, encode, XOR with reference to make helper
    static unsigned char rbits[BCH_N], msg[BCH_K], cw[BCH_N], helper[BCH_N];
    pufReadStable(ref, idx, n, rbits);
    for (int i = 0; i < BCH_K; i++) msg[i] = esp_random() & 1;
    bch_encode(msg, cw);
    for (int i = 0; i < BCH_N; i++) helper[i] = cw[i] ^ rbits[i];

    pufMakeNodeId(msg, node_id_out, puf_hex_out);

    // Persist helper data and mask
    p.putBytes("mask", maskbuf, PUF_SIZE);
    p.putBytes("helper", helper, BCH_N);
    p.putString("nodeid", node_id_out);
    p.putBool("done", true);
    p.end();

    s_enrolled = true;
    Serial.printf("[PUF] ENROLLMENT COMPLETE. NODE_ID: %s (%d stable bits)\n", node_id_out, stable);
    Serial.printf("[PUF] Hex: %s\n", puf_hex_out);
    return true;
}

int puf_get_enroll_count() {
    return s_enroll_count;
}

bool puf_is_enrolled() {
    return s_enrolled;
}

bool puf_get_bytes(uint8_t* out_bytes) {
    if (!s_bytes_ready) return false;
    memcpy(out_bytes, s_puf_bytes, 8);
    return true;
}

void puf_dev_reset() {
    Preferences p;
    p.begin("pufbch", false);
    p.clear();
    p.end();
    s_enrolled = false;
    s_enroll_count = 0;
    s_bytes_ready = false;
    Serial.println("[PUF] Enrollment reset (dev mode)");
}
