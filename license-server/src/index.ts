import {
  DEFAULT_DEV_SECRET,
  type LicenseClaims,
  signClaims,
  verifyToken,
} from "./crypto";

export interface Env {
  LICENSES: KVNamespace;
  LICENSE_SIGNING_SECRET?: string;
  ADMIN_SECRET?: string;
}

interface StoredLicense {
  keyId: string;
  kind: "trial" | "paid";
  email: string;
  deviceId: string;
  issuedAt: number;
  expiresAt: number;
  token: string;
  revoked: boolean;
}

const TRIAL_DAYS = 14;
const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization",
};

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "Content-Type": "application/json",
      ...CORS_HEADERS,
    },
  });
}

function signingSecret(env: Env): string {
  return env.LICENSE_SIGNING_SECRET || DEFAULT_DEV_SECRET;
}

function unixNow(): number {
  return Math.floor(Date.now() / 1000);
}

function normalizeEmail(email: string): string {
  return email.trim().toLowerCase();
}

function newKeyId(prefix: string): string {
  const bytes = new Uint8Array(8);
  crypto.getRandomValues(bytes);
  let hex = "";
  for (const byte of bytes) {
    hex += byte.toString(16).padStart(2, "0");
  }
  return `${prefix}${hex}`;
}

async function readJson(request: Request): Promise<Record<string, unknown>> {
  try {
    const parsed = await request.json();
    if (parsed && typeof parsed === "object") {
      return parsed as Record<string, unknown>;
    }
  } catch {
    // fall through
  }
  return {};
}

function stringField(object: Record<string, unknown>, key: string): string {
  const value = object[key];
  return typeof value === "string" ? value.trim() : "";
}

function numberField(object: Record<string, unknown>, key: string, fallback: number): number {
  const value = object[key];
  return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

async function putLicense(env: Env, record: StoredLicense): Promise<void> {
  await env.LICENSES.put(`license:${record.keyId}`, JSON.stringify(record));
  if (record.kind === "trial" && record.deviceId) {
    await env.LICENSES.put(`trial:${record.deviceId}`, record.keyId);
  }
  if (record.email) {
    await env.LICENSES.put(`email:${normalizeEmail(record.email)}`, record.keyId);
  }
}

async function getLicense(env: Env, keyId: string): Promise<StoredLicense | null> {
  const raw = await env.LICENSES.get(`license:${keyId}`);
  if (!raw) return null;
  return JSON.parse(raw) as StoredLicense;
}

function publicLicense(record: StoredLicense) {
  return {
    keyId: record.keyId,
    kind: record.kind,
    email: record.email,
    deviceId: record.deviceId,
    issuedAt: record.issuedAt,
    expiresAt: record.expiresAt,
    revoked: record.revoked,
  };
}

async function startTrial(env: Env, deviceId: string, offeredToken: string): Promise<Response> {
  if (!deviceId) {
    return jsonResponse({ ok: false, reason: "missing_device" }, 400);
  }

  const existingId = await env.LICENSES.get(`trial:${deviceId}`);
  if (existingId) {
    const existing = await getLicense(env, existingId);
    if (existing) {
      return jsonResponse({ ok: true, token: existing.token, license: publicLicense(existing) });
    }
  }

  const secret = signingSecret(env);
  let claims = offeredToken ? await verifyToken(secret, offeredToken) : null;
  const now = unixNow();
  if (!claims || claims.kind !== "trial" || (claims.deviceId && claims.deviceId !== deviceId)) {
    claims = {
      v: 1,
      kind: "trial",
      email: "",
      deviceId,
      keyId: newKeyId("t_"),
      issuedAt: now,
      expiresAt: now + TRIAL_DAYS * 24 * 60 * 60,
    };
  } else {
    claims = { ...claims, deviceId };
  }

  const token = await signClaims(secret, claims);
  const record: StoredLicense = {
    keyId: claims.keyId,
    kind: "trial",
    email: "",
    deviceId,
    issuedAt: claims.issuedAt,
    expiresAt: claims.expiresAt,
    token,
    revoked: false,
  };
  await putLicense(env, record);
  return jsonResponse({ ok: true, token, license: publicLicense(record) });
}

async function activatePaid(env: Env, email: string, token: string, deviceId: string): Promise<Response> {
  const secret = signingSecret(env);
  const claims = await verifyToken(secret, token);
  if (!claims || claims.kind !== "paid") {
    return jsonResponse({ ok: false, reason: "invalid_key" }, 400);
  }
  if (normalizeEmail(claims.email) !== normalizeEmail(email)) {
    return jsonResponse({ ok: false, reason: "email_mismatch" }, 400);
  }
  const now = unixNow();
  if (now >= claims.expiresAt) {
    return jsonResponse({ ok: false, reason: "expired" }, 400);
  }

  let record = await getLicense(env, claims.keyId);
  if (!record) {
    record = {
      keyId: claims.keyId,
      kind: "paid",
      email: normalizeEmail(claims.email),
      deviceId: "",
      issuedAt: claims.issuedAt,
      expiresAt: claims.expiresAt,
      token,
      revoked: false,
    };
  }
  if (record.revoked) {
    return jsonResponse({ ok: false, reason: "revoked" }, 403);
  }
  if (record.deviceId && record.deviceId !== deviceId) {
    return jsonResponse({ ok: false, reason: "other_device" }, 409);
  }
  record.deviceId = deviceId;
  record.token = token;
  await putLicense(env, record);
  return jsonResponse({ ok: true, token: record.token, license: publicLicense(record) });
}

async function checkLicense(env: Env, token: string, deviceId: string): Promise<Response> {
  const secret = signingSecret(env);
  const claims = await verifyToken(secret, token);
  if (!claims) {
    return jsonResponse({ ok: false, reason: "invalid_key" }, 400);
  }
  const now = unixNow();
  let record = await getLicense(env, claims.keyId);
  if (!record) {
    record = {
      keyId: claims.keyId,
      kind: claims.kind,
      email: claims.email,
      deviceId: claims.deviceId || deviceId,
      issuedAt: claims.issuedAt,
      expiresAt: claims.expiresAt,
      token,
      revoked: false,
    };
    await putLicense(env, record);
  }
  if (record.revoked) {
    return jsonResponse({ ok: false, reason: "revoked", license: publicLicense(record) }, 403);
  }
  if (now >= record.expiresAt) {
    return jsonResponse({ ok: false, reason: "expired", license: publicLicense(record) }, 400);
  }
  if (record.deviceId && record.deviceId !== deviceId) {
    return jsonResponse({ ok: false, reason: "other_device", license: publicLicense(record) }, 409);
  }
  if (!record.deviceId) {
    record.deviceId = deviceId;
    await putLicense(env, record);
  }
  return jsonResponse({ ok: true, token: record.token, license: publicLicense(record) });
}

/**
 * PAYMENTS EXTENSION POINT
 *
 * When Stripe or MercadoPago is wired, the webhook should verify the payment
 * then call issuePaidLicense() with the payer's email and period. Email the
 * returned token (or a .ava-license file) to the coach. Do not put payment
 * provider secrets in the Qt app.
 */
export async function issuePaidLicense(
  env: Env,
  email: string,
  days: number,
): Promise<{ token: string; license: StoredLicense }> {
  const now = unixNow();
  const claims: LicenseClaims = {
    v: 1,
    kind: "paid",
    email: normalizeEmail(email),
    deviceId: "",
    keyId: newKeyId("k_"),
    issuedAt: now,
    expiresAt: now + Math.max(1, days) * 24 * 60 * 60,
  };
  const token = await signClaims(signingSecret(env), claims);
  const record: StoredLicense = {
    keyId: claims.keyId,
    kind: "paid",
    email: claims.email,
    deviceId: "",
    issuedAt: claims.issuedAt,
    expiresAt: claims.expiresAt,
    token,
    revoked: false,
  };
  await putLicense(env, record);
  return { token, license: record };
}

function requireAdmin(request: Request, env: Env): boolean {
  const expected = env.ADMIN_SECRET;
  if (!expected) return false;
  const header = request.headers.get("Authorization") || "";
  const prefix = "Bearer ";
  if (!header.startsWith(prefix)) return false;
  return header.slice(prefix.length) === expected;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS_HEADERS });
    }

    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";

    if (request.method === "GET" && path === "/health") {
      return jsonResponse({ ok: true });
    }

    if (request.method === "POST" && path === "/v1/trial/start") {
      const body = await readJson(request);
      return startTrial(env, stringField(body, "deviceId"), stringField(body, "token"));
    }

    if (request.method === "POST" && path === "/v1/license/activate") {
      const body = await readJson(request);
      return activatePaid(
        env,
        stringField(body, "email"),
        stringField(body, "token"),
        stringField(body, "deviceId"),
      );
    }

    if (request.method === "POST" && path === "/v1/license/check") {
      const body = await readJson(request);
      return checkLicense(env, stringField(body, "token"), stringField(body, "deviceId"));
    }

    if (request.method === "POST" && path === "/v1/admin/issue") {
      if (!requireAdmin(request, env)) {
        return jsonResponse({ ok: false, reason: "unauthorized" }, 401);
      }
      const body = await readJson(request);
      const email = stringField(body, "email");
      const days = numberField(body, "days", 365);
      if (!email) return jsonResponse({ ok: false, reason: "missing_email" }, 400);
      const issued = await issuePaidLicense(env, email, days);
      return jsonResponse({
        ok: true,
        token: issued.token,
        license: publicLicense(issued.license),
      });
    }

    if (request.method === "POST" && path === "/v1/admin/revoke") {
      if (!requireAdmin(request, env)) {
        return jsonResponse({ ok: false, reason: "unauthorized" }, 401);
      }
      const body = await readJson(request);
      const keyId = stringField(body, "keyId");
      const record = await getLicense(env, keyId);
      if (!record) return jsonResponse({ ok: false, reason: "not_found" }, 404);
      record.revoked = true;
      await putLicense(env, record);
      return jsonResponse({ ok: true, license: publicLicense(record) });
    }

    if (request.method === "GET" && path === "/v1/admin/introspect") {
      if (!requireAdmin(request, env)) {
        return jsonResponse({ ok: false, reason: "unauthorized" }, 401);
      }
      const keyId = url.searchParams.get("keyId") || "";
      const email = url.searchParams.get("email") || "";
      let resolvedId = keyId;
      if (!resolvedId && email) {
        resolvedId = (await env.LICENSES.get(`email:${normalizeEmail(email)}`)) || "";
      }
      if (!resolvedId) return jsonResponse({ ok: false, reason: "not_found" }, 404);
      const record = await getLicense(env, resolvedId);
      if (!record) return jsonResponse({ ok: false, reason: "not_found" }, 404);
      return jsonResponse({ ok: true, license: publicLicense(record) });
    }

    return jsonResponse({ ok: false, reason: "not_found" }, 404);
  },
};
