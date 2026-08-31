# Distributing AVA to coaches

Short operator guide for Camila. The Qt UI did not change; this adds a 14-day trial, paid license keys, a small license API, and Mac packaging.

## What we built

1. **In the app (first launch)**  
   AVA starts a **14-day trial bound to this Mac**. Device id = `IOPlatformUUID` from IOKit (`mac-<uuid>`). Reinstalling the app on the same Mac keeps the same id.

2. **While the trial / paid license is valid**  
   Tagging, export, and Presentation work as today, including **offline**. Coaches at a pitch with bad wifi are fine.

3. **When it expires (or 7 days after the last successful online check)**  
   The app still opens. It shows a lock screen (EN+ES): trial ended / connect once to refresh / paste a license key / write to Camila. It does not crash. Network errors never lock on the first failure — only after the 7-day grace is used up.

4. **Paid license**  
   You issue a signed key (`AVA1.…`) with `scripts/issue-license`. The coach pastes email + key (or loads a `.ava-license` file). The key contains the expiry date. No passwords.

5. **License API (optional but recommended)**  
   Cloudflare Worker in `license-server/`: start trial, activate key, check license, admin issue/introspect/revoke. If the Worker is not configured, local signed keys and the local trial still work.

**Shape we chose:** license keys first, payments later. A Stripe/MercadoPago webhook should call `issuePaidLicense()` in `license-server/src/index.ts` and email the token. Do not put payment secrets in the Mac app.

## Coach first launch

1. Coach downloads `AVA.dmg` (notarized) and drags AVA to Applications.
2. Opens AVA. 14-day trial starts on that Mac.
3. Welcome screen shows remaining trial days. They can work fully offline.
4. After 14 days, lock screen. They paste the key you sent. If they have wifi, the Worker binds the key to that Mac so a zip of the `.app` sent to a friend does not carry the paid license.

## Issue a trial vs a paid key

- **Trial:** automatic on first launch. You do not issue anything. One trial per Mac once the Worker has seen that device id.
- **Paid key:**

```bash
export AVA_LICENSE_SIGNING_SECRET='the-same-secret-compiled-into-AVA'
./scripts/issue-license --email coach@club.cl --days 365 --out coach.ava-license
```

Email the token or the `.ava-license` file. Optional: `--register` (needs `AVA_LICENSE_API_URL` and `AVA_LICENSE_ADMIN_SECRET`) so the Worker stores the key for device binding.

`--self-test` checks that Python HMAC matches the C++ / Worker test vector.

## Build / sign / notarize / upload (on your Mac)

Cloud agents **cannot** produce a notarized build. Developer ID lives on your Mac.

```bash
# 1) One-time: Apple Developer Program, certificates in Keychain, app-specific password
xcrun notarytool store-credentials "ava-notarize" \
  --apple-id "YOUR_APPLE_ID@email" \
  --team-id "YOUR_TEAM_ID" \
  --password "app-specific-password"

# 2) Production secret + Worker URL (must match the Worker secrets)
export AVA_LICENSE_SIGNING_SECRET='your-long-random-secret'
export AVA_LICENSE_API_URL='https://ava-license.YOUR_SUBDOMAIN.workers.dev'

# 3) Build a .app that coaches can run without installing Qt
./AVA_V01_cpp/scripts/package_macos.sh

# 4) Sign + notarize (skip this and Gatekeeper will scare coaches)
export AVA_CODESIGN_IDENTITY='Developer ID Application: Camila Escudero (TEAMID)'
./AVA_V01_cpp/scripts/sign_and_notarize.sh
```

Upload `AVA_V01_cpp/dist/AVA.dmg` (or `AVA.app.zip`) wherever you already send files: your site, Google Drive, Cloudflare R2. There is no in-app updater.

Copy `AVA_V01_cpp/config/license_server.json.example` → `license_server.json` with your Worker URL before packaging if you did not pass `AVA_LICENSE_API_URL` at CMake time.

Also set the same signing secret on the Worker:

```bash
cd license-server
npx wrangler kv namespace create AVA_LICENSES   # paste the id into wrangler.jsonc
npx wrangler secret put LICENSE_SIGNING_SECRET
npx wrangler secret put ADMIN_SECRET
npx wrangler deploy
```

Local API: copy `.dev.vars.example` → `.dev.vars` and `npx wrangler dev`.

## Still manual

| You still do | Why |
| --- | --- |
| Apple Developer account, signing, notarization | Only your Mac / Keychain can hold Developer ID |
| Host the `.dmg` | No store listing in this PR |
| Take payment (transfer, Stripe, MercadoPago) | Keys first; payments are an extension point |
| Email keys to coaches | `issue-license` prints the token; it does not send mail |
| Install FFmpeg on coach Macs for export | `brew install ffmpeg` — bundling FFmpeg is a follow-up |
| Replace the in-repo HMAC secret before shipping | Default secret is public in git; anyone can mint keys until you rebuild |

## Honest limits

This stops a coach zipping `AVA.app` and handing a **paid** copy to a friend (the paid key is not inside the `.app`; it is bound to the first Mac that activates it online). It does not stop a determined pirate. The HMAC secret is in the binary; they can patch `isEntitled()` or mint keys. Do not overbuild. Deleting local prefs can start another **offline** trial until the Worker has seen that Mac.

## If wifi is down

Honor the signed local license. If the app has successfully talked to the Worker at least once, it may keep working **7 days** without a check-in, then asks to connect once. A stadium with no wifi for a weekend is OK. The first network error never bricks the app.
