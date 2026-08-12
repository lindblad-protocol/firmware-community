/**
 * mining_engine.cpp - Community Mining Engine (Firmware A)
 *
 * Adapted for real LindFullnode protocol:
 * - Fetches block from /block/latest
 * - Submits proofs to /api/v1/community-mining/submit
 * - Rewards: 0.01 PYCO per valid proof (backend-defined, ~14 PYCO/day)
 * - Rate limit: 60 seconds between proofs per device
 */

#include "application/mining_engine.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
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
static String s_device_id;
static MiningStats s_stats;

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

static String s_current_block_hash = "";
static uint32_t s_current_nonce = 0;
static uint32_t s_current_difficulty = 4;
static uint32_t s_current_block_ref = 0;
static String s_found_hash = "";
static uint32_t s_found_nonce = 0;

// ==========================================
// FORWARD DECLARATIONS
// ==========================================

static void state_fetch_block();
static void state_mining();
static void state_submit();
static bool sha256_hex(const String& input, String& output);
static bool check_difficulty(const String& hash, uint32_t difficulty);
static bool http_get_json(const String& endpoint, JsonDocument& doc);
static bool http_post_json(const String& endpoint, JsonDocument& payload, JsonDocument& response);

// External reference to device_id from main.cpp
extern String device_id;

// ==========================================
// PUBLIC API
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
    s_device_id = device_id;  // From main.cpp

    memset(&s_stats, 0, sizeof(s_stats));

    s_initialized = true;

    Serial.println("[MINING] Engine initialized");
    Serial.print("[MINING] Device ID: ");
    Serial.println(s_device_id);
    Serial.print("[MINING] Wallet: ");
    Serial.println(s_wallet);
    Serial.print("[MINING] Fullnode: ");
    Serial.println(s_fullnode_url);
    Serial.print("[MINING] Difficulty: ");
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
    if (!s_active) return;

    unsigned long now = millis();
    if (s_next_action_ms > 0 && now < s_next_action_ms) return;

    switch (s_state) {
        case STATE_IDLE: break;
        case STATE_FETCH_BLOCK: state_fetch_block(); break;
        case STATE_MINING: state_mining(); break;
        case STATE_SUBMIT: state_submit(); break;
        case STATE_WAIT_NEXT_CYCLE:
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
// STATE MACHINE
// ==========================================

static void state_fetch_block() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MINING] WiFi disconnected, retry in 10s");
        s_next_action_ms = millis() + 10000;
        return;
    }

    Serial.println("[MINING] Fetching latest block...");

    JsonDocument doc;
    bool ok = http_get_json("/block/latest", doc);

    if (!ok) {
        Serial.println("[MINING] Failed to fetch block, retry in 10s");
        s_stats.submit_errors++;
        s_next_action_ms = millis() + 10000;
        return;
    }

    // Parse LindFullnode format: {"block": {...}, "epoch": N}
    s_current_block_ref = doc["block"]["ep"] | 0;
    s_current_block_hash = doc["block"]["hash"].as<String>();
    s_stats.current_block = s_current_block_ref;

    if (s_current_block_hash.length() == 0) {
        Serial.println("[MINING] Empty block hash, retry in 10s");
        s_next_action_ms = millis() + 10000;
        return;
    }

    Serial.print("[MINING] Block #");
    Serial.print(s_current_block_ref);
    Serial.print(" | Hash: ");
    Serial.println(s_current_block_hash);

    s_current_nonce = 0;
    s_found_hash = "";
    s_state = STATE_MINING;
    s_state_start_ms = millis();
    s_next_action_ms = 0;
}

static void state_mining() {
    const uint32_t HASHES_PER_TICK = 500;

    if (millis() - s_state_start_ms > POC_TIMEOUT_MS) {
        Serial.println("[MINING] Timeout, refreshing block");
        s_state = STATE_FETCH_BLOCK;
        return;
    }

    for (uint32_t i = 0; i < HASHES_PER_TICK; i++) {
        // Include device_id to make proofs unique per node (prevents duplicate_proof)
        String input = s_current_block_hash + s_device_id + s_wallet + String(s_current_nonce);

        String hash;
        if (!sha256_hex(input, hash)) {
            Serial.println("[MINING] SHA256 error");
            s_state = STATE_FETCH_BLOCK;
            return;
        }

        s_stats.total_hashes++;

        if (check_difficulty(hash, s_current_difficulty)) {
            s_found_hash = hash;
            s_found_nonce = s_current_nonce;

            unsigned long elapsed = millis() - s_state_start_ms;

            Serial.println();
            Serial.print("[MINING] FOUND PROOF! Nonce=");
            Serial.print(s_found_nonce);
            Serial.print(" Hash=");
            Serial.println(s_found_hash.substring(0, 24) + "...");
            Serial.print("[MINING] Time: ");
            Serial.print(elapsed);
            Serial.print("ms | Hashes: ");
            Serial.println(s_current_nonce + 1);

            s_stats.proofs_generated++;
            s_state = STATE_SUBMIT;
            s_next_action_ms = 0;
            return;
        }

        s_current_nonce++;

        if (s_current_nonce >= POC_MAX_ITERATIONS) {
            Serial.println("[MINING] Max iterations, restart");
            s_state = STATE_FETCH_BLOCK;
            return;
        }
    }
}

static void state_submit() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MINING] WiFi lost during submit");
        s_next_action_ms = millis() + 5000;
        return;
    }

    Serial.println("[MINING] Submitting proof...");

    JsonDocument payload;
    payload["device_id"] = s_device_id;
    payload["firmware"] = "A";
    payload["version"] = LINDBLAD_FIRMWARE_VERSION;
    payload["wallet"] = s_wallet;
    payload["block_ref"] = s_current_block_ref;
    payload["block_hash"] = s_current_block_hash;
    payload["nonce"] = s_found_nonce;
    payload["proof_hash"] = s_found_hash;
    payload["difficulty"] = s_current_difficulty;

    JsonDocument response;
    bool ok = http_post_json("/api/v1/community-mining/submit", payload, response);

    s_stats.proofs_submitted++;

    if (!ok) {
        Serial.println("[MINING] Submit failed");
        s_stats.submit_errors++;
    } else {
        bool accepted = response["accepted"] | false;

        if (accepted) {
            s_stats.proofs_accepted++;
            float reward = response["reward"] | 0.0f;
            int total_proofs = response["total_proofs"] | 0;
            float total_pyco = response["total_rewards_pyco"] | 0.0f;

            Serial.println("[MINING] PROOF ACCEPTED!");
            Serial.print("[MINING] Reward: ");
            Serial.print(reward, 6);
            Serial.print(" PYCO | Total proofs: ");
            Serial.print(total_proofs);
            Serial.print(" | Total earned: ");
            Serial.print(total_pyco, 6);
            Serial.println(" PYCO");
        } else {
            s_stats.proofs_rejected++;
            String reason = response["reason"] | "unknown";
            Serial.print("[MINING] Rejected: ");
            Serial.println(reason);

            if (reason == "rate_limited") {
                int wait_s = response["wait_seconds"] | 60;
                Serial.print("[MINING] Waiting ");
                Serial.print(wait_s);
                Serial.println("s before next attempt");
            }
        }
    }

    Serial.print("[MINING] Next cycle in ");
    Serial.print(POC_SUBMIT_INTERVAL_MS / 1000);
    Serial.println("s");

    s_state = STATE_WAIT_NEXT_CYCLE;
    s_next_action_ms = millis() + POC_SUBMIT_INTERVAL_MS;
}

// ==========================================
// UTILITIES
// ==========================================

static bool sha256_hex(const String& input, String& output) {
    unsigned char hash[32];
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) { mbedtls_sha256_free(&ctx); return false; }
    if (mbedtls_sha256_update_ret(&ctx, (const unsigned char*)input.c_str(), input.length()) != 0) { mbedtls_sha256_free(&ctx); return false; }
    if (mbedtls_sha256_finish_ret(&ctx, hash) != 0) { mbedtls_sha256_free(&ctx); return false; }
    mbedtls_sha256_free(&ctx);

    char hex_buf[65];
    for (int i = 0; i < 32; i++) snprintf(hex_buf + (i * 2), 3, "%02x", hash[i]);
    hex_buf[64] = '\0';
    output = String(hex_buf);
    return true;
}

static bool check_difficulty(const String& hash, uint32_t difficulty) {
    for (uint32_t i = 0; i < difficulty; i++) {
        if (hash.charAt(i) != '0') return false;
    }
    return true;
}

static bool http_get_json(const String& endpoint, JsonDocument& doc) {
    HTTPClient http;
    String url = s_fullnode_url + endpoint;

    http.setTimeout(DEFAULT_FULLNODE_TIMEOUT_MS);

    if (url.startsWith("https://")) {
        WiFiClientSecure* client = new WiFiClientSecure;
        client->setInsecure();
        http.begin(*client, url);
    } else {
        http.begin(url);
    }

    int code = http.GET();

    if (code != 200) {
        Serial.print("[HTTP] GET failed: ");
        Serial.println(code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.print("[HTTP] JSON error: ");
        Serial.println(err.c_str());
        return false;
    }

    return true;
}

static bool http_post_json(const String& endpoint, JsonDocument& payload, JsonDocument& response) {
    HTTPClient http;
    String url = s_fullnode_url + endpoint;

    http.setTimeout(DEFAULT_FULLNODE_TIMEOUT_MS);

    if (url.startsWith("https://")) {
        WiFiClientSecure* client = new WiFiClientSecure;
        client->setInsecure();
        http.begin(*client, url);
    } else {
        http.begin(url);
    }

    http.addHeader("Content-Type", "application/json");

    String body;
    serializeJson(payload, body);

    int code = http.POST(body);

    if (code < 200 || code >= 300) {
        String resp_body = http.getString();
        Serial.print("[HTTP] POST failed: ");
        Serial.print(code);
        Serial.print(" | Body: ");
        Serial.println(resp_body);

        // Try to parse error response
        DeserializationError err = deserializeJson(response, resp_body);
        if (!err) {
            // Error body parsed - return true so caller sees "rejected" with reason
            http.end();
            return true;
        }

        http.end();
        return false;
    }

    String resp_body = http.getString();
    http.end();

    DeserializationError err = deserializeJson(response, resp_body);
    if (err) {
        Serial.print("[HTTP] JSON error: ");
        Serial.println(err.c_str());
        return false;
    }

    return true;
}
