const ADMIN_TOKEN_KEY = 'level-control-admin-token';

function getStoredToken() {
  return localStorage.getItem(ADMIN_TOKEN_KEY) || '';
}

function storeToken(token) {
  if (token) {
    localStorage.setItem(ADMIN_TOKEN_KEY, token);
  } else {
    localStorage.removeItem(ADMIN_TOKEN_KEY);
  }
}

async function getAuthStatus() {
  const response = await fetch('/api/auth/status', { cache: 'no-store' });
  if (!response.ok) {
    throw new Error('Authentifizierungsstatus konnte nicht geladen werden');
  }
  return response.json();
}

async function authenticate() {
  const status = await getAuthStatus();
  const storedToken = getStoredToken();
  if (storedToken && status.authenticated) {
    return storedToken;
  }

  const password = window.prompt(status.configured
    ? 'Admin-Passwort eingeben'
    : 'Admin-Passwort für dieses Gerät festlegen (mindestens 8 Zeichen)');
  if (!password) {
    throw new Error('Admin-Anmeldung abgebrochen');
  }

  let passwordToConfirm = password;
  if (!status.configured) {
    passwordToConfirm = window.prompt('Admin-Passwort wiederholen');
    if (passwordToConfirm !== password) {
      throw new Error('Die Admin-Passwörter stimmen nicht überein');
    }
  }

  const endpoint = status.configured ? '/api/auth/login' : '/api/auth/setup';
  const response = await fetch(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password })
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok || !payload.token) {
    throw new Error(payload.error || 'Admin-Anmeldung fehlgeschlagen');
  }

  storeToken(payload.token);
  return payload.token;
}

export async function adminFetch(url, options = {}) {
  const token = await authenticate();
  const headers = new Headers(options.headers || {});
  headers.set('X-Admin-Token', token);
  const response = await fetch(url, { ...options, headers });
  if (response.status === 401) {
    storeToken('');
    throw new Error('Admin-Sitzung abgelaufen. Bitte erneut anmelden.');
  }
  return response;
}

export function clearAdminSession() {
  storeToken('');
}
