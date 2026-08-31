import assert from "node:assert/strict";
import test from "node:test";

import {
  DEFAULT_DEV_SECRET,
  KNOWN_VECTOR_CLAIMS,
  KNOWN_VECTOR_TOKEN,
  canonicalClaimsPayload,
  signClaims,
  verifyToken,
} from "./crypto.ts";

test("canonical payload matches the published vector", () => {
  assert.equal(
    canonicalClaimsPayload(KNOWN_VECTOR_CLAIMS),
    '{"deviceId":"","email":"coach@example.com","expiresAt":1893456000,"issuedAt":1704067200,"keyId":"k_test1","kind":"paid","v":1}',
  );
});

test("HMAC token matches Python/C++ known vector", async () => {
  const token = await signClaims(DEFAULT_DEV_SECRET, KNOWN_VECTOR_CLAIMS);
  assert.equal(token, KNOWN_VECTOR_TOKEN);
});

test("verifyToken accepts the known vector", async () => {
  const claims = await verifyToken(DEFAULT_DEV_SECRET, KNOWN_VECTOR_TOKEN);
  assert.ok(claims);
  assert.equal(claims?.email, "coach@example.com");
  assert.equal(claims?.keyId, "k_test1");
  assert.equal(claims?.expiresAt, 1893456000);
});

test("verifyToken rejects a flipped signature", async () => {
  const tampered = KNOWN_VECTOR_TOKEN.replace(".PLj0", ".XLj0");
  const claims = await verifyToken(DEFAULT_DEV_SECRET, tampered);
  assert.equal(claims, null);
});
