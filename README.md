# Lindblad Firmware A — Community Miner

Open source firmware for community-driven PYCO mining on the Lindblad Protocol network.

**Version:** 0.1.0 (Development)
**License:** MIT
**Target:** ESP32-S3 (and compatible boards)

---

## What is this?

Firmware A is the **public, open source** firmware that anyone can install on Lindblad-compatible hardware to participate in community mining of PYCO tokens.

This is the counterpart to Firmware B (institutional, closed source) which handles verified producer attestations for Real World Asset (RWA) tokenization.

### What Firmware A does:

- ✅ Mines PYCO via Proof of Coherence consensus
- ✅ Connects to the Lindblad Fullnode network
- ✅ Provides local HTTP UI for configuration
- ✅ Manages wallet address for reward distribution
- ✅ Syncs blockchain state

### What Firmware A does NOT do:

- ❌ Sign RWA attestations
- ❌ Mint verified tokens (GKWH, SBAG, OILB, etc.)
- ❌ Access silicon PUF cryptography
- ❌ Register as institutional producer

**These capabilities are exclusive to Firmware B and require KYC/KYB verification.**

---

## Supported Hardware

| Board | Status | Notes |
|-------|--------|-------|
| ESP32-S3-DevKitC-1 | ✅ Recommended | Default target |
| Heltec WiFi LoRa 32 V3 | ✅ Supported | LoRa mesh capability |
| Generic ESP32-S3 | ✅ Supported | Basic functionality |

### Minimum Requirements

- ESP32-S3 or compatible chip
- 8 MB Flash
- 512 KB SRAM
- WiFi capability
- 5V USB power (or battery)

---

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- USB cable
- ESP32-S3 board

### Build and Flash

```bash
# Clone repository
git clone https://github.com/lindblad-protocol/firmware-community
cd firmware-community

# Build for ESP32-S3
pio run

# Upload to connected board
pio run --target upload

# Monitor serial output
pio device monitor
```

### First Boot Setup

1. Power on the device
2. Look for WiFi network named `Lindblad-LD-XXXXXXXX`
3. Connect to it (open network, no password)
4. Browser will open automatically to configuration portal
5. Enter your WiFi credentials
6. Enter your PYCO wallet address
7. Device restarts and starts mining

---

## Project Structure

firmware-community/
├── platformio.ini # PlatformIO configuration
├── README.md # This file
├── LICENSE # MIT license
├── include/
│ └── config.h # Global configuration
├── src/
│ ├── main.cpp # Main entry point
│ ├── application/ # Application layer
│ ├── consensus/ # Proof of Coherence
│ ├── network/ # P2P and fullnode connection
│ ├── crypto/ # Basic cryptography (NO PUF)
│ └── storage/ # Persistent storage
├── test/ # Unit and integration tests
├── examples/ # Example configurations
├── docs/ # Technical documentation
└── scripts/ # Helper scripts

---

## Architecture

Firmware A is organized in 5 layers:

1. **Application Layer** — Mining engine, reward manager, config UI
2. **Consensus Layer** — Proof of Coherence, block validation, chain sync
3. **Network Layer** — P2P discovery, fullnode connection, message routing
4. **Crypto Layer** — SHA-256, basic ECDSA (limited scope, no PUF access)
5. **Storage Layer** — Configuration, blockchain cache, wallet keys

See `docs/ARCHITECTURE.md` for detailed design.

---

## Economics

### Community Mining Rewards

- **Total pool:** 30,000,000 PYCO (30% of total supply)
- **Distribution period:** ~10 years with halving every 2 years
- **Emission model:** Proof of Coherence based

### Expected Returns

Realistic scenarios depend on PYCO market value. See main protocol documentation for detailed economic analysis.

**Important:** Community mining rewards are subject to network conditions and PYCO market dynamics. This firmware makes no guarantees regarding returns.

---

## Security

### What Firmware A Guarantees

- **Cannot forge RWA attestations** — No silicon PUF access in codebase
- **Cannot mint verified tokens** — RWAFactory rejects non-PUF signatures
- **Cannot bypass institutional verification** — Producer Registry requires oracle approval

### Threat Model

Firmware A operates in a **trust-minimized public environment**:

- Miners can modify their local firmware (only affects themselves)
- Multiple wallets per miner is acceptable
- P2P network is public and monitorable
- Rewards are proportional to legitimate mining work

### What Users Should Do

- Keep firmware up to date
- Secure your wallet private keys
- Do not share your wallet configuration
- Use trusted networks for setup

---

## Building from Source

### Requirements

- PlatformIO 6.0+
- Python 3.9+
- USB drivers for your board

### Development Build

```bash
pio run
```

### Production Build

```bash
pio run --environment esp32-s3-devkitc-1
```

### Running Tests

```bash
pio test
```

---

## Contributing

Contributions welcome. Please:

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass
5. Submit a pull request

See `CONTRIBUTING.md` for detailed guidelines.

---

## Roadmap

### v0.1.0 (Current) — Boot & Configuration
- ✅ Project structure
- ✅ WiFi setup via WiFiManager
- ✅ Basic HTTP configuration
- ✅ Device identity generation

### v0.2.0 — Mining Engine
- Proof of Coherence implementation
- Fullnode connection
- Reward tracking

### v0.3.0 — Network Layer
- P2P discovery via mDNS
- Chain synchronization
- Block validation

### v0.4.0 — Persistence
- LittleFS storage
- Wallet management
- Configuration persistence

### v1.0.0 — Public Release
- Security audit completed
- Beta testing phase
- Production ready

**Target release:** Q1 2027

---

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md)
- [API Reference](docs/API.md)
- [Setup Guide](docs/SETUP.md)
- [Security Model](docs/SECURITY.md)

---

## Community

- **Website:** https://lindblad.io
- **GitHub:** https://github.com/lindblad-protocol
- **Documentation:** https://docs.lindblad.io (coming soon)

---

## License

MIT License — See [LICENSE](LICENSE) for full text.

**Trademark Notice:** "Lindblad" is a trademark of Lindblad Protocol. This firmware is officially maintained, but community forks may not use the Lindblad trademark for branding.

---

## Related Projects

- **Lindblad Protocol Contracts** — https://github.com/lindblad-protocol/contracts
- **Lindblad Documentation** — https://github.com/lindblad-protocol/docs
- **Lindblad Website** — https://lindblad.io

---

*Lindblad Firmware A v0.1.0*
*Post Founder House London — 2026*
