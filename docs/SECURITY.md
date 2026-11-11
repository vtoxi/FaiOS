# Security

Status of each control is stated as **implemented + tested**, **implemented, not yet tested here**, or **not built**. Nothing is claimed beyond that.

## Secrets (OAuth tokens)

| Control | Status |
|---|---|
| Refresh/access tokens are stored only sealed (`ISecretBox`); if no secure backend exists, sign-in is **refused** (fails closed, no plaintext fallback, authorization code not even exchanged) | implemented + tested |
| AES-256-GCM backend (OpenSSL EVP), PBKDF2-HMAC-SHA256 key derivation, fresh salt + IV per blob, header authenticated as AAD, tamper/wrong-key detection | implemented, **tests skipped until OpenSSL dev files are installed** |
| Tokens never appear in `accounts.json`, in storage in the clear, or in logs (test checks all three). Logs carry a short one-way hash of the account id, never email or tokens | implemented + tested |
| Unreadable/tampered token blob => account marked "needs sign-in", no retry storm | implemented + tested |
| Passwords are never requested or handled. Sign-in happens in an external browser | by design; browser not built |
| OAuth client ID / secret come from configuration, not source. (Google "desktop" client secrets are not confidential; PKCE is what protects the code exchange) | implemented |
| `state` checked on the redirect (CSRF), PKCE S256 on every sign-in, ID token `iss`/`aud`/`exp` checked | implemented + tested |
| ID token signature is **not** verified (permitted by OIDC Core 3.1.3.7 when received directly from the token endpoint over TLS). TLS must therefore verify certificates | implemented as stated; TLS transport **not built** |
| Revocation on sign-out; if it cannot be confirmed (offline) local tokens are still wiped and the log says "NOT confirmed" | implemented + tested |

### Key management limitation (read this)
The phone has no known hardware keystore (hardware unidentified). The AES key is derived from key material supplied by the caller:
- **User PIN**: real protection against someone who copies the flash. Not built into the UI yet.
- **`/etc/machine-id`** (what the simulator uses): lives on the same storage as the ciphertext, so it only stops casual copying of the token file. It does **not** protect against an attacker who can read the whole device. Do not describe it as strong.

## Network
- No real HTTP transport exists yet (`NullHttp` always reports offline). When added it must use TLS with certificate verification on, no custom trust overrides, redirects limited, and timeouts. **Not built.**

## Simulator-only code
`simulator/mock_google.h` fakes Google's token endpoint for UI testing (`faios_sim --mock-google`). Every account it creates is labelled `[MOCK]` and uses fake data. It is not compiled into any device image.

## Data protection
- Account metadata (`accounts.json`: name, email, status, scopes) is personal data stored unencrypted next to the config. Removing an account deletes its entry and tokens. **Encrypting metadata is not built.**
- Local data-retention preferences (spec item) are **not built**.

## Reporting
Not a public project yet; no disclosure process defined.
