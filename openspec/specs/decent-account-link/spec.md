# decent-account-link Specification

## Purpose
Lets the user link Decenza to their decentespresso.com account so Decent API features — shot upload first — can act on that account. Uses the same email + encrypted-password scheme as de1app and Decaid.

## Requirements

### Requirement: Linking exchanges the password for the server's encrypted password

When the user links an account, the system SHALL send the entered email and plaintext password once to `GET https://decentespresso.com/support/api/login_test` using HTTP Basic authentication. If the response is HTTP 200 with a trimmed body that is non-empty and not `0`, the system SHALL store the email and that body (the encrypted password) as the linked account, and SHALL switch uploads to Decent on. The system SHALL NOT persist the plaintext password anywhere, and SHALL clear it from the input field once the request completes, whether it succeeded or failed.

#### Scenario: Successful link
- **WHEN** the user enters a valid email and password and confirms
- **THEN** the system calls `login_test` with Basic `email:password`
- **AND** stores the email and the returned encrypted password
- **AND** shows the account as connected ("Connected as <email>") and switches Decent on
- **AND** the plaintext password exists in no setting, log line or file

#### Scenario: Wrong password
- **WHEN** `login_test` returns `0` or an empty body
- **THEN** no credentials are stored
- **AND** the user is told the email or password was not accepted

#### Scenario: Network failure while linking
- **WHEN** the `login_test` request fails at the transport level or times out
- **THEN** no credentials are stored
- **AND** the user is told the Decent server could not be reached, which is distinct from the wrong-password message

#### Scenario: Server error while linking
- **WHEN** `login_test` returns a non-200 status, or a 200 whose body is not a token (e.g. a captive portal's page)
- **THEN** no credentials are stored
- **AND** the user is told the server had a problem and to try again later

### Requirement: Authenticated calls use HTTP Basic with the encrypted password

Every subsequent Decent API call SHALL authenticate with HTTP Basic, with username = the stored email and password = the stored encrypted password. Credentials SHALL NOT be placed in URL query parameters or cookies.

#### Scenario: Authenticated request header
- **WHEN** the system makes any Decent API call for a linked account
- **THEN** the request carries `Authorization: Basic base64(email:encryptedPassword)`
- **AND** the request URL contains neither the email nor the encrypted password

### Requirement: Encrypted password is stored as a secret

The stored encrypted password SHALL be treated as an account secret. It SHALL NOT be written to logs, returned by any MCP tool, or included in any ShotServer page or API response. Where a surface shows the account, it SHALL show only the email and the linked/needs-sign-in state.

#### Scenario: MCP settings read
- **WHEN** an MCP client reads the Decent settings
- **THEN** the response includes whether an account is linked, the email, and the upload settings
- **AND** the response does not contain the encrypted password

#### Scenario: Log output
- **WHEN** any Decent API request or response is logged
- **THEN** the log line contains neither the Authorization header, the encrypted password nor the account email

### Requirement: Rejected credentials put the account in a needs-sign-in state

When any authenticated Decent API call returns HTTP 401, the system SHALL mark the linked account as needing sign-in, SHALL send the stored credentials on no further Decent API call, and SHALL show the state where the account is displayed. The stored email SHALL remain so the user only re-enters the password. A successful re-link SHALL clear the state and resume uploads.

#### Scenario: Password changed on the website
- **WHEN** an upload returns HTTP 401
- **THEN** the account shows "Sign in again" instead of "Connected as <email>"
- **AND** no further automatic uploads are attempted until the user re-links

#### Scenario: Re-link resumes
- **WHEN** the user re-enters a valid password for an account in the needs-sign-in state
- **THEN** the state clears and automatic uploads resume

### Requirement: Unlinking removes the credentials

Unlinking SHALL delete the stored email and encrypted password and SHALL stop all automatic Decent API activity immediately, including any queued Upload missing shots work that has not yet been sent. Unlinking SHALL NOT delete local shots or their recorded upload state.

#### Scenario: Disconnect during a sign-in
- **WHEN** the user taps Disconnect while a sign-in is still waiting for `login_test`
- **THEN** the sign-in is cancelled, and its answer does not link the account or switch Decent on

#### Scenario: Unlink
- **WHEN** the user unlinks the account
- **THEN** the email and encrypted password are no longer stored
- **AND** no Decent API request is sent afterwards
- **AND** shots already marked uploaded keep that mark

### Requirement: Open the Decent account in the browser already signed in

The system SHALL offer an action that opens the user's shot history on decentespresso.com without a second login. It SHALL call `GET /support/api/authenticated_redirect?dest=/support/espressomachine` with the linked account's credentials, and open the returned single-use `url` in the system browser. If that call fails, the system SHALL open `https://decentespresso.com/support/espressomachine` unauthenticated, so the user can still sign in there.

#### Scenario: Open signed in
- **WHEN** the user chooses "View my shots on decentespresso.com" with a linked account
- **THEN** the system requests an authenticated redirect and opens the returned URL in the browser

#### Scenario: Redirect unavailable
- **WHEN** the authenticated-redirect request fails
- **THEN** the browser opens `https://decentespresso.com/support/espressomachine`
