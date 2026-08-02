# mcp-tool-error-reporting Specification

## Purpose

A tool call that failed must be identifiable as failed by a client that reads
only the protocol, without parsing the rendered text of the result.

## ADDED Requirements

### Requirement: A tool result carrying an error is marked as a failed tool call

When a tool's result object contains an `error` key, the tool-call response SHALL
carry `isError: true`. The marking SHALL be applied where the result is wrapped
into its content envelope, so that it cannot be lost by the wrapping, and SHALL
apply to every tool without each tool opting in.

#### Scenario: A tool returns an error

- **WHEN** a tool's result object contains an `error` key
- **THEN** the tool-call response carries `isError: true`

#### Scenario: A tool succeeds

- **WHEN** a tool's result object contains no `error` key
- **THEN** the tool-call response does not carry `isError: true`

#### Scenario: The error survives the content wrapping

- **WHEN** a failing tool's result is wrapped into its content envelope
- **THEN** the failure marking is present on the envelope, not only inside the wrapped payload

### Requirement: The error text remains readable in the result content

Marking a tool call as failed SHALL NOT remove the error text from the response
content. A model reading the result SHALL still be able to read what went wrong.

#### Scenario: Reading a failed tool call

- **WHEN** a client receives a failed tool call
- **THEN** the response content still contains the tool's error message

### Requirement: Tool failures are not JSON-RPC errors

A tool that fails SHALL still produce a JSON-RPC `result`, not a JSON-RPC
`error`. JSON-RPC `error` is reserved for protocol faults — an unknown method, a
malformed request, a rejected session. A tool that ran and reported a problem is
a successful protocol exchange carrying a failed tool call.

#### Scenario: A tool fails

- **WHEN** a tool reports an error
- **THEN** the JSON-RPC response carries a `result` member and no `error` member

#### Scenario: A protocol fault occurs

- **WHEN** a request names an unknown method or is malformed
- **THEN** the JSON-RPC response carries an `error` member, unchanged from today

### Requirement: One place decides what a failed tool call looks like

The rule SHALL be applied in a single shared path. No individual tool, and no
individual call site, SHALL hand-roll the failure marking.

#### Scenario: The confirmation-denial path

- **WHEN** a user denies confirmation for a tool that requires it
- **THEN** the response is marked failed by the same shared path as any other tool failure, not by a separate assignment at that call site

#### Scenario: A newly added tool

- **WHEN** a new tool is added that reports failure with an `error` key
- **THEN** its failures are marked without any change to the tool itself
