## REMOVED Requirements

### Requirement: MMR write verification retries a missing read-back, not just a mismatched one

**Reason**: `writeMMRVerified` and the whole read-back verification mechanism are removed by
this change, so a requirement governing how its read-back retries has nothing left to govern.
Leaving it in place would put a live requirement naming a function that no longer exists
directly beside the new requirement saying read-back verification is removed rather than left
available — and the next reader would resolve that contradiction by reintroducing the
mechanism, which is exactly what the new requirement exists to prevent.

**Migration**: None. No caller remains; the register that used the verified path
(`0x803828`) is written unverified from every site, alongside every other MMR register. The
one-shot MMR *read* retry mechanism (`m_pendingMMRReads`) is a separate capability, is
untouched, and keeps its own requirements in this spec.

## ADDED Requirements

### Requirement: One MMR register is written at one assurance level

The system SHALL NOT write a given MMR register through the verified path at one call site and the unverified path at another. Mixed assurance on one register means whether the setting reaches the machine depends on which code path last wrote it, which is not diagnosable from the machine's behaviour and not reproducible from a log.

Choosing which level a register uses is a per-register decision. Where a register is left unverified, the reason SHALL be stated at the call site, so the choice is visible as a decision rather than an oversight.

No MMR write SHALL be verified by reading the register back. Unverified is not merely the default side of that split — after this change it is the only side, and the verification mechanism is removed rather than left available. Read-back verification is not free on this protocol: an MMR read is itself a write to the read-request characteristic, so verifying a write issues a second write, plus a retry ladder of further writes, onto the same link whose write failures prompted the verification. Neither reference implementation verifies an MMR write at all, while both retry MMR *reads* — which is the asymmetry the protocol actually justifies. Reintroducing read-back verification SHALL require stating the consequence of the write silently not landing, for the specific register, and SHALL account for that cost.

#### Scenario: A register written from several call sites

- **WHEN** an MMR register is written from more than one call site
- **THEN** all of those call sites use the same assurance level

#### Scenario: A register deliberately left unverified

- **WHEN** a register is written unverified
- **THEN** the reason is recorded at the call site
