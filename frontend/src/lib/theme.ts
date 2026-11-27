import { useEffect, useState } from 'react'
import { api } from '../bridge'

export type ThemeMode = 'dark' | 'light' | 'system'
export type ResolvedTheme = 'dark' | 'light'

const KEY = 'oly-theme'

export function resolveTheme(mode: ThemeMode): ResolvedTheme {
  if (mode === 'system') {
    return window.matchMedia?.('(prefers-color-scheme: light)').matches ? 'light' : 'dark'
  }
  return mode
}

export function applyTheme(mode: ThemeMode) {
  const resolved = resolveTheme(mode)
  document.documentElement.dataset.theme = resolved
  document.documentElement.style.colorScheme = resolved
  try { localStorage.setItem(KEY, mode) } catch { /* ignore */ }
  window.dispatchEvent(new CustomEvent('oly-theme', { detail: mode }))
  return resolved
}

export function initTheme() {
  let mode: ThemeMode = 'dark'
  try { mode = (localStorage.getItem(KEY) as ThemeMode) || 'dark' } catch { /* ignore */ }
  applyTheme(mode)
  const mq = window.matchMedia('(prefers-color-scheme: light)')
  const onChange = () => {
    const cur = (localStorage.getItem(KEY) as ThemeMode) || 'dark'
    if (cur === 'system') applyTheme('system')
  }
  mq.addEventListener?.('change', onChange)
}

export function useThemeMode() {
  const [mode, setMode] = useState<ThemeMode>(() => {
    try { return ((localStorage.getItem(KEY) as ThemeMode) || 'dark') } catch { return 'dark' }
  })
  useEffect(() => {
    const h = (e: Event) => setMode((e as CustomEvent).detail as ThemeMode)
    window.addEventListener('oly-theme', h)
    return () => window.removeEventListener('oly-theme', h)
  }, [])
 	return mode
}

/** Toggle dark/light with a circular reveal from the clicked point when supported. */
export function toggleThemeWithReveal(next: ThemeMode, x?: number, y?: number) {
  const doc = document as Document & {
    startViewTransition?: (cb: () => void) => { finished: Promise<void> }
  }
  if (!doc.startViewTransition || document.documentElement.dataset.reduceMotion === 'true') {
    applyTheme(next)
    api.getSettings().then(s => api.saveSettings({ ...s, theme: next })).catch(() => {})
    return
  }
  document.documentElement.style.setProperty('--tx', `${x ?? window.innerWidth - 80}px`)
  document.documentElement.style.setProperty('--ty', `${y ?? 36}px`)
  doc.startViewTransition(() => { applyTheme(next) })
  api.getSettings().then(s => api.saveSettings({ ...s, theme: next })).catch(() => {})
}
