# EVA edge-first voice platform

## Overview
EVA is an edge-first voice interface platform that captures live audio, recognises wake phrases, and hands control to agentic skills. It ships as a lean C++17 runtime optimised for laptops (macOS) and Raspberry Pi-class hardware, keeping wake word detection and low-latency decision making on-device for privacy and resilience. EVA’s roadmap layers in DSP (MFCCs), neural inference, and an orchestration API so developers can plug in their own assistant logic or connect to hosted EVA services.

### Core Value Proposition
- **On-device intelligence:** Wake phrase detection and first-stage inference run locally, reducing cloud dependency and latency.
- **Agent-ready pipeline:** Audio buffers, VAD results, and metadata flow into an action orchestrator so you can trigger skills, tools, or LLM agents.
- **Fleet friendly:** Designed for commercial deployments with a control plane that provisions devices, ships OTA updates, and exposes analytics.
- **Built-in transcription:** Optional Vosk integration streams wake segments into speech-to-text, printing transcripts directly to the console.

## Architectural Diagram
```
                 +-----------------+     speech frames     +------------------+
 Mic / ALSA /    | Voice Activity  | --------------------> | Skill Orchestrator|
 CoreAudio ----> |  Detection      |                       | (Agents / Tools)  |
                 +-----------------+                       +------------------+
                        ^                                           |
                        | PCM buffers                               |
                 +---------------+                           +-------------+
                 | AudioCapture  |<---------------------------| Control    |
                 | (Platform IO) |  device config, OTA        | Plane (SaaS)|
                 +---------------+                           +-------------+
                        |
        Platform backends:
          - macOS: CoreAudio + AudioQueue
          - Linux / Raspberry Pi: ALSA PCM
```

## Dependencies
- CMake >= 3.16
- A C++17 toolchain (`clang` on macOS, `gcc`/`g++` on Linux)
- Platform audio SDK
  - macOS: CoreAudio (Xcode Command Line Tools)
  - Linux/Raspberry Pi: ALSA headers (`libasound2-dev`)
- (Optional) Vosk speech recognition SDK + offline acoustic model for transcription
- Optional: Docker + Docker Compose v2 for containerised builds

## Installation

### macOS (Apple Silicon or Intel)
1. Install Xcode Command Line Tools: `xcode-select --install`
2. Install CMake via Homebrew (if missing): `brew install cmake`
3. Install Vosk C API (optional, for transcription). Follow the [official instructions](https://alphacephei.com/vosk/install) or build from source so that `pkg-config --libs vosk` succeeds.
4. Configure and build:
   ```bash
   cmake -S . -B build
   cmake --build build
   ```

### Raspberry Pi OS / Other Debian-based Linux
1. Install build prerequisites:
   ```bash
   sudo apt-get update
   sudo apt-get install -y build-essential cmake pkg-config libasound2-dev
   ```
2. Install Vosk (optional, for transcription):
   ```bash
   sudo apt-get install -y libvosk
   # or build and install the Vosk C API manually if your distro lacks packages
   ```
3. Configure and build:
   ```bash
   cmake -S . -B build
   cmake --build build
   ```

### Docker (Clean Linux Build/Test)
Use the provided `docker.yaml` to spin up disposable build containers:
```bash
# Build/test on amd64
docker compose -f docker.yaml run --rm kws-build-amd64

# Build/test on arm64 rootfs (mimics Raspberry Pi 5)
docker compose -f docker.yaml run --rm kws-build-arm64
```
The services install dependencies, run `cmake`/`ctest`, and leave the generated `build`/`build-arm64` directories on the host.

## Usage
1. Run the binary after building:
   ```bash
   ./build/kws
   ```
2. At startup the program lists available capture devices (CoreAudio or ALSA).
3. Speak into the selected microphone; energy spikes above the threshold trigger transcription and print `[Transcription] ...` lines on the console (requires Vosk).
4. Set the `EVA_VOSK_MODEL` environment variable to point to the unpacked model directory, or place a model at `models/vosk-model-small-en-us-0.15`.
5. Press `Ctrl+C` to exit.

### Customising Audio Input
- Edit `AudioConfig` in `src/main.cpp` (or set via future CLI flags) to choose sample rate, buffer size, and device identifier.
- On Linux/Raspberry Pi, use ALSA names such as `plughw:1,0`. On macOS, change the default input device in System Settings.
- To disable transcription (e.g., no Vosk library present) build with `-DEVA_ENABLE_VOSK=OFF`.

## Commercial Roadmap
- **Starter Plan:** Ship EVA runtime with default wake word, local dashboards, and manual skill configuration for small teams.
- **Pro Plan:** Add hosted control plane, fleet management, usage analytics, and configurable skill marketplace.
- **Enterprise Plan:** Custom wake-word training, managed OTA updates, SLA-backed support, and hybrid on-prem/cloud deployments.

Interested in shaping EVA into a commercial subscription service? Focus next on hardening the orchestrator, exposing APIs for third-party skills, and building streaming telemetry so the control plane can deliver insights and billing-grade metering.

## Open Source Contributions
- **Skills & Agents:** Fork the repo, add modules under `src/app` or contribute skill templates that expose new automations. Submit pull requests with unit tests and a short demo description.
- **Voices & DSP:** Integrate open-source TTS engines or deploy new wake-word/DSP models. Document configuration steps and provide licensing info inside your PR.
- **Tooling & Docs:** Improve build scripts, Docker flows, or developer documentation. Open issues first for major changes so we can align on scope.

Contribution checklist:
1. Create a feature branch from `main`.
2. Run `cmake --build build` (or Docker compose flow) to ensure the runtime compiles.
3. Add tests or samples where applicable.
4. Submit a PR describing changes, test coverage, and any new dependencies.

## EVA vs Alexa / Google Assistant Devices
Key differences between EVA and mainstream smart-speaker ecosystems like Amazon Alexa or Google Nest Mini:

- **Deployment:** EVA runs on your hardware (macOS, Raspberry Pi, containers). Alexa and Google Mini are tied to vendor-specific smart speakers.
- **Data Control:** EVA keeps audio on-device by default; you decide what, if anything, goes to the cloud. Consumer assistants route interactions through Amazon or Google services.
- **Customization:** EVA lets you brand the experience, build bespoke skills, and choose voices or TTS engines. Alexa/Google limit you to marketplaces and approved APIs.
- **Privacy & Compliance:** EVA can operate offline or in edge-first mode for regulated industries (healthcare, industrial, finance). Alexa/Google are cloud-first services with vendor-defined data policies.
- **Commercial Focus:** EVA is a platform you can resell or deploy privately with subscription plans, OTA updates, and analytics. Alexa/Google devices are consumer products monetized via vendor ecosystems.

In short, EVA is a controllable, customisable voice platform for building your own agentic experiences on edge devices, whereas Alexa/Google Mini are consumer smart speakers locked into proprietary ecosystems.
