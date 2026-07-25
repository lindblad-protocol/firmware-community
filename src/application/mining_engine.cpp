/**
 * mining_engine.cpp - Proof of Coherence mining implementation
 *
 * Firmware A simplified PoC:
 * - SHA-256 based nonce search
 * - Difficulty adjustment via leading zeros
 * - HTTP POST to fullnode /api/v1/mining/proof
 * - No PUF, no Chua HSC (those are Firmware B exclusive)
 *
 * Main loop is non-blocking, called from Arduino loop() every tick.
 */

#include "application/mining_engine.h"
#include "config.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>

// ==========================================
// INTERNAL STATE
// ==========================================

static bool s_initialized = false;
static bool s_active = false;
static String s_wallet;
static String s_fullnode_url;
static MiningStats s_stats;

// Mining state machine
enum MiningState {
    STATE_IDLE,
    STATE_FETCH_BLOCK,
    STATE_MINING,
    STATE_SUBMIT,
    STATE_WAIT_NEXT_CYCLE,
};

static MiningState s_state = STATE_IDLE;
static unsigned long s_state_start_ms = 0;
static unsigned long s_next_action_ms = 0;

// Current mining work
static String s_current_block_hash = "";
static uint32_t s_current_nonce = 0;
static uint32_t s_current_difficulty = POC_DIFFICULTY_INITIAL;
static String s_found_hash = "";
static uint32_t s_found_nonce = 0;

// ==========================================
// FORWARD DECLARATIONS
// ==========================================

static void state_fetch_block();
static void state_mining();
static void state_submit();
static void reset_mining_state();
static bool sha256_hex(const String& input, String& output);
static bool check_difficulty(const String& hash, uint32_t difficulty);
static bool http_get_json(const String& endpoint, JsonDocument& doc);
static bool http_post_json(const String& endpoint, JsonDocument& payload, JsonDocument& response);

// ==========================================
// PUBLIC API IMPLEMENTATION
// ==========================================

bool mining_engine_init(const String& wallet, const String& fullnode_url) {
    if (wallet.length() == 0) {
        Serial.println("[MINING] ERROR: wallet address required");
        return false;
    }

    if (fullnode_url.length() == 0) {
        Serial.println("[MINING] ERROR: fullnode URL required");
        return false;
    }

    s_wallet = wallet;
    s_fullnode_url = fullnode_url;

    // Reset stats
    memset(&s_stats, 0, sizeof(s_stats));

    s_initialized = true;

    Serial.println("[MINING] Engine initialized");
    Serial.print("[MINING] Wallet: ");
    Serial.println(s_wallet);
    Serial.print("[MINING] Fullnode: ");
    Serial.println(s_fullnode_url);
    Serial.print("[MINING] Initial difficulty: ");
    Serial.println(s_current_difficulty);

    return true;
}

void mining_engine_start() {
    if (!s_initialized) {
        Serial.println("[MINING] ERROR: not initialized");
        return;
    }

    s_active = true;
    s_state = STATE_FETCH_BLOCK;
    s_state_start_ms = millis();

    Serial.println("[MINING] Mining STARTED");
}

void mining_engine_stop() {
    s_active = false;
    s_state = STATE_IDLE;
    Serial.println("[MINING] Mining STOPPED");
}

void mining_engine_tick() {
    if (!s_active) {
        return;
    }

    unsigned long now = millis();

    // Wait for scheduled next action
    if (s_next_action_ms > 0 && now < s_next_action_ms) {
        return;
    }

    switch (s_state) {
        case STATE_IDLE:
            break;

        case STATE_FETCH_BLOCK:
            state_fetch_block();
            break;

        case STATE_MINING:
            state_mining();
            break;

        case STATE_SUBMIT:
            state_submit();
            break;

        case STATE_WAIT_NEXT_CYCLE:
            // Wait period completed, start new cycle
            s_state = STATE_FETCH_BLOCK;
            s_next_action_ms = 0;
            break;
    }
}

bool mining_engine_is_active() {
    return s_active;
}

MiningStats mining_engine_get_stats() {
    return s_stats;
}

void mining_engine_print_stats() {
    Serial.println();
    Serial.println("--- MINING STATS ---");
    Serial.print("Active: ");
    Serial.println(s_active ? "YES" : "NO");
    Serial.print("Current block: ");
    Serial.println(s_stats.current_block);
    Serial.print("Proofs generated: ");
    Serial.println(s_stats.proofs_generated);
    Serial.print("Proofs submitted: ");
    Serial.println(s_stats.proofs_submitted);
    Serial.print("Proofs accepted: ");
    Serial.println(s_stats.proofs_accepted);
    Serial.print("Proofs rejected: ");
    Serial.println(s_stats.proofs_rejected);
    Serial.print("Submit errors: ");
    Serial.println(s_stats.submit_errors);
    Serial.print("Total hashes: ");
    Serial.println((unsigned long)s_stats.total_hashes);
    Serial.println("--------------------");
    Serial.println();
}

// ==========================================
// STATE MACHINE IMPLEMENTATION
// ==========================================

/**
 * STATE_FETCH_BLOCK: get latest block from fullnode
 */
static void state_fetch_block() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MINING] WiFi disconnected, retrying in 10s...");
        s_next_action_ms = millis() + 10000;
        return;
    }

    Serial.println("[MINING] Fetching latest block...");

    JsonDocument doc;
    bool ok = http_get_json("/api/v1/chain/latest", doc);

    if (!ok) {
        Serial.println("[MINING] Failed to fetch block, retrying in 10s...");
        s_stats.submit_errors++;
        s_next_action_ms = millis() + 10000;
        return;
    }

    // Extract block info
    s_stats.current_block = doc["block_height"] | 0;
    s_current_block_hash = doc["block_hash"].as<String>();
    s_current_difficulty = doc["difficulty"] | POC_DIFFICULTY_INITIAL;

    if (s_current_block_hash.length() == 0) {
        // Fullnode may not have proper response yet, use fallback
        s_current_block_hash = String(millis(), HEX);
        Serial.println("[MINING] Using fallback block hash (fullnode not ready)");
    }

    Serial.print("[MINING] Block #");
    Serial.print(s_stats.current_block);
    Serial.print(" | Difficulty: ");
    Serial.print(s_current_difficulty);
    Serial.print(" | Hash: ");
    Serial.println(s_current_block_hash.substring(0, 16) + "...");

    // Prepare for mining
    s_current_nonce = 0;
    s_found_hash = "";
    s_state = STATE_MINING;
    s_state_start_ms = millis();
    s_next_action_ms = 0;
}

/**
 * STATE_MINING: search for a valid nonce
 * Non-blocking: hashes a batch each tick to allow other operations
 */
static void state_mining() {
    // How many hashes to compute per tick (adjust for responsiveness)
    const uint32_t HASHES_PER_TICK = 500;

    // Timeout check
    if (millis() - s_state_start_ms > POC_TIMEOUT_MS) {
        Serial.println("[MINING] Mining timeout, retrying with new block...");
        s_state = STATE_FETCH_BLOCK;
        return;
    }

    // Batch of hashes
    for (uint32_t i = 0; i < HASHES_PER_TICK; i++) {
        // Build proof input: block_hash + wallet + nonce
        String input = s_current_block_hash + s_wallet + String(s_current_nonce);

        String hash;
        if (!sha256_hex(input, hash)) {
            Serial.println("[MINING] SHA256 error");
            s_state = STATE_FETCH_BLOCK;
            return;
        }

        s_stats.total_hashes++;

        // Check difficulty
        if (check_difficulty(hash, s_current_difficulty)) {
            // FOUND!
            s_found_hash = hash;
            s_found_nonce = s_current_nonce;

            unsigned long elapsed = millis() - s_state_start_ms;

            Serial.println();
            Serial.print("[MINING] ✅ FOUND PROOF! Nonce=");
            Serial.print(s_found_nonce);
            Serial.print(" Hash=");
            Serial.println(s_found_hash.substring(0, 24) + "...");
            Serial.print("[MINING] Time: ");
            Serial.print(elapsed);
            Serial.print("ms | Hashes: ");
            Serial.println(s_current_nonce + 1);
            Serial.println();

            s_stats.proofs_generated++;
            s_state = STATE_SUBMIT;
            s_next_action_ms = 0;
            return;
        }

        s_current_nonce++;

        // Sanity limit
        if (s_current_nonce >= POC_MAX_ITERATIONS) {
            Serial.println("[MINING] Max iterations reached, restart cycle");
            s_state = STATE_FETCH_BLOCK;
            return;
        }
    }
}

/**
 * STATE_SUBMIT: send found proof to fullnode
 */
static void state_submit() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MINING] WiFi lost during submit, retrying...");
        s_next_action_ms = millis() + 5000;
        return;
    }

    Serial.println("[MINING] Submitting proof to fullnode...");

    // Build payload
    JsonDocument payload;
    payload["block_height"] = s_stats.current_block;
    payload["block_hash"] = s_current_block_hash;
    payload["wallet"] = s_wallet;
    payload["nonce"] = s_found_nonce;
    payload["proof_hash"] = s_found_hash;
    payload["difficulty"] = s_current_difficulty;
    payload["firmware"] = "A";
    payload["version"] = LINDBLAD_FIRMWARE_VERSION;

    JsonDocument response;
    bool ok = http_post_json("/api/v1/mining/proof", payload, response);

    s_stats.proofs_submitted++;

    if (!ok) {
        Serial.println("[MINING] Submit failed, will retry next cycle");
        s_stats.submit_errors++;
    } else {
        bool accepted = response["accepted"] | false;

        if (accepted) {
            s_stats.proofs_accepted++;
            Serial.println("[MINING] ✅ Proof ACCEPTED");

            // Check for reward info
            if (response["reward"].is<float>()) {
                float reward = response["reward"];
                Serial.print("[MINING] 💰 Reward: ");
                Serial.print(reward, 6);
                Serial.println(" PYCO");
            }
        } else {
            s_stats.proofs_rejected++;
            String reason = response["reason"] | "unknown";
            Serial.print("[MINING] ❌ Proof rejected: ");
            Serial.println(reason);
        }
    }

    // Wait before next cycle
    Serial.print("[MINING] Cycle complete. Next in ");
    Serial.print(POC_SUBMIT_INTERVAL_MS / 1000);
    Serial.println("s");

    s_state = STATE_WAIT_NEXT_CYCLE;
    s_next_action_ms = millis() + POC_SUBMIT_INTERVAL_MS;
}

// ==========================================
// UTILITIES
// ==========================================

/**
 * SHA-256 hash to hex string using mbedtls (built-in ESP32)
 */
static bool sha256_hex(const String& input, String& output) {
    unsigned char hash[32];
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_update_ret(&ctx, (const unsigned char*)input.c_str(), input.length()) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_finish_ret(&ctx, hash) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);

    // Convert to hex string
    char hex_buf[65];
    for (int i = 0; i < 32; i++) {
        snprintf(hex_buf + (i * 2), 3, "%02x", hash[i]);
    }
    hex_buf[64] = '\0';
    output = String(hex_buf);

    return true;
}

/**
 * Check if hash meets difficulty (N leading zeros in hex)
 */
static bool check_difficulty(const String& hash, uint32_t difficulty) {
    for (uint32_t i = 0; i < difficulty; i++) {
        if (hash.charAt(i) != '0') {
            return false;
        }
    }
    return true;
}

/**
 * HTTP GET returning JSON response
 */
static bool http_get_json(const String& endpoint, JsonDocument& doc) {
    HTTPClient http;
    String url = s_fullnode_url + endpoint;

    http.setTimeout(DEFAULT_FULLNODE_TIMEOUT_MS);
    http.begin(url);

    int code = http.GET();

    if (code != 200) {
        Serial.print("[HTTP] GET ");
        Serial.print(url);
        Serial.print(" failed: ");
        Serial.println(code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[HTTP] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    return true;
}

/**
 * HTTP POST with JSON payload, returns JSON response
 */
static bool http_post_json(const String& endpoint, JsonDocument& payload, JsonDocument& response) {
    HTTPClient http;
    String url = s_fullnode_url + endpoint;

    http.setTimeout(DEFAULT_FULLNODE_TIMEOUT_MS);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String body;
    serializeJson(payload, body);

    int code = http.POST(body);

    if (code < 200 || code >= 300) {
        Serial.print("[HTTP] POST ");
        Serial.print(url);
        Serial.print(" failed: ");
        Serial.println(code);
        http.end();
        return false;
    }

    String resp_body = http.getString();
    http.end();

    DeserializationError err = deserializeJson(response, resp_body);
    if (err) {
        Serial.print("[HTTP] JSON parse error: ");
        Serial.println(err.c_str());
        return false;
    }

    return true;
}
