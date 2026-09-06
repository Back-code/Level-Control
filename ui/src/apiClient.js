import { adminFetch } from './adminAuth.js';

export async function apiRequest(url, options = {}) {
  const response = options.admin === false
    ? await fetch(url, options)
    : await adminFetch(url, options);

  if (!response.ok) {
    let payload = null;
    try {
      payload = await response.clone().json();
    } catch (_) {
      // Keep the HTTP status as the fallback error context.
    }
    const error = new Error(payload?.error || `HTTP ${response.status}`);
    error.status = response.status;
    error.payload = payload;
    throw error;
  }

  return response;
}

export async function apiJson(url, options = {}) {
  const response = await apiRequest(url, options);
  return response.json();
}
