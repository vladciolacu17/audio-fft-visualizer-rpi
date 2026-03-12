# Real-Time Audio FFT Visualizer on Raspberry Pi Zero 2 W

A register-level embedded C project that samples audio from a MAX4466 microphone through an ADC0832CCN ADC, performs FFT-based analysis, and renders a real-time waveform + spectrum visualizer on HDMI.

## Hardware
- Raspberry Pi Zero 2 W
- MAX4466 microphone amplifier
- ADC0832CCN 8-bit ADC
- RC anti-alias filter: 1 kΩ + 33 nF

## GPIO connections (BCM numbering)
- ADC0832 CS  -> GPIO 8
- ADC0832 CLK -> GPIO 11
- ADC0832 DI  -> GPIO 10
- ADC0832 DO  -> GPIO 9
- ADC0832 CH0 -> RC filter node from MAX4466 OUT
- VCC -> 3.3 V
- GND -> GND

## Build
```bash
sudo apt-get update
sudo apt-get install -y build-essential libfftw3-dev libsdl2-dev
gcc -O2 -o audio_vis src/audio_vis.c -lfftw3 -lSDL2 -lm
```

## Run
```bash
sudo ./audio_vis
```

If launching from SSH and displaying on the Pi screen:
```bash
export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/1000
sudo -E ./audio_vis
```

## Project structure
```text
audio-fft-visualizer-rpi/
├── README.md
├── LICENSE
├── .gitignore
├── Makefile
├── src/
│   └── audio_vis.c
└── docs/
    ├── REPORT_TEXT.md
    └── images/
        ├── software_block_diagram.png
        └── hardware_schematic.png
```
