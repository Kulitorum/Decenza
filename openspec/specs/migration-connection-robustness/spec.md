# migration-connection-robustness Specification

## Purpose

Covers how a migration client reaches a peer device across a multi-homed local network: a reachability preflight over every candidate interface, bounded retry of transient neighbour-resolution failures, and connection failure messages that report unreachable only when every candidate interface fails.

## Requirements
### Requirement: Interface-aware reachability preflight

When connecting to a migration peer, the client SHALL check reachability over every candidate local interface, not by default source-address selection. A candidate is a local IPv4 source address whose interface subnet contains the target. Candidates SHALL be probed with the default-route interface first, and the first to complete a TCP connection to the peer's `host:port` SHALL be selected. This SHALL apply to discovered and manually entered `IP:port` connections.

#### Scenario: Multi-homed host with two interfaces on the target subnet
- **WHEN** the local host has two active interfaces addressed on the same subnet as the peer and the peer's server is up
- **THEN** the client selects a candidate interface that reaches the peer and proceeds to fetch the manifest, instead of failing with "Host unreachable"

#### Scenario: Single-homed host
- **WHEN** the local host has exactly one interface on the peer's subnet
- **THEN** the preflight probes that one candidate, succeeds, and adds no user-perceptible delay beyond a single connection attempt

#### Scenario: Peer genuinely offline
- **WHEN** no candidate interface can establish a TCP connection to the peer within the preflight window
- **THEN** the client reports the peer as unreachable and does not attempt the HTTP manifest fetch

### Requirement: Off-subnet targets use the default route

When the target is on no directly-connected subnet, the default-route interface SHALL be the sole candidate.

#### Scenario: Off-subnet peer probes only the default route
- **WHEN** the target address is not on any directly-connected subnet
- **THEN** only the default-route interface is probed

### Requirement: Bounded retry on transient connection errors

The manifest fetch that establishes a migration connection SHALL retry a small, bounded number of times when it fails with a transient network error (for example a neighbour-resolution / `EHOSTUNREACH` failure), so that a single cold-neighbour failure does not abort the connection. Retries SHALL NOT be attempted for authentication challenges or for HTTP-level responses, which are terminal outcomes of a successful connection.

#### Scenario: First attempt hits a cold neighbour
- **WHEN** the initial manifest fetch fails with a transient network error but the peer is reachable on a selected interface
- **THEN** the client retries within the bounded limit and completes the connection without user intervention

#### Scenario: Authentication required is not retried
- **WHEN** the manifest fetch returns HTTP 401
- **THEN** the client stops retrying and surfaces the authentication prompt

#### Scenario: Retries exhausted
- **WHEN** every retry of the manifest fetch fails with a transient network error
- **THEN** the client stops and reports a connection failure

### Requirement: Accurate connection failure classification

Connection failure messaging SHALL distinguish between (a) the peer being unreachable on every candidate local interface, (b) the peer being reachable but requiring authentication, and (c) the peer returning an HTTP or malformed-response error. An "unreachable" message SHALL be shown only in case (a).

#### Scenario: Reachable but unauthenticated
- **WHEN** the peer is reachable and responds with an authentication challenge
- **THEN** the user is prompted for a code and no "unreachable" error is shown

#### Scenario: Reachable but returns an error body
- **WHEN** the peer is reachable and returns an HTTP error or an unparseable manifest
- **THEN** the user sees an error describing the bad response, not "unreachable"

#### Scenario: Unreachable on all interfaces
- **WHEN** no candidate local interface can reach the peer
- **THEN** the user sees an unreachable error that reflects that the device could not be contacted on the local network
