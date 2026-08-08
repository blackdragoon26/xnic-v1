# W5500 qualification gate

No row may be marked pass without a timestamped raw artifact in
`hardware/w5500/evidence/`. Source presence or a successful QEMU build is not
physical validation.

Software preflight is complete: the module builds on ARM64 Linux 6.8, sparse
passes, the overlay builds, ten contract tests pass, and 100 unbound module
load/unload cycles produce no new kernel messages. Raw results are under
`evidence/software/`. This does not change any physical row below.

| Gate | Required evidence | Status |
|---|---|---|
| board identity and tool versions | board model, OS, kernel, config, compiler, dtc | pending hardware |
| electrical preflight | continuity record and powered 3.3 V scope capture | pending hardware |
| reset timing | scope measurement: low >=500 us, PLL wait >=1 ms | pending hardware |
| SPI identity | logic decode of `VERSIONR == 0x04` | pending hardware |
| SPI framing | read/write captures with address, BSB, RWB, VDM and data decoded | pending hardware |
| probe/remove | 100 overlay/module bind cycles without warnings | pending hardware |
| packet protocols | ARP, ICMP, UDP, TCP, broadcast PCAPs on board and peer | pending hardware |
| buffer wrap | TX and RX pointer wrap with matching frames and counters | pending hardware |
| backpressure | deterministic queue stop/wake under TX saturation | pending hardware |
| RX pressure | burst receive, bounded IRQ work, no permanent INTn assertion | pending hardware |
| link transitions | 100 cable down/up cycles with carrier convergence | pending hardware |
| reset under traffic | 100 resets with recovery and no module reload | pending hardware |
| teardown races | close/remove concurrent with RX, TX, IRQ, and reset | pending hardware |
| fault paths | bad version, SPI error, allocation failure, timeout | pending hardware |
| diagnostics | strongest practical KASAN/KFENCE/lockdep run | pending hardware |
| sustained traffic | 30 minutes mixed bidirectional traffic, zero unexplained loss | pending hardware |

Release requires zero oops, sanitizer report, lock warning, stuck queue,
permanently asserted interrupt, leaked skb, or unexplained packet loss. Any
environment limitation must remain visible in the result rather than converted
to a pass.
