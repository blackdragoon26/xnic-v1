# Procurement decision

Checked 2026-08-08. No purchase has been made. Prices, tax, customs, and stock
must be rechecked at checkout.

## Buy now if not already owned

1. **Raspberry Pi 4 Model B, 4 GB** with official power supply, microSD card,
   and simple cooling. More RAM and a Pi 5 do not add signal to this driver
   experiment. Robu lists the Pi 4 range through its Indian storefront:
   <https://robu.in/product-category/microcontroller-development-board/raspberry-pi-microcontroller-development-board/official-boards-and-accessories/raspberry-pi-4/>
2. **WIZnet WIZ850io**, manufacturer part `WIZ850io`, Mouser part
   `950-WIZ850IO`. This is the documented 3.3 V W5500 module with transformer
   and RJ45, not an unknown marketplace clone. Mouser India showed 741 in stock
   at INR 1,824.46 for one:
   <https://www.mouser.in/ProductDetail/WIZnet/WIZ850io>
3. Female-to-female 2.54 mm jumpers, a short Ethernet cable, and a USB-C
   Ethernet adapter for the peer if no second Ethernet-capable Linux machine is
   available.

## Logic analyzer

Recommended: **Saleae Logic 8**. It provides eight channels, 100 MS/s digital
capture, long streaming captures, SPI decoding, and Linux/macOS software. Its
specified fastest digital signal is 25 MHz, which covers the lab's initial
4 MHz and intended 20 MHz SPI rates:
<https://www.saleae.com/products/logic-8>.

Indian distributor listing: Mouser part `SAL-00111`:
<https://www.mouser.in/ProductDetail/Saleae/SAL-00111>.

Saleae captures use the vendor's Logic 2 application. The repository's
`capture-spi.sh` helper targets sigrok-compatible analyzers and is not a Saleae
control script; preserve the native `.sal` session and exported CSV/PNG when a
Saleae is used.

A 24 MHz FX2/sigrok analyzer is acceptable only for the deliberately reduced
4 MHz bring-up clock. It is not adequate evidence for a 20 MHz final capture.

## Oscilloscope

Borrow or rent a calibrated scope first. Buying an INR 80k-plus instrument for
one resume project is not rational unless embedded/lab work is a continuing
career investment.

If purchasing, the **Rigol DHO914S** is a suitable long-term option: four
channels, 125 MHz bandwidth, 1.25 GSa/s, 12-bit conversion, and an AWG. An
Indian listing showed INR 86,564 and two units in stock:
<https://www.salicontech.co.in/product/rigol-dho914s-12bit-dso-125mhz-4ch-with-awg/>.

For this experiment, the scope must have at least two channels, 100 MHz-class
bandwidth, proper 10x probes, and enough memory to inspect reset, power, clock,
chip-select, and interrupt timing. The scope does not replace the logic
analyzer's protocol decode.

## Purchase gate

Do not order until existing equipment is inventoried. The minimum defensible
physical result needs the Pi, WIZ850io, wiring, peer, and logic analyzer. The
oscilloscope can be borrowed. Photograph part numbers on arrival and preserve
invoices outside the public repository.
