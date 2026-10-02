# Licensing and trial — proposal

Date: 2026-10-03. Status: proposal for the user's decision (nothing built yet).

## What the market does

- Trials: 7 days (UAD, Waves, Minimal Audio), 14 days (Kiive, McDSP), 30 days (Plugin Alliance), 3 days
  (Vitric). 7–14 is the norm; a 3-day trial is rare and converts worse unless the product is instant.
- Payment: a merchant of record (Lemon Squeezy, Paddle, Gumroad) handles VAT / sales tax worldwide,
  issues license keys and has a validate / activate API with an activation limit per key. Their keys
  are plain strings: the plug-in has to ask the API, there is no signed offline proof.
- Dedicated license layers (LicenseSeat, LicenseLatte) add Ed25519-signed offline licenses, device
  limits and a C++ / JUCE integration for ~$10–30 a month. JUCE itself ships `juce_product_unlocking`
  (`OnlineUnlockStatus`, `KeyGeneration`: RSA-signed key files with optional expiry, machine IDs,
  drag-and-drop offline key files) — free with the JUCE licence we already use, but it needs our own
  little server (or a script) to sign keys.

## Recommendation

**Store:** Lemon Squeezy at $12 (merchant of record, license keys on, activation limit 3 machines).
No own server at the start: the plug-in activates against the Lemon Squeezy License API.

**In the plug-in:**
1. First run writes the trial start into the user settings (`~/Library/Application Support/Swarmness`
   / `%APPDATA%\Swarmness`), **7 days** of the full product. Not reset by reinstalling (the settings
   file stays); moving the clock back is detected (last-seen date).
2. After the trial, **demo mode**: everything works, but the output fades out for 3 s every 2 minutes
   and sessions / user presets are not saved. The product stays try-able for as long as the user wants —
   that is what makes a short trial acceptable.
3. **ACTIVATE** in the header (replaces the trial counter): paste the key → the plug-in calls
   `licenses/activate` with a machine name, stores the answer in the settings as a token signed with a
   secret held in the binary, then checks `licenses/validate` once a month with a 30-day offline grace.
   `DEACTIVATE` frees a machine. No account, no e-mail login inside the plug-in.
4. The whole thing is one class (`Licence`), a header pill (TRIAL · 5 days / DEMO / ACTIVATED) and a
   small dialog; the DSP is untouched except the demo fade.

**What this does not do:** stop a determined cracker (a $12 pedal plug-in should not try). It makes
paying the easy path and keeps honest users working offline.

**Alternatives if you prefer:** Gumroad (simpler, fewer countries' VAT handled) or JUCE key files
signed by a Cloudflare Worker (true offline licences, our own keys, ~2 days more work).

## Open questions

1. 7-day full trial + demo mode after — or 14 days and no demo mode?
2. Lemon Squeezy or Gumroad? (Both take ~5 % + fees; Lemon Squeezy handles EU VAT as merchant of record.)
3. 3 activations per key?
4. Price: $12, or $10 / $15?

## Effort

Lemon Squeezy account and product: an hour. Plug-in side (class, UI, demo mode, tests for trial /
demo / activation paths with a mocked API, README): ~2–3 days. Installer / notarisation are unchanged.
