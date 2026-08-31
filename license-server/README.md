# AVA license API — see ../DISTRIBUTION.md

A small Cloudflare Worker that records trials, activations, and license checks.

```
POST /v1/trial/start      { deviceId, token? }
POST /v1/license/activate { email, token, deviceId }
POST /v1/license/check    { token, deviceId }
POST /v1/admin/issue      { email, days }     Authorization: Bearer ADMIN_SECRET
POST /v1/admin/revoke     { keyId }
GET  /v1/admin/introspect ?keyId= | ?email=
GET  /health
```

Payments are not wired. `issuePaidLicense()` in `src/index.ts` is the extension point for a future Stripe/MercadoPago webhook.
