# Google sign-in on FaiOS: what is possible and what is built

Sources are Google's own documentation, checked 2026-11-11. Re-check before relying on any of it; Google changes these rules.

## Facts that shape the design

1. **The device authorization flow cannot be used for Contacts or Calendar.** Google's page for "OAuth 2.0 for TV and Limited-Input Device Applications" says the flow supports only: `email`, `openid`, `profile`, `drive.appdata`, `drive.file`, `youtube`, `youtube.readonly`. No Calendar or People/Contacts scopes. (https://developers.google.com/identity/protocols/oauth2/limited-input-device)
2. **Embedded webviews are blocked.** Google rejects authorization requests from embedded webviews with `disallowed_useragent` (policy announced 2021). Sign-in has to run in an external user-agent. (https://developers.googleblog.com/2021/06/upcoming-security-changes-to-googles-oauth-2.0-authorization-endpoint.html)
3. Therefore Contacts/Calendar sync needs the **installed-app (loopback redirect) flow with PKCE**, in a browser the user can see the address bar of. On a keypad phone that means the on-device browser (not built), or a companion device. **Unconfirmed:** whether Google would accept a small embedded-engine browser as a legitimate external user-agent, and whether it would render its sign-in page acceptably on this hardware. This must be tested, not assumed.
4. **Verification.** Google states public apps using sensitive-data scopes must complete its verification process. Contacts and Calendar are sensitive scopes; until verified, an app can only be used by listed test users and shows an "unverified app" screen. **Not researched in depth yet**: the exact test-user limit and verification requirements need to be read from the current consent-screen documentation before launch.
5. **FaiOS cannot ship a Google client.** The project owner must create a Google Cloud project, consent screen and OAuth client and put the client ID in `config.ini` (`google_client_id`, `google_redirect_uri`, optional `google_client_secret`). FaiOS never contains these values.

## Scopes (least privilege, incremental)
| Step | Scopes | Why |
|---|---|---|
| Sign-in (implemented) | `openid email profile` | identify the account |
| Contacts read (planned) | `contacts.readonly` | download contacts |
| Contacts write (later) | `contacts` | only when create/update/delete UI exists |
| Calendar read (planned) | `calendar.readonly` | list events |
| Calendar write (later) | `calendar.events` | only when event editing exists |

Requested with `include_granted_scopes=true` so later requests add to earlier consent. If the user narrows consent in their Google account, the next refresh reports the smaller scope set and FaiOS switches the affected sync off.

## What is built (see SECURITY.md for security status)
- PKCE (S256), authorization URL, redirect parsing with `state` check, code exchange, refresh, revoke, ID-token claim checks: **implemented, tested against a mock transport**.
- Account manager, token lifecycle, re-auth handling, sign-out with revocation: **implemented, tested against a mock transport**.
- Settings -> Accounts -> Add account -> Google: shows exactly which prerequisite is missing (client ID, secure storage, external browser).
- **Not built:** real HTTP/TLS transport, external-browser launch and loopback listener, Contacts/Calendar sync, SQLite repository, offline queue, conflict resolution, Gmail/Drive.
- **Not verified against real Google at all.** Every test uses a scripted fake. First real sign-in needs a client ID from the owner and the pieces above.
