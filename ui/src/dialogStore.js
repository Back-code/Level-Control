import { writable } from 'svelte/store';

export const activeNotice = writable(null);
export const activeConfirmation = writable(null);

let noticeTimer;

function clearNoticeTimer() {
  if (!noticeTimer) {
    return;
  }

  clearTimeout(noticeTimer);
  noticeTimer = undefined;
}

export function showNotice(type, message, options = {}) {
  clearNoticeTimer();

  const duration = options.duration ?? (type === 'success' ? 4200 : 0);
  activeNotice.set({
    type,
    title: options.title || (type === 'error' ? 'Fehler' : 'Erfolg'),
    message,
    duration
  });

  if (duration > 0) {
    noticeTimer = setTimeout(() => {
      activeNotice.set(null);
      noticeTimer = undefined;
    }, duration);
  }
}

export function closeNotice() {
  clearNoticeTimer();
  activeNotice.set(null);
}

export function confirmAction(options) {
  return new Promise((resolve) => {
    activeConfirmation.set({
      title: options.title || 'Bestätigung',
      message: options.message || '',
      confirmLabel: options.confirmLabel || 'Bestätigen',
      cancelLabel: options.cancelLabel || 'Abbrechen',
      tone: options.tone || 'default',
      resolve
    });
  });
}

export function resolveConfirmation(result) {
  let currentDialog;
  activeConfirmation.update((dialog) => {
    currentDialog = dialog;
    return null;
  });

  if (currentDialog?.resolve) {
    currentDialog.resolve(result);
  }
}