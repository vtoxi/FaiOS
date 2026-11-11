# Fai Browser and Fai YouTube: engine evaluation

Status 2026-11-11. **No engine is linked yet and none is chosen as final.** The Digit 4G's RAM, CPU, GPU and OS are
unknown (see HARDWARE_DISCOVERY.md), and the choice depends on them. What exists today is the engine-independent part
(`core/browser.h`): the URL policy, the `IBrowserEngine` interface and a `TabManager` that enforces resource budgets,
all unit-tested with a fake engine. Nothing here browses the web.

## Evidence used (web research, 2026-11-11; indirect, not benchmarks on this phone)

| Claim | Source |
|---|---|
| WPE WebKit is the embedded WebKit port; minimum graphics needs are OpenGL ES 2.0 and buffer sharing; Buildroot, Yocto and PTXdist are supported build systems; Cog is the reference browser | [Igalia, "Building WPE for an embedded device"](https://people.igalia.com/psaavedra/slides/webengines-hackfest-2021-wpe-embedded/), [webkit.org/wpe](https://webkit.org/wpe/) |
| One developer limited Cog on an i.MX8 to a web process of 140 MB and about 182 MB total. One user's tuning, **not a documented minimum** | [Igalia/WPE slides](https://www.slideshare.net/slideshow/embedding-wpe-webkit-from-bringup-to-maintenance/267644796) |
| Digi recommends reduced-cache settings when total RAM is 1 GiB or less (their Yocto setup) | [Digi docs](https://docs.digi.com/resources/documentation/digidocs/embedded/dey/3.0/cc6/yocto_t_webkit-use) |
| Building WPE needs a lot of RAM on the build host (about 8 GB plus swap reported), not on the device | [WPEWebKit issue](https://github.com/WebPlatformForEmbedded/WPEWebKit/issues/1569) |
| Servo is designed for embedding but is still maturing; Ladybird is desktop-first, not for embedding; NetSurf is tiny because it has no JavaScript | [The Register](https://www.theregister.com/2025/11/18/servo_002_arrives/), [comparison post](https://www.pistack.xyz/posts/2026-09-24-ladybird-vs-servo-vs-webkitgtk-browser-engines/) (a blog; verify before relying on it) |
| YouTube asks the browser `MediaSource.isTypeSupported()` and serves VP9 when it says yes, otherwise H.264; software VP9 on weak hardware plays badly | [Mozilla bug 1614958](https://bugzilla.mozilla.org/show_bug.cgi?id=1614958) and related |

**Not found:** an official minimum RAM for WPE, and any official YouTube requirement for embedded browsers. Both numbers
must be MEASURED on the target.

## Candidates

| Engine | JavaScript / modern web | Fits a small phone? | Verdict |
|---|---|---|---|
| **WPE WebKit + Cog** | Yes, full modern engine, MSE via GStreamer | Heavy for a feature phone. Needs GLES2-class GPU path and, realistically, several hundred MB RAM; unknown until the hardware is known | **Preferred**, if the hardware can carry it |
| Servo | Partial, maturing | Embeddable, but young and a Rust toolchain port | Watch; revisit later |
| NetSurf | No JavaScript | Very light | Only for a reader-mode fallback (static pages) |
| Ladybird | Maturing | Desktop-first, not embeddable | Rejected for now |
| Chromium / CEF | Yes | Far too large for this class of device | Rejected |
| litehtml / custom | Minimal | Light | Only as a text fallback, not a "browser" |

## Decision (provisional)

1. Code against `IBrowserEngine`. The UI and policy never name an engine.
2. **Preferred backend: WPE WebKit via Cog**, but only after hardware discovery shows: a usable GPU/GLES path or an
   accepted software-rendering cost, enough RAM for at least one tab under the budget below, and a working network stack.
3. If the phone cannot carry it, ship a **reader-mode fallback** (fetch, strip, render text) and say so in the UI. No
   claim of "full browser" until a real page renders.
4. Anything not measured stays labelled UNVERIFIED.

## Resource budgets (defaults, in `BrowserBudget`; tune from measurements)

4 tabs open, 2 live renderers, 150 MB for all live renderers, 100 MB for one tab. Background tabs are suspended
(renderer destroyed, URL kept) least recently used first. A tab over its own limit is discarded, even in the foreground.
These are placeholders chosen to be conservative, not measured numbers.

## URL and security policy (implemented, tested)

Only `http` and `https`. Refused: `javascript:`, `data:`, `file:`, `ftp:`, `blob:`, `about:`, `chrome:`, `mailto:`, `tel:`,
URLs with `user:pass@` (spoofing), control characters, over-long addresses and malformed hosts. A bare host defaults to
`https`. Still to do with a real engine: certificate errors shown plainly and never silently bypassed, private browsing,
cookie/storage limits, download confirmation, no access to FaiOS storage from web content.

## Fai YouTube

Plan: load the official YouTube site or the official embedded player inside the browser engine. **No** private API
scraping, no DRM bypass, no downloading. Playback needs MSE plus a decoder. WebKit's GStreamer backend uses whatever
decoders exist on the device. Whether the phone has hardware H.264/VP9 decoding is UNKNOWN; without it, software
decoding on a weak CPU may be unusable, and YouTube may refuse an old engine build. This can only be answered by trying
the real engine on the real hardware. Until then Fai YouTube is not implemented and must not appear as working.

## GStreamer evaluation (pending)

Needed for WPE media playback and also for the Music/Videos/Recorder apps once real audio exists. Open questions that
need hardware answers: which codecs the SoC decodes, the audio path (ALSA?), and the plugin set size on a small image.

## Next steps

1. Hardware discovery (SoC, RAM, GPU, display, kernel).
2. Build `wpewebkit` + `cog` in the QEMU image and measure real memory per tab (this proves the stack, not the phone).
3. Implement `WpeEngine : IBrowserEngine`; keep the fake engine for tests.
4. Browser UI (address bar, tabs, text entry: needs the T9/multi-tap input work first).
