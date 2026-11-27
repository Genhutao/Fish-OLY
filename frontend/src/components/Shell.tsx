import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import { NavLink, useLocation, useNavigate } from 'react-router-dom'
import { AnimatePresence, motion, useReducedMotion } from 'framer-motion'
import {
  BookOpen, Cat, Command, Compass, Library, MessageCircle, Moon, Plus, Search,
  Settings, Sparkles, Sun, Users, UsersRound, type LucideIcon,
} from 'lucide-react'
import { api } from '../bridge'
import { applyTheme, toggleThemeWithReveal, resolveTheme, useThemeMode } from '../lib/theme'
import { cn } from './ui'
import { ErrorBoundary } from './ErrorBoundary'

type NavItem = { to: string; label: string; icon: LucideIcon; group: number; keywords: string }

const NAV: NavItem[] = [
  { to: '/chat', label: '消息', icon: MessageCircle, group: 0, keywords: 'chat message conversation qq im' },
  { to: '/personas', label: '角色', icon: Users, group: 0, keywords: 'persona character oc' },
  { to: '/roam', label: '漫游', icon: Compass, group: 1, keywords: 'roam roaming scene group' },
  { to: '/community', label: '动态', icon: UsersRound, group: 1, keywords: 'community post feed moments' },
  { to: '/library', label: '技能库', icon: Library, group: 1, keywords: 'skill preset library prompt github' },
  { to: '/settings', label: '设置', icon: Settings, group: 2, keywords: 'settings config api model theme' },
]

const TITLES: Record<string, string> = {
  '/chat': '消息',
  '/personas': '人设',
  '/story': '剧情演绎',
  '/library': '技能库',
  '/community': 'OC 社区',
  '/roam': 'OC 漫游',
  '/pet': '卓宠',
  '/settings': '设置',
}

function useClock() {
  const [now, setNow] = useState(new Date())
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 30_000)
    return () => clearInterval(t)
  }, [])
  return now
}

/* ─── Icon rail ───────────────────────────────────────────────────────────── */

function Rail() {
  const loc = useLocation()
  const groups = useMemo(() => [0, 1, 2].map(g => NAV.filter(n => n.group === g)), [])
  return (
    <aside className="relative z-20 flex w-[76px] shrink-0 flex-col items-center border-r border-border/80 bg-panel/80 py-4">
      <NavLink to="/chat" aria-label="OLY Persona 首页" className="group mb-5 grid h-10 w-10 place-items-center rounded-full border border-accent-300/25 bg-accent-300/[0.06] shadow-glow">
        <Moon size={17} className="text-accent-200 transition-transform duration-300 group-hover:-rotate-12" />
      </NavLink>

      <nav className="flex flex-1 flex-col items-center gap-1" aria-label="主导航">
        {groups.map((items, gi) => (
          <div key={gi} className="flex flex-col items-center gap-1">
            {gi > 0 && <span className="my-2 h-px w-6 bg-border" />}
            {items.map(({ to, label, icon: Icon }) => {
              const active = loc.pathname === to || loc.pathname.startsWith(to + '/')
              return (
                <NavLink
                  key={to}
                  to={to}
                  title={label}
                  className={cn(
                    'group relative flex h-[52px] w-[60px] flex-col items-center justify-center gap-1 rounded-md transition-colors duration-150',
                    active ? 'text-accent-200' : 'text-muted hover:text-body hover:bg-veil/[0.04]',
                  )}
                >
                  {active && (
                    <motion.span
                      layoutId="rail-active"
                      className="absolute left-[-8px] top-1/2 h-6 w-[2px] -translate-y-1/2 rounded-full bg-accent-300"
                      transition={{ duration: 0.25, ease: [0.16, 1, 0.3, 1] }}
                    />
                  )}
                  <Icon size={18} strokeWidth={active ? 2 : 1.7} />
                  <span className="text-[9.5px] leading-none tracking-[0.04em]">{label}</span>
                </NavLink>
              )
            })}
          </div>
        ))}
      </nav>

      <div className="flex flex-col items-center gap-2 pt-2">
        <span className="h-1.5 w-1.5 rounded-full bg-success/80" title="本地运行中" />
        <span className="text-[8.5px] tracking-[0.1em] text-muted/60">0.3</span>
      </div>
    </aside>
  )
}

/* ─── Top bar ─────────────────────────────────────────────────────────────── */

function TopBar({ onCommand }: { onCommand: () => void }) {
  const loc = useLocation()
  const now = useClock()
  const mode = useThemeMode()
  const resolved = resolveTheme(mode)
  const [hasKey, setHasKey] = useState(false)
  useEffect(() => {
    const refresh = () => api.getSettings().then(s => setHasKey(!!s.apiKey)).catch(() => {})
    refresh()
    window.addEventListener('oly-settings', refresh)
    return () => window.removeEventListener('oly-settings', refresh)
  }, [])
  const section = Object.entries(TITLES).find(([path]) => loc.pathname.startsWith(path))?.[1] ?? 'OLY'

  return (
    <div className="relative z-10 flex h-11 shrink-0 items-center justify-between border-b border-border/60 px-7">
      <div className="flex items-center gap-2 text-[11px] text-muted/80">
        <span className="tracking-[0.12em] text-muted/60">OLY PERSONA</span>
        <span className="text-border">/</span>
        <span className="text-body/80">{section}</span>
      </div>
      <div className="flex items-center gap-3">
        <span className={cn('flex items-center gap-1.5 text-[10.5px]', hasKey ? 'text-aurora' : 'text-muted/70')}>
          <span className={cn('h-1.5 w-1.5 rounded-full', hasKey ? 'bg-aurora' : 'bg-warning/70')} />
          {hasKey ? '已接入模型' : 'mock 模式'}
        </span>
        <span className="hidden font-mono text-[10.5px] tabular-nums text-muted/60 sm:inline">
          {now.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })}
        </span>
        <button
          onClick={e => toggleThemeWithReveal(resolved === 'dark' ? 'light' : 'dark', e.clientX, e.clientY)}
          className="btn-icon h-7 w-7 rounded-md border border-border"
          aria-label={resolved === 'dark' ? '切换到白昼模式' : '切换到黑夜模式'}
          title={resolved === 'dark' ? '切换到白昼模式' : '切换到黑夜模式'}
        >
          <motion.span key={resolved} initial={{ rotate: -30, opacity: 0 }} animate={{ rotate: 0, opacity: 1 }} transition={{ duration: 0.25 }}>
            {resolved === 'dark' ? <Sun size={13} /> : <Moon size={13} />}
          </motion.span>
        </button>
        <button
          onClick={onCommand}
          className="flex h-7 items-center gap-2 rounded-md border border-border bg-veil/[0.03] px-2.5 text-[10.5px] text-muted transition-colors hover:border-accent-300/30 hover:text-body"
        >
          <Command size={11} /> 命令
          <span className="kbd ml-0.5">⌘K</span>
        </button>
      </div>
    </div>
  )
}

/* ─── Command palette ─────────────────────────────────────────────────────── */

type Command = { id: string; label: string; hint: string; icon: LucideIcon; keywords: string; run: () => void }

function CommandPalette({ open, onClose }: { open: boolean; onClose: () => void }) {
  const nav = useNavigate()
  const [query, setQuery] = useState('')
  const [cursor, setCursor] = useState(0)
  const inputRef = useRef<HTMLInputElement>(null)

  const commands: Command[] = useMemo(() => [
    ...NAV.map(n => ({
      id: 'nav-' + n.to, label: `前往${n.label}`, hint: '导航', icon: n.icon, keywords: n.keywords,
      run: () => nav(n.to),
    })),
    { id: 'new-persona', label: '新建人设', hint: '动作', icon: Plus, keywords: 'new persona create', run: () => nav('/personas/new') },
    { id: 'story', label: '前往剧情演绎', hint: '导航', icon: BookOpen, keywords: 'story script', run: () => nav('/story') },
    { id: 'pet', label: '前往卓宠', hint: '导航', icon: Cat, keywords: 'pet desktop', run: () => nav('/pet') },
    { id: 'roam', label: '查看漫游动态', hint: '动作', icon: Compass, keywords: 'roam feed', run: () => nav('/roam') },
    { id: 'library', label: '装配预设与技能', hint: '动作', icon: Sparkles, keywords: 'preset skill equip', run: () => nav('/library') },
  ], [nav])

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase()
    if (!q) return commands
    return commands.filter(c => (c.label + c.hint + c.keywords).toLowerCase().includes(q))
  }, [commands, query])

  useEffect(() => {
    if (!open) { setQuery(''); setCursor(0); return }
    const t = setTimeout(() => inputRef.current?.focus(), 40)
    return () => clearTimeout(t)
  }, [open])

  useEffect(() => { setCursor(0) }, [query])

  const run = (c?: Command) => { if (!c) return; onClose(); c.run() }

  return (
    <AnimatePresence>
      {open && (
        <div className="fixed inset-0 z-[55] flex justify-center px-6 pt-[14vh]" role="dialog" aria-modal="true" aria-label="命令面板">
          <motion.div
            className="absolute inset-0 bg-scrim backdrop-blur-[5px]"
            initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.16 }}
            onClick={onClose}
          />
          <motion.div
            initial={{ opacity: 0, y: -8, scale: 0.985 }}
            animate={{ opacity: 1, y: 0, scale: 1 }}
            exit={{ opacity: 0, y: -4, scale: 0.99 }}
            transition={{ duration: 0.2, ease: [0.16, 1, 0.3, 1] }}
            className="relative h-fit w-full max-w-[560px] overflow-hidden rounded-lg border border-border bg-raised/95 shadow-modal backdrop-blur-xl"
          >
            <div className="flex items-center gap-2.5 border-b border-border px-4">
              <Search size={15} className="text-muted" />
              <input
                ref={inputRef}
                value={query}
                onChange={e => setQuery(e.target.value)}
                onKeyDown={e => {
                  if (e.key === 'ArrowDown') { e.preventDefault(); setCursor(c => Math.min(filtered.length - 1, c + 1)) }
                  if (e.key === 'ArrowUp') { e.preventDefault(); setCursor(c => Math.max(0, c - 1)) }
                  if (e.key === 'Enter') { e.preventDefault(); run(filtered[cursor]) }
                  if (e.key === 'Escape') onClose()
                }}
                placeholder="搜索页面或动作…"
                className="h-12 flex-1 bg-transparent text-[13px] text-bright outline-none placeholder:text-muted/60"
              />
              <span className="kbd">esc</span>
            </div>
            <div className="max-h-[320px] overflow-y-auto p-2">
              {filtered.length === 0 && <p className="px-3 py-6 text-center text-[12px] text-muted">没有匹配的命令</p>}
              {filtered.map((c, i) => {
                const Icon = c.icon
                return (
                  <button
                    key={c.id}
                    onMouseEnter={() => setCursor(i)}
                    onClick={() => run(c)}
                    className={cn(
                      'flex w-full items-center gap-3 rounded-md px-3 py-2.5 text-left transition-colors',
                      i === cursor ? 'bg-accent-300/10 text-bright' : 'text-body/80 hover:bg-veil/[0.04]',
                    )}
                  >
                    <Icon size={15} className={i === cursor ? 'text-accent-200' : 'text-muted'} />
                    <span className="flex-1 text-[12.5px]">{c.label}</span>
                    <span className="text-[10px] text-muted/60">{c.hint}</span>
                  </button>
                )
              })}
            </div>
          </motion.div>
        </div>
      )}
    </AnimatePresence>
  )
}

/* ─── Shell ───────────────────────────────────────────────────────────────── */

export function AppShell({ children }: { children: ReactNode }) {
  const [paletteOpen, setPaletteOpen] = useState(false)
  const reduced = useReducedMotion()
  const loc = useLocation()

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault()
        setPaletteOpen(v => !v)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  useEffect(() => {
    api.getSettings().then(s => {
      document.documentElement.dataset.density = s.uiDensity || 'comfortable'
      document.documentElement.dataset.reduceMotion = String(!!s.reduceMotion)
      if (s.theme) applyTheme(s.theme)
    }).catch(() => {})
  }, [])

  return (
    <div className="relative flex h-screen overflow-hidden">
      <Rail />
      <div className="flex min-w-0 flex-1 flex-col">
        <TopBar onCommand={() => setPaletteOpen(true)} />
        {/* Remount per route: a fresh scroll container guarantees the new page
            starts at the top and can never inherit a stale scroll offset. */}
        <main key={loc.pathname} className="relative flex-1 overflow-y-auto">
          <motion.div
            initial={reduced ? false : { opacity: 0, y: 6 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.22, ease: [0.16, 1, 0.3, 1] }}
            className="min-h-full"
          >
            <ErrorBoundary>{children}</ErrorBoundary>
          </motion.div>
        </main>
      </div>
      <CommandPalette open={paletteOpen} onClose={() => setPaletteOpen(false)} />
    </div>
  )
}
