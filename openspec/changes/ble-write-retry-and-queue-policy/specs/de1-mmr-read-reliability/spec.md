## ADDED Requirements

### Requirement: One MMR register is written at one assurance level

The system SHALL NOT write a given MMR register through the verified path at one call site and the unverified path at another. Mixed assurance on one register means whether the setting reaches the machine depends on which code path last wrote it, which is not diagnosable from the machine's behaviour and not reproducible from a log.

Choosing which level a register uses is a per-register decision. Where a register is left unverified, the reason SHALL be stated at the call site, so the choice is visible as a decision rather than an oversight.

Unverified SHALL be the default side of that split. Read-back verification is not free on this protocol: an MMR read is itself a write to the read-request characteristic, so verifying a write issues a second write, plus a retry ladder of further writes, onto the same link whose write failures prompted the verification. Neither reference implementation verifies an MMR write at all, while both retry MMR *reads* — which is the asymmetry the protocol actually justifies. A register SHALL be raised to the verified level only where the consequence of the write silently not landing is itself stated.

#### Scenario: A register written from several call sites

- **WHEN** an MMR register is written from more than one call site
- **THEN** all of those call sites use the same assurance level

#### Scenario: A register deliberately left unverified

- **WHEN** a register is written unverified
- **THEN** the reason is recorded at the call site
