## Context

See `proposal.md` — Why. What shapes the approach:

- `DE1::Characteristic::CALIBRATION` (`0000A012`) is declared at `de1characteristics.h:50` with no reader, writer or subscriber. The USB serial transport already accepts it (`serialtransport.cpp:465` covers through `0xA012`).
- `BinaryCodec` already has `encodeS32P16` / `decodeS32P16` — the only unusual number format in the record.
- One shared GATT queue orders every operation in the process (`blegattqueue.h`), so a burst of reads needs no pacing of its own. de1app staged the equivalent with `after 1000 / 2000 / 4000 / 5000` (`gui.tcl:2532`); Decenza must not, and does not need to.
- `DescalingPage.qml` (936 lines) and `TransportPage.qml` (414) establish the guided full-screen operation grammar this page follows. They differ in one way that matters: their phase IS their page, so the shell never replaces them. An espresso-driven operation has no such protection — see the phase-handler exemption below.
- The machine has a GHC. Shots start from the physical button, so a wizard observes; it does not drive the machine into Espresso.
- `test_pressure_calibration.json` (3 steps) and `test_temperature_calibration.json` (2 steps) already ship in `resources/profiles/`. They pull real shots and stay.

Reference implementations, and how far each gets:

| | `A012` sensor calibration | Heater voltage write | Shape of the UI |
|---|---|---|---|
| **de1app** | full: send / read / factory reset | yes (`set_heater_voltage`) | a settings grid the user types both halves into |
| **decaid** | endpoint declared read-only, transport plumbing only, no feature | yes, clamped two-value enum | none |
| **Decenza (this change)** | pressure + temperature, read / write / restore factory | yes | a guided wizard that measures one half itself |

de1app is the only source for the record format and command semantics; decaid contributes the heater-voltage enum shape. Neither is a model for the UI.

## Goals / Non-Goals

**Goals:**
- Make the error class that matters — a correction written against a value the machine never reported — **structurally impossible**, not merely warned about.
- One path, one write function, one place each per-sensor fact is declared.
- Follow the existing maintenance-page grammar rather than inventing a second one.

**Non-Goals:**
- Starting the shot. The user does that.
- Any second surface: no settings card, popup, MCP tool or web endpoint.
- Flow-sensor calibration (target 0), slow start, and replacing the test profiles — see `proposal.md`.
- Persisting anything.

## Decisions

### The user never types the machine's half

Every failure mode in this feature reduces to the `(DE1ReportedVal, MeasuredVal)` pair being mismatched — a profile target mistaken for a reading, a value remembered from the wrong shot, a stale number left in a field. de1app is exposed to all three because both halves are text fields on a settings page.

The wizard removes the class rather than guarding it: **`DE1ReportedVal` can only come from a run the wizard watched.** There is no field to type it into, no default, no fallback to a profile target, and no path that reads a historical shot. If the run did not hold, there is no value and no way to submit.

This is what earns the wizard over the settings card. It is not a nicer presentation of the same risk; it deletes the risk.

*Consequence:* the historical steady-window analysis a settings card would have needed disappears with it. So does any reuse of the auto-flow window finder (`maincontroller.cpp:3140`+), which would have been wrong here anyway — it gates on weight flow ≥ 0.5 g/s and a scale, and skips the first 10 s, while a calibration run uses a blind or leaking portafilter and often no scale at all.

### Placement: the Calibration tab, sharing the Maintenance row format

The two operations live in a Sensor Calibration card on Settings → Calibration, beside Flow Calibration, Weight Stop Timing and Heater Calibration — not on the Machine tab's Maintenance card. Calibration is what they are; descaling and transport are upkeep.

They still look like Maintenance operations, because that is the shape a guided full-screen operation already has here. Rather than copy those 45 lines a third time, the row is extracted to `qml/components/SettingsActionRow.qml` and Descaling Wizard and Transport Mode move onto it in the same change — the project's rule about collapsing copies when you touch code that has them. Their behaviour is unchanged, so `machine-maintenance` needs no spec delta.

### Two operations to the user, one parameterized page in the code

The two sensors are **separate Maintenance operations** — Pressure Calibration and Temperature Calibration — not one operation with a sensor picker inside. They need different equipment and a user will commonly own one and not the other: a gauge portafilter is a purchase, a thermocouple basket means drilling a no-hole basket and threading a K-type bead through it (`test_temperature_calibration.json` notes). A combined entry implies both are equally available and only reveals the hardware requirement after the user is inside; separate rows let each state its instrument on the Maintenance card, so someone who lacks one can see that without opening anything.

That separation is in the *presentation*, not the code. Pressure and temperature differ in five ways and no more: test profile, `A012` target byte, unit, the instrument to fit, and the two bounds. Declare that as one table in C++ — target, profile filename, unit label, physical range, maximum correction, instruction key — exposed to QML, and let **one** page read a row, launched from two Maintenance rows with different arguments.

*Alternative rejected:* two page files, or one page with `if (sensor === "pressure")` branches. Both are the shape that lets pressure gain a fix temperature never gets. The project rule is explicit: anything produced at more than one site gets one definition.

*Consequence worth having:* because the operations are independent all the way to the Maintenance row, dropping or deferring temperature later is removing one row and one table entry, not unpicking a shared flow.

### The machine's half is the profile's declared hold, not a measurement

`SensorCalibrationController` reads the loaded profile's final frame — its
declared pressure or temperature — and offers that as the machine's half. The
user enters only their instrument's reading, and `applyCorrection` combines the
two so no caller can name the machine's half.

It is guarded on the active profile: a correction is computed only while that
sensor's own test profile is loaded, which is what makes the declared hold the
number the user is watching.

*Two alternatives were tried and rejected, both with evidence:*

- **A per-profile scalar**, which is what de1app sends
  (`de1_skin_settings.tcl:2391`). It is a global and it goes stale — observed
  showing a Goal of 6.0 bar with the calibration profile loaded and holding at
  9.0, because the D-Flow editor sets it to 6.0
  (`profile_editors/D_Flow/code.tcl:154`). A correction against that is wrong by
  ~3 bar.
- **Measuring the hold from live shot samples.** Needs a steady-window search,
  and the shipped one picked the profile's 20 s 7 bar lead-in over its 60 s 9 bar
  hold. The frames already carry the right number with no heuristic, so the
  machinery bought nothing this workflow needed.

The controller takes its profile facts through a closure rather than a
`ProfileManager*`: taking the manager dragged its resource loading into the test
binary and put `ProfileManager` symbols in front of all 113 test targets. Same
pull-provider shape as `SettingsCalibration::setServingScaleTypeProvider`.

### One packer, one parser

The 14-byte big-endian record is `WriteKey` (u32), `CalCommand` (u8), `CalTarget` (u8), `DE1ReportedVal` (S32P16), `MeasuredVal` (S32P16) — `calibrate_spec`, `binary.tcl:414`. Enums and keys go in `de1characteristics.h` beside the UUID:

```
namespace DE1::Calibration {
    enum class Target  : uint8_t { Flow = 0, Pressure = 1, Temperature = 2 };
    enum class Command : uint8_t { ReadCurrent = 0, Write = 1, ResetFactory = 2, ReadFactory = 3 };
    constexpr uint32_t WRITE_KEY = 0xCAFEF00D;
    constexpr uint32_t READ_KEY  = 0x00000001;
}
```

`DE1Device::sendCalibration(target, command, reported, measured)` is the only writer, and `readCalibration()` is that call with different arguments. Pack and parse as private helpers in `de1device.cpp`, matching how `SHOT_SETTINGS` is assembled at `:2287` rather than adding a file for one record (a new `.cpp` costs ~1.4 s of build forever — `TESTING.md`).

### Replies: `WriteKey == 0` means it is a value

The characteristic notifies for reads, for writes, and with the machine's real current value. de1app's rule (`bluetooth.tcl:3344`) is the whole protocol: **a reply with `WriteKey == 0` carries a real value in `MeasuredVal`; anything else is an echo carrying nothing.** `CalTarget` selects the sensor; `CalCommand == 3` marks the factory value rather than the stored one.

Store as `std::optional<double>` per (target, kind) and expose read-only properties. An echo updates nothing; an unanswered read leaves the value absent and the wizard says so and refuses to write. Never zero — zero reads as "no correction" and is the one wrong answer that looks plausible.

Subscribe to `A012` in `bletransport.cpp` as a **non-required** subscription (the `WATER_LEVELS` pattern at `:225`), so a transport that cannot notify still connects and the wizard degrades to "unavailable" rather than the app failing to connect.

### Guards catch typos; they are not a safety net

Two checks on the instrument reading: inside the sensor's physical range (pressure `0–14 bar`, temperature `0–110 °C`), and within the sensor's maximum correction of the measured reading (**2 bar**, **5 °C**).

Their job is catching a unit mix-up or a slipped decimal — **not** preventing an unrecoverable state, because with an instrument in hand there isn't one. The loop is self-correcting: re-run, compare, enter the corrected reading, and the next write lands on a machine whose reported value already reflects the last. That is what the profile note means by "retest until the two agree", and it is why the wizard's final step is another run rather than a congratulation.

**The restore-to-factory command is the least-supported thing in this change.** The factory value lives in the machine's firmware, written at manufacture: the protocol has no command that sets it, so it is a read-only fact both apps can only fetch (`ReadFactory = 3`) or ask the firmware to restore from (`ResetFactory = 2`). The trouble is that de1app is inconsistent about which command restores. Its helper documents 2 — `# change calibcmd to 2, to reset to factory settings, otherwise default of 1 does a write` (`de1_comms.tcl:1653`) — but its reset buttons pass **3**, which is read-factory and would change nothing (`de1_skin_settings.tcl:2360-2362`). Those buttons are commented out, which may be exactly why.

So the only reference implementation of restore is disabled code contradicting its own documentation, and there is no firmware source here to settle it. Decenza uses 2, the better-supported reading. **It is unverified and must be checked on hardware** — write a small correction, restore, confirm the stored value returns to the factory number — before the wizard's restore affordance can be trusted. If 2 turns out to be wrong, the fix is one enum value.

*Caveat, stated because it is load-bearing and unverified:* there is no DE1 firmware source in this tree, so the exact arithmetic applied to the pair is not confirmed. What is confirmed is that both values are sent and the machine stores a signed offset it reports back (de1app renders it with `return_plus_or_minus_number`, `vars.tcl:1556`), and that de1app itself uses the returned values for display only — `::de1(calibration_pressure)` and friends are written solely by the notification handler (`bluetooth.tcl:3328-3336`) and read solely by four display widgets (`de1_skin_settings.tcl:2329-2339`). The correction lives in firmware. Confirm against firmware before tuning any guard that assumes a formula.

### The factory value is read and shown; restoring it is not implemented

Reading has a working reference and is kept: de1app issues `de1_read_calibration <sensor> "factory"` for temperature and pressure when its Calibrate page opens (`gui.tcl:2537-2538`, flow's equivalent commented out beside them) and displays the result (`de1_skin_settings.tcl:2338-2339`). That is a live, shipped path for exactly the two sensors in scope, so the wizard shows what the machine holds.

Restoring is NOT implemented, and the reason is a rule rather than a preference: **no reference, not shipped.** `Command::ResetFactory` (2) exists in the wire vocabulary, and de1app's own helper names 2 as the reset command (`de1_comms.tcl:1653`) — but its three reset buttons passed **3**, read-factory, from the day they were added (2018-02-27, `a2092efc`) until they were commented out ten weeks later (2018-05-15, `69e4277c`). Both commits carry empty messages, so nothing states why. That they could not have worked is the obvious guess and is only a guess.

With no working implementation in any app and no firmware source in this tree, shipping a restore would mean writing an unverified command to a machine in the name of undoing a mistake — the worst place to be wrong. And it buys little: the loop is self-correcting, so the actual fix for a bad calibration is another run with the instrument.

The enum value stays, annotated, so the protocol stays documented and nobody re-derives it; `tst_de1device_mmrreads` asserts nothing ever sends it.

### Heater voltage goes in the Heater Calibration popup, not in the wizard and not on a tab

It has no wizard shape — one value, no measurement, no loop, nothing to converge — so it does not belong in a calibration session. But it is also the wrong thing to leave in easy reach: it is set once at commissioning, almost never changed, and setting it wrong runs the heater at the wrong duty.

So it goes in the existing Heater Calibration popup, after the fan threshold row. That location is doing real work: the popup is already behind `calibrationWarningDialog` (`SettingsCalibrationTab.qml:767`), it already holds expert parameters nobody adjusts casually, and putting the control there costs one row instead of a new surface. A Machine-tab placement was the earlier plan and is rejected for exactly this reason — visible while browsing ordinary settings.

Two things follow and are easy to get wrong:
- **"Defaults for cafe" must not touch it.** That button resets five `Settings.hardware` preferences (`SettingsCalibrationTab.qml:924-929`). Heater voltage is machine state read back from the DE1, so resetting it there would write a guess to the machine.
- **The popup's own spec moves.** `heater-calibration-layout` names five parameters, budgets the height for five rows, and fixes the tab order; all three change. That is a MODIFIED delta, not a silent addition.

Reading it back needs decaid's bucket rule (`de1_interface.dart:126`): subtract 1000 when above 1000, then `90–150 → 120`, `180–260 → 230`, anything else unknown. Unknown renders as unknown with neither option preselected — guessing here and being wrong is a heater at the wrong duty.

## Risks / Trade-offs

- **A correction written with no instrument to check it against.** The real exposure, and narrower than de1app's warning implies: a user *with* a gauge cannot get stuck. → The wizard's structure makes the instrument reading the only thing typed, and the final step is a verification run rather than a completion claim.
- **The wizard cannot start the shot (GHC).** → It observes, exactly as Transport Mode does; the prepare step ends by waiting for the user.
- **A run that never holds steady produces nothing, which could read as the wizard being broken.** → Say plainly that no hold was measured and offer another run; the threshold is tunable from a real run (see Open Questions).
- **The firmware might not notify `A012` on some transport.** → Non-required subscription; values render unavailable and writing is refused, rather than a failed connect.
- **Wizard pages are large.** `DescalingPage.qml` is 936 lines and this will be comparable. Accepted: the alternative shape was a settings popup, and the reason for rejecting it is a correctness argument, not a size one.
- **Wiki drift.** A user-visible feature with no manual entry is incomplete per `CLAUDE.md`. → Manual entry is a task, not a follow-up, and per project rule it gets *shorter* because the wizard removes user steps.

## Migration Plan

None. Nothing persisted, no schema change, no settings key. Rollback is removing the Maintenance row and the page; a calibration already written to a machine stays there and is corrected by another run, or by restoring factory.

## Open Questions

None. The two that stood here — how to tune the steady-hold detector, and whether `CalCommand 2` restores — were both settled on hardware (2026-08-29): the detector was deleted with the measuring design, and the machine answers `CalCommand 3` with the current value, so there is no factory value to restore to.
