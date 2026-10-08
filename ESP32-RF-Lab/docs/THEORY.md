# Interference in the 2.4 GHz ISM Band: An Academic Overview

This document provides the theoretical background that motivates **ESP32-RF-Lab**. It discusses how radio interference is classified, why it is heavily regulated, and which measurement techniques this lab implements instead. It is intentionally conceptual: no interference techniques are described at implementation level.

## 1. The shared spectrum problem

The 2.400–2.4835 GHz band is ISM (Industrial, Scientific, Medical): Wi-Fi, Bluetooth, BLE, Zigbee, proprietary links and even microwave ovens share it without exclusive frequency rights. Coexistence depends on polite access rules (listen-before-talk, adaptive frequency hopping, duty-cycle limits). The band is channelized differently by each protocol:

* **Wi-Fi** — 11–14 channels of 22 MHz (only 1/6/11 are mutually non-overlapping).
* **BLE** — 40 channels of 2 MHz (37 data + 3 advertising: 2402, 2426, 2480 MHz).
* **nRF24-style links** — 125 channels of 1 MHz starting at 2400 MHz.

## 2. Taxonomy of interference (conceptual)

* **Barrage** — Noise or carriers spread across the whole band; indiscriminate denial.
* **Swept / follower** — Energy that hops along or tracks the victim's channel usage.
* **Single-channel** — Persistent energy parked on one specific channel.
* **Protocol-aware DoS** — Valid-looking frames that exhaust victims' state machines rather than raising the noise floor.

All of these share a defining property: they deny service to **everyone** in range, not only the intended target — including emergency and safety systems.

## 3. Why active interference is illegal (and disclaimers do not help)

Radio jammers are prohibited to manufacture, market, import or operate in most jurisdictions, because spectrum is a licensed public resource:

* **United States** — 47 U.S.C. §302(b) and 47 C.F.R. §2.803/§2.805; the FCC has repeatedly enforced against "educational/research" devices and explicitly rejects such disclaimers.
* **European Union** — Radio Equipment Directive 2014/53/EU and its national implementations.
* **Spain** — Ley 9/2014, General de Telecomunicaciones; RF inhibition is reserved to specific State facilities (e.g. penitentiaries).

No contract, authorization letter or "red-team engagement" can waive these rules, because the interfered spectrum belongs to third parties. Legitimate interference research does exist, but only inside **contained environments** (Faraday cages, anechoic chambers) operated by accredited EMC laboratories.

## 4. What this lab measures instead

ESP32-RF-Lab takes the defensive, diagnostic side of the same physics:

* **Energy detection** — The nRF24L01+ RPD bit flags received power above ≈ -64 dBm; sampled statistically across channels it yields an occupancy map of the band (where is it crowded? where is a clean channel for my deployment?).
* **Traffic observation** — Frames passing the CRC are counted as real on-air activity (`frames_seen`).
* **Link characterization** — The `LINK/PER` mode exchanges standard packets between the lab's own two radios to quantify link quality per channel (site surveys, antenna comparison, range testing).

These are the same measurements an RF engineer performs before deploying any 2.4 GHz system: passive, standard-compliant, and aimed at interference *hunting* rather than interference *creation*.
