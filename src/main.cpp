/**
 * Lindblad Firmware A — Community Miner
 *
 * Main entry point with active mining engine.
 *
 * Version: 0.2.0 - Mining Engine
 * License: MIT
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "application/mining_engine.h"
#include "crypto/puf.h"

// ==========================================
// GLOBAL STATE
// ==========================================

const char* FIRMWARE_NAME = "Lindblad Community Miner";
const char* FIRMWARE_VERSION_STR = "0.2.1";
const char* FIRMWARE_TYPE_STR = "A";

String device_id = "";
String puf_hex = "";
String wallet_address = "";
String fullnode_url = "";
bool is_configured = false;

unsigned long boot_timestamp = 0;

// Configuration - HARDCODED for now (v0.3 will add web UI)
// TODO: Load from LittleFS
// For testing, set your wallet here or leave empty
const char* HARDCODED_WALLET = "0x964b32cef544001d6DB8F585B708A5A62da308eD";
const char* HARDCODED_FULLNODE = "https://api.lindblad.io";

// ==========================================
// FORWARD DECLARATIONS
// ==========================================

void print_banner();
void generate_device_id();
void setup_wifi();
void load_config();
void print_status();
void handle_serial_commands();

// ==========================================
// SETUP
// ==========================================


// ============================================
// Pairing Code Generation & Upload
// ============================================
static String g_pairing_code = "";

String generate_pairing_code() {
    String code = "";
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 6; i++) {
        code += chars[esp_random() % 36];
    }
    return code;
}

void upload_pairing_code() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[PAIR] No WiFi, skip pairing upload");
        return;
    }

    // Generate pairing code
    g_pairing_code = generate_pairing_code();

    // Build URL
    String url = "https://api.lindblad.io/upload-pairing";

    // Build JSON payload
    StaticJsonDocument<256> payload;
    payload["nodeId"] = device_id;
    payload["code"] = g_pairing_code;
    payload["puf"] = device_id; // Firmware A uses device_id as identifier

    String body;
    serializeJson(payload, body);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    int code = http.POST(body);

    if (code >= 200 && code < 300) {
        Serial.println();
        Serial.println("=====================================");
        Serial.println("  PAIRING CODE (share with owner)");
        Serial.println("=====================================");
        Serial.print("  Code: ");
        Serial.println(g_pairing_code);
        Serial.print("  Node: ");
        Serial.println(device_id);
        Serial.println("=====================================");
        Serial.println();
    } else {
        Serial.printf("[PAIR] Upload failed: HTTP %d\n", code);
    }

    http.end();
}


void setup() {
    Serial.begin(115200);
    delay(2000);

    boot_timestamp = millis();

    print_banner();

    generate_device_id();
    Serial.print("[BOOT] Device ID: ");
    Serial.println(device_id);

    Serial.println("[BOOT] Loading configuration...");
    load_config();

    Serial.println("[BOOT] Setting up WiFi...");
    setup_wifi();

    // Initialize mining engine
    if (is_configured && WiFi.status() == WL_CONNECTED) {
        Serial.println("[BOOT] Initializing mining engine...");

        if (mining_engine_init(wallet_address, fullnode_url)) {
            mining_engine_start();
            Serial.println("[BOOT] Mining engine STARTED");
        } else {
            Serial.println("[BOOT] Mining engine init FAILED");
        }
    } else {
        Serial.println("[BOOT] Mining engine NOT started (missing config or WiFi)");
    }

    print_status();

    Serial.println("[BOOT] System ready");
    Serial.println("[BOOT] Type 'help' + Enter for commands");
    Serial.println();
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
    static unsigned long last_heartbeat = 0;
    static unsigned long last_stats_print = 0;
    static unsigned long last_wifi_check = 0;
    static unsigned long last_successful_proof = 0;
    static unsigned long wifi_disconnected_since = 0;
    unsigned long now = millis();

    // ── WiFi Watchdog: check every 30s ──
    if (now - last_wifi_check >= 30000) {
        last_wifi_check = now;
        if (WiFi.status() != WL_CONNECTED) {
            if (wifi_disconnected_since == 0) {
                wifi_disconnected_since = now;
                Serial.println("[WATCHDOG] WiFi disconnected, attempting reconnect...");
                WiFi.reconnect();
            } else if (now - wifi_disconnected_since >= 120000) {
                // 2 min without WiFi -> restart
                Serial.println("[WATCHDOG] WiFi down >2min, restarting ESP32");
                delay(1000);
                ESP.restart();
            }
        } else {
            wifi_disconnected_since = 0;
        }
    }

    // ── Mining Watchdog: restart if no proof in 10 minutes ──
    MiningStats current_stats = mining_engine_get_stats();
    if (current_stats.proofs_accepted > 0) {
        last_successful_proof = now;
    }
    if (last_successful_proof > 0 && now - last_successful_proof >= 600000) {
        Serial.println("[WATCHDOG] No proof accepted in 10min, restarting");
        delay(1000);
        ESP.restart();
    }

    // Heartbeat every 15 seconds
    if (now - last_heartbeat >= 15000) {
        last_heartbeat = now;

        MiningStats stats = mining_engine_get_stats();

        Serial.print("[HEARTBEAT] Uptime: ");
        Serial.print((now - boot_timestamp) / 1000);
        Serial.print("s | Block: ");
        Serial.print(stats.current_block);
        Serial.print(" | Hashes: ");
        Serial.print((unsigned long)stats.total_hashes);
        Serial.print(" | Accepted: ");
        Serial.print(stats.proofs_accepted);
        Serial.print("/");
        Serial.print(stats.proofs_submitted);
        Serial.print(" | Mining: ");
        Serial.println(mining_engine_is_active() ? "YES" : "NO");
    }

    // Full stats every 5 minutes
    if (now - last_stats_print >= 300000) {
        last_stats_print = now;
        mining_engine_print_stats();
    }

    // Mining engine tick
    mining_engine_tick();

    // Serial commands
    handle_serial_commands();

    delay(10);
}

// ==========================================
// UTILITIES
// ==========================================

void print_banner() {
    Serial.println();
    Serial.println("========================================");
    Serial.println("  LINDBLAD COMMUNITY MINER");
    Serial.println("  Firmware A - Open Source");
    Serial.println("========================================");
    Serial.print("Version: ");
    Serial.println(FIRMWARE_VERSION_STR);
    Serial.print("Type: ");
    Serial.println(FIRMWARE_TYPE_STR);
    Serial.print("Compiled: ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.println(__TIME__);
    Serial.println("========================================");
    Serial.println();
}

void generate_device_id() {
    // Extract silicon PUF identity (real hardware fingerprint)
    // Falls back to MAC-based ID if PUF not yet enrolled (first boot)
    char node_id_buf[NODE_ID_LEN];
    char puf_hex_buf[PUF_HEX_LEN];
    
    bool puf_ready = puf_extract(node_id_buf, puf_hex_buf);
    
    if (puf_ready) {
        // PUF successfully reconstructed - use silicon identity
        device_id = String(node_id_buf);
        puf_hex = String(puf_hex_buf);
        Serial.print("[PUF] Silicon identity: ");
        Serial.println(device_id);
    } else {
        // PUF still enrolling - use temporary MAC-based ID
        // User needs to power-cycle 12 times to complete enrollment
        uint64_t chip_id = ESP.getEfuseMac();
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "LD-%08X", (uint32_t)(chip_id >> 24));
        device_id = String(buffer);
        puf_hex = device_id; // Fallback
        Serial.println("[PUF] Enrollment in progress. Power-cycle 12 times.");
        Serial.print("[PUF] Temporary ID: ");
        Serial.println(device_id);
    }
}

void setup_wifi() {
    WiFiManager wm;

    String ap_name = "Lindblad-" + device_id;

    Serial.print("[WIFI] Starting WiFiManager. AP: ");
    Serial.println(ap_name);

    bool connected = wm.autoConnect(ap_name.c_str());

    if (connected) {
        Serial.println("[WIFI] Connected!");
        Serial.print("[WIFI] IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("[WIFI] SSID: ");
        Serial.println(WiFi.SSID());
        
        // Print pairing URL for LindWallet
        Serial.println();
        Serial.println("=====================================");
        Serial.println("  OPEN THIS URL ON YOUR PHONE:");
        Serial.println("=====================================");
        Serial.print("  https://api.lindblad.io/wallet?node=");
        Serial.print(device_id);
        Serial.print("&puf=");
        Serial.println(puf_hex);
        Serial.println("=====================================");
        Serial.println();
    } else {
        Serial.println("[WIFI] Failed to connect. Rebooting in 5s...");
        delay(5000);
        ESP.restart();
    }
}

void load_config() {
    // For v0.2.0 we use hardcoded values
    // v0.4.0 will load from LittleFS

    wallet_address = String(HARDCODED_WALLET);
    fullnode_url = String(HARDCODED_FULLNODE);

    if (wallet_address.length() > 0 && fullnode_url.length() > 0) {
        is_configured = true;
        Serial.print("[CONFIG] Wallet: ");
        Serial.println(wallet_address);
        Serial.print("[CONFIG] Fullnode: ");
        Serial.println(fullnode_url);
        
        // Generate and upload pairing code
        upload_pairing_code();
    } else {
        is_configured = false;
        Serial.println("[CONFIG] Incomplete configuration");
    }
}

void print_status() {
    Serial.println();
    Serial.println("--- STATUS ---");
    Serial.print("Device ID: ");
    Serial.println(device_id);
    Serial.print("Firmware: ");
    Serial.print(FIRMWARE_NAME);
    Serial.print(" v");
    Serial.println(FIRMWARE_VERSION_STR);
    Serial.print("Configured: ");
    Serial.println(is_configured ? "YES" : "NO");
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }
    Serial.print("Mining: ");
    Serial.println(mining_engine_is_active() ? "ACTIVE" : "IDLE");
    Serial.println("--------------");
    Serial.println();
}

void handle_serial_commands() {
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "help") {
        Serial.println();
        Serial.println("Available commands:");
        Serial.println("  help    - show this help");
        Serial.println("  status  - show system status");
        Serial.println("  stats   - show mining statistics");
        Serial.println("  start   - start mining");
        Serial.println("  stop    - stop mining");
        Serial.println("  reboot  - restart the device");
        Serial.println();
    } else if (cmd == "status") {
        print_status();
    } else if (cmd == "stats") {
        mining_engine_print_stats();
    } else if (cmd == "start") {
        mining_engine_start();
    } else if (cmd == "stop") {
        mining_engine_stop();
    } else if (cmd == "reboot") {
        Serial.println("Rebooting...");
        delay(500);
        ESP.restart();
    } else if (cmd.length() > 0) {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
        Serial.println("Type 'help' for available commands");
    }
}
