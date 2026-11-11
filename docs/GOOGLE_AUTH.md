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
- **Built, tested against a fake only (`MockGoogleApi`):** Contacts and Calendar sync, offline queue, conflict handling, scheduler (`core/sync.*`, `core/contacts.*`). Request shapes follow Google's documentation; real behaviour is UNVERIFIED.
- **Not built:** real HTTP/TLS transport, external-browser launch and loopback listener, SQLite repository (JSON files today), Gmail/Drive.

## Sync design (Contacts and Calendar)

- **Offline-first.** The UI reads only local stores. A local add/edit/delete sets `dirty` (or a tombstone for a synced item). That flag is the persistent queue; it survives restarts and a failed sync never clears it.
- **Pull then push.** Pull is incremental (`syncToken`; first sync lists contacts fully and the last 30 days of events). All pages are fetched before anything is applied, and the new sync token is saved only after the pull was applied and saved. Expired tokens (People: 400 EXPIRED_SYNC_TOKEN, Calendar: 410) trigger one full resync.
- **Conflicts never destroy data.** If both sides changed an item, the account's version keeps the original and the local edit becomes a separate "(local)" copy that is uploaded. Remote delete vs local edit keeps the edit and re-creates it. Local delete vs remote edit: the delete wins. Etag mismatches found at push time (People 400 FAILED_PRECONDITION, Calendar 412) take the same path.
- **Scheduler.** Every 15 min while healthy; offline or server errors back off 30 s, 60 s ... capped at 15 min; no link = no attempt and no backoff; an authorisation problem stops automatic retries until the user acts ("Sync now" still works).
- **Safety.** Server-supplied ids are validated before they go into a URL (no path/query injection); the access token is sent only in the Authorization header and never logged; a loop guard bounds retries per item; paging is capped at 50 pages; removing an account deletes the items synced from it.
- **Known limits.** Sync needs the write scopes (`contacts`, `calendar.events`); with read-only scopes pushes get 403 and show "permission denied". Time zones: downloaded events keep their own wall-clock time, uploaded ones use the configured UTC offset. A contact keeps one name (sent as givenName) and one number. Events spanning midnight are shown on their start day. Recurring events are expanded by Google (`singleEvents=true`); editing a single instance is not modelled.
- **Not verified against real Google at all.** Every test uses a scripted fake. First real sign-in needs a client ID from the owner and the pieces above.
