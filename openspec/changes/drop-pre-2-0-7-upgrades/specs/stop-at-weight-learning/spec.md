# Spec Delta

## REMOVED Requirements

### Requirement: Pre-Basket History Is Copied Once Into The Combinations Actually Pulled
**Reason**: The copy shipped in 2.0.3. Every installation at or above the upgrade floor (2.0.7) has already run it. The copied buckets it produced remain, and the "Copied buckets do not vote in the bootstrap" requirement still governs them.
**Migration**: Install Decenza 2.1.0 first to bring older learning data past this step.

### Requirement: The copy closes only on a successful read
**Reason**: It qualifies the removed copy.
**Migration**: None.

### Requirement: Each copy outcome is recorded
**Reason**: It qualifies the removed copy.
**Migration**: None.
