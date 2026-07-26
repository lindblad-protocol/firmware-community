/**
 * config.h - Global configuration for Lindblad Firmware A
 *
 * Contains constants, defaults, and feature flags.
 */

#ifndef LINDBLAD_CONFIG_H
#define LINDBLAD_CONFIG_H

// ==========================================
// FIRMWARE INFO
// ==========================================

#define LINDBLAD_FIRMWARE_TYPE "A"
#define LINDBLAD_FIRMWARE_VERSION "0.2.2"
#define LINDBLAD_PROTOCOL_VERSION 1

// ==========================================
// NETWORK ENDPOINTS
// ==========================================

// Lindblad Fullnode (default)
#define DEFAULT_FULLNODE_HOST "167.99.8.29"
#define DEFAULT_FULLNODE_PORT 8080
#define DEFAULT_FULLNODE_TIMEOUT_MS 10000

// Arbitrum Sepolia (for wallet queries)
#define ARBITRUM_SEPOLIA_RPC "https://sepolia-rollup.arbitrum.io/rpc"
#define ARBITRUM_SEPOLIA_CHAIN_ID 421614

// ==========================================
// MINING PARAMETERS
// ==========================================

// Proof of Coherence
#define POC_DIFFICULTY_INITIAL 4          // Number of leading zeros required
#define POC_MAX_ITERATIONS 1000000        // Prevent infinite loops
#define POC_SUBMIT_INTERVAL_MS 60000      // Submit proof every minute
#define POC_TIMEOUT_MS 30000              // Timeout for network operations

// Block validation
#define BLOCK_SYNC_INTERVAL_MS 30000      // Sync every 30 seconds
#define MAX_BLOCK_CACHE 100               // Blocks kept in memory

// ==========================================
// HARDWARE LIMITS
// ==========================================

// ESP32-S3 memory constraints
#define MAX_JSON_BUFFER_SIZE 4096
#define MAX_HTTP_RESPONSE_SIZE 8192
#define MAX_WALLET_BALANCE_STR 64

// Watchdog
#define WDT_TIMEOUT_SECONDS 30

// ==========================================
// STORAGE PATHS (LittleFS)
// ==========================================

#define CONFIG_FILE_PATH "/config.json"
#define WALLET_FILE_PATH "/wallet.json"
#define BLOCK_CACHE_DIR "/blocks"
#define LOG_FILE_PATH "/logs.txt"
#define MAX_LOG_SIZE_BYTES 65536          // 64KB max log

// ==========================================
// LED INDICATORS
// ==========================================

// LED status codes
#define LED_STATUS_BOOTING 0    // Slow blink
#define LED_STATUS_WIFI 1       // Fast blink
#define LED_STATUS_MINING 2     // Solid on
#define LED_STATUS_ERROR 3      // Very fast blink
#define LED_STATUS_WON 4        // Triple flash

// Default LED pin (ESP32-S3-DevKitC-1)
#ifndef LED_PIN
#define LED_PIN 48
#endif

// ==========================================
// HTTP UI
// ==========================================

#define HTTP_SERVER_PORT 80
#define HTTP_MAX_CONNECTIONS 4
#define HTTP_TIMEOUT_MS 5000

// ==========================================
// FEATURE FLAGS
// ==========================================

// Enable/disable features at compile time
#define FEATURE_MINING_ENABLED 1
#define FEATURE_HTTP_UI_ENABLED 1
#define FEATURE_MDNS_ENABLED 1
#define FEATURE_OTA_ENABLED 0     // Off by default, enable in production

// LoRa support (only on Heltec boards)
#ifdef HELTEC_LORA_V3
#define FEATURE_LORA_ENABLED 1
#else
#define FEATURE_LORA_ENABLED 0
#endif

// ==========================================
// SECURITY GUARANTEES (Firmware A)
// ==========================================

/**
 * IMPORTANT: This firmware is Firmware A (Community Miner).
 *
 * It DELIBERATELY does NOT include:
 * - Silicon PUF cryptography
 * - Chua HSC temporal proof
 * - BCH fuzzy extractor
 * - RWA attestation signing capability
 * - Producer registry integration
 *
 * These are exclusive to Firmware B (institutional).
 *
 * This separation guarantees that community mining cannot
 * compromise the integrity of the institutional model.
 *
 * RWAFactory smart contract rejects any attestation not
 * signed with silicon PUF, providing double security.
 */

#define FIRMWARE_A_LIMITATIONS_ACKNOWLEDGED 1

// ==========================================
// LOGGING
// ==========================================

// Log levels
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

// Current log level
#ifdef DEBUG
#define LOG_LEVEL LOG_LEVEL_DEBUG
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// Logging macros
#define LOG_ERROR(msg) if (LOG_LEVEL >= LOG_LEVEL_ERROR) Serial.println("[ERROR] " msg)
#define LOG_WARN(msg)  if (LOG_LEVEL >= LOG_LEVEL_WARN)  Serial.println("[WARN] " msg)
#define LOG_INFO(msg)  if (LOG_LEVEL >= LOG_LEVEL_INFO)  Serial.println("[INFO] " msg)
#define LOG_DEBUG(msg) if (LOG_LEVEL >= LOG_LEVEL_DEBUG) Serial.println("[DEBUG] " msg)

#endif // LINDBLAD_CONFIG_H
