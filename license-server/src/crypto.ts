export interface LicenseClaims {
  v: 1;
  kind: "trial" | "paid";
  email: string;
  deviceId: string;
  keyId: string;
  issuedAt: number;
  expiresAt: number;
}

const TOKEN_PREFIX = "AVA1.";

function jsonEscape(value: string): string {
  return value
    .replaceAll("\\", "\\\\")
    .replaceAll('"', '\\"')
    .replaceAll("\n", "\\n")
    .replaceAll("\r", "\\r")
    .replaceAll("\t", "\\t");
}

export function canonicalClaimsPayload(claims: LicenseClaims): string {
  return (
    '{"deviceId":"' +
    jsonEscape(claims.deviceId) +
    '","email":"' +
    jsonEscape(claims.email) +
    '","expiresAt":' +
    String(claims.expiresAt) +
    ',"issuedAt":' +
    String(claims.issuedAt) +
    ',"keyId":"' +
    jsonEscape(claims.keyId) +
    '","kind":"' +
    jsonEscape(claims.kind) +
    '","v":1}'
  );
}

export function bytesToBase64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const byte of bytes) {
    binary += String.fromCharCode(byte);
  }
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replaceAll("=", "");
}

export function base64UrlToBytes(text: string): Uint8Array {
  const padded = text.replaceAll("-", "+").replaceAll("_", "/");
  const padLength = (4 - (padded.length % 4)) % 4;
  const binary = atob(padded + "=".repeat(padLength));
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index);
  }
  return bytes;
}

async function hmacSha256(secret: string, message: string): Promise<Uint8Array> {
  const encoder = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const signature = await crypto.subtle.sign("HMAC", key, encoder.encode(message));
  return new Uint8Array(signature);
}

export async function signClaims(secret: string, claims: LicenseClaims): Promise<string> {
  const payload = canonicalClaimsPayload(claims);
  const signature = await hmacSha256(secret, payload);
  return TOKEN_PREFIX + bytesToBase64Url(encoderBytes(payload)) + "." + bytesToBase64Url(signature);
}

function encoderBytes(text: string): Uint8Array {
  return new TextEncoder().encode(text);
}

export async function verifyToken(
  secret: string,
  token: string,
): Promise<LicenseClaims | null> {
  const trimmed = token.trim();
  if (!trimmed.startsWith(TOKEN_PREFIX)) return null;
  const rest = trimmed.slice(TOKEN_PREFIX.length);
  const dot = rest.lastIndexOf(".");
  if (dot <= 0 || dot === rest.length - 1) return null;
  const payloadBytes = base64UrlToBytes(rest.slice(0, dot));
  const givenSignature = base64UrlToBytes(rest.slice(dot + 1));
  const payload = new TextDecoder().decode(payloadBytes);
  const expected = await hmacSha256(secret, payload);
  if (expected.length !== givenSignature.length) return null;
  let different = 0;
  for (let index = 0; index < expected.length; index += 1) {
    different |= expected[index] ^ givenSignature[index];
  }
  if (different !== 0) return null;
  try {
    const parsed = JSON.parse(payload) as LicenseClaims;
    if (parsed.v !== 1) return null;
    if (parsed.kind !== "trial" && parsed.kind !== "paid") return null;
    if (!parsed.keyId || !parsed.expiresAt) return null;
    return {
      v: 1,
      kind: parsed.kind,
      email: parsed.email ?? "",
      deviceId: parsed.deviceId ?? "",
      keyId: parsed.keyId,
      issuedAt: parsed.issuedAt,
      expiresAt: parsed.expiresAt,
    };
  } catch {
    return null;
  }
}

export const KNOWN_VECTOR_CLAIMS: LicenseClaims = {
  v: 1,
  kind: "paid",
  email: "coach@example.com",
  deviceId: "",
  keyId: "k_test1",
  issuedAt: 1704067200,
  expiresAt: 1893456000,
};

export const KNOWN_VECTOR_TOKEN =
  "AVA1.eyJkZXZpY2VJZCI6IiIsImVtYWlsIjoiY29hY2hAZXhhbXBsZS5jb20iLCJleHBpcmVzQXQiOjE4OTM0NTYwMDAsImlzc3VlZEF0IjoxNzA0MDY3MjAwLCJrZXlJZCI6ImtfdGVzdDEiLCJraW5kIjoicGFpZCIsInYiOjF9.PLj0fvb--oSW5sAp3FnHUHMdRgraaepgsDWExc0sY8M";

export const DEFAULT_DEV_SECRET = "ava-dev-signing-secret-not-for-production";
