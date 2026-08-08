#!/bin/sh
set -eu

OUT=${1:-w5500-spi.sr}
DRIVER=${SIGROK_DRIVER:-fx2lafw}
SAMPLES=${SIGROK_SAMPLES:-12000000}

command -v sigrok-cli >/dev/null 2>&1 || {
	echo "sigrok-cli is required" >&2
	exit 1
}

# Default 24 MHz acquisition is six samples per 4 MHz SPI clock.
sigrok-cli --driver "$DRIVER" --config samplerate=24m \
	--channels D0=SCLK,D1=MOSI,D2=MISO,D3=CS,D4=INT,D5=RESET \
	--samples "$SAMPLES" --output-file "$OUT"
echo "captured $OUT; decode as SPI mode 0, MSB first"
