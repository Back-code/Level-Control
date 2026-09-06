<script>
  import { activeAdminLogin, adminSessionActive, changeAdminPassword, logoutAdmin } from './adminAuth.js';

  let password = '';
  let confirmation = '';
  let error = '';
  let changeOpen = false;
  let newPassword = '';
  let newConfirmation = '';

  function close() {
    const request = $activeAdminLogin;
    const submittedPassword = password;
    password = '';
    confirmation = '';
    error = '';
    activeAdminLogin.set(null);
    request?.reject(new Error('Admin-Anmeldung abgebrochen'));
  }

  function submit() {
    const request = $activeAdminLogin;
    if (!request) return;
    if (password.length < 8) {
      error = 'Das Passwort muss mindestens 8 Zeichen enthalten.';
      return;
    }
    if (!request.configured && password !== confirmation) {
      error = 'Die Passwörter stimmen nicht überein.';
      return;
    }
    password = '';
    confirmation = '';
    error = '';
    activeAdminLogin.set(null);
    request.resolve({ password: submittedPassword });
  }

  async function submitChange() {
    error = '';
    if (newPassword.length < 8 || newPassword !== newConfirmation) {
      error = 'Passwörter müssen übereinstimmen und mindestens 8 Zeichen enthalten.';
      return;
    }
    try {
      await changeAdminPassword(newPassword);
      newPassword = '';
      newConfirmation = '';
      changeOpen = false;
    } catch (changeError) {
      error = changeError?.message || 'Passwort konnte nicht geändert werden';
    }
  }
</script>

{#if $adminSessionActive && !$activeAdminLogin}
  <div class="admin-session-tools">
    <button type="button" on:click={() => (changeOpen = true)}>Admin-Passwort ändern</button>
    <button type="button" on:click={logoutAdmin}>Abmelden</button>
  </div>
{/if}

{#if $activeAdminLogin}
  <div class="admin-login-backdrop" role="presentation">
    <form class="admin-login" on:submit|preventDefault={submit}>
      <h2>{$activeAdminLogin.configured ? 'Admin-Anmeldung' : 'Admin-Passwort festlegen'}</h2>
      <p>{$activeAdminLogin.configured ? 'Diese Aktion benötigt eine Admin-Anmeldung.' : 'Lege jetzt das Passwort für geschützte Geräteaktionen fest.'}</p>
      <label>Passwort<input type="password" bind:value={password} autocomplete={ $activeAdminLogin.configured ? 'current-password' : 'new-password' } /></label>
      {#if !$activeAdminLogin.configured}
        <label>Passwort wiederholen<input type="password" bind:value={confirmation} autocomplete="new-password" /></label>
      {/if}
      {#if error}<p class="admin-login-error" role="alert">{error}</p>{/if}
      <div class="admin-login-actions">
        <button type="button" on:click={close}>Abbrechen</button>
        <button class="primary" type="submit">Fortfahren</button>
      </div>
    </form>
  </div>
{/if}

{#if changeOpen}
  <div class="admin-login-backdrop" role="presentation">
    <form class="admin-login" on:submit|preventDefault={submitChange}>
      <h2>Admin-Passwort ändern</h2>
      <label>Neues Passwort<input type="password" bind:value={newPassword} autocomplete="new-password" /></label>
      <label>Passwort wiederholen<input type="password" bind:value={newConfirmation} autocomplete="new-password" /></label>
      {#if error}<p class="admin-login-error" role="alert">{error}</p>{/if}
      <div class="admin-login-actions">
        <button type="button" on:click={() => (changeOpen = false)}>Abbrechen</button>
        <button class="primary" type="submit">Speichern</button>
      </div>
    </form>
  </div>
{/if}

<style>
  .admin-login-backdrop { position: fixed; inset: 0; z-index: 100; display: grid; place-items: center; padding: 20px; background: rgba(3, 8, 18, 0.7); backdrop-filter: blur(6px); }
  .admin-login { width: min(420px, 100%); display: grid; gap: 14px; padding: 24px; border: 1px solid var(--surface-border); border-radius: 16px; background: var(--card-grad); color: var(--text-main); box-shadow: var(--shadow); }
  .admin-login h2, .admin-login p { margin: 0; }
  .admin-login label { display: grid; gap: 6px; color: var(--text-muted); font-weight: 700; }
  .admin-login input { min-height: 40px; padding: 0 10px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--surface-2); color: var(--text-main); }
  .admin-login-error { color: #fca5a5; }
  .admin-login-actions { display: flex; justify-content: flex-end; gap: 8px; }
  .admin-login-actions button { min-height: 40px; padding: 0 14px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--button-bg); color: var(--button-text); cursor: pointer; }
  .admin-login-actions .primary { background: var(--button-active-bg); color: var(--button-active-text); }
  .admin-session-tools { position: fixed; right: 16px; bottom: 16px; z-index: 45; display: flex; gap: 6px; }
  .admin-session-tools button { min-height: 32px; padding: 0 10px; border: 1px solid var(--surface-border); border-radius: 8px; background: var(--surface-2); color: var(--text-muted); cursor: pointer; }
</style>