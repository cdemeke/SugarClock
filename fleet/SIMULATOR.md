# Disposable fleet simulator

Run only against an isolated development fleet database. The simulator creates
real installation records with private credentials in `simulator-state.json`
(mode 0600); keep that file out of source control and never share it. It prints
counts and error types, never credentials.

```sh
python -m fleet.simulator --server http://127.0.0.1:8080 --count 3 --features
```

Clocks report `fleet_rollout_v1` by default. `--features` adds a fixed demo feature
snapshot. `--legacy` omits both and uses legacy polling cadence; use a separate
`--state legacy-state.json` when comparing legacy with fleet-aware installations.
The simulator respects server intervals with firmware-like clamps and jitter,
backs off failed visits per clock, and retries registration with the existing
identity after authentication loss. `--once` performs one visit per selected clock.

By default update offers produce a deferred outcome. **`--simulate-updates`
explicitly enables fake successful updates**, gated by a fresh matching, unexpired
server authorization. It neither downloads nor verifies firmware and never
flashes or tests a bootloader. Its `boot_validated` result is synthetic and is
unsuitable for bridge readiness or production deployment decisions. Lost result
acknowledgments are retained for retry. Legacy JSON `ota_install` commands cannot
change the simulated version.

Up to 1,000 identities may be selected, but public registration limits still
apply; this is an interaction simulator, not a capacity harness. For 1,000-clock
transition sizing, use `python -m fleet.load_test --devices 1000 --seconds 10 --rate 12`,
which creates a disposable mixed population without flooding public enrollment.
