import {
  createContext, useCallback, useContext, useEffect, useId, useMemo, useRef, useState,
  type ButtonHTMLAttributes, type ReactNode,
} from 'react'
import { createPortal } from 'react-dom'
import { AnimatePresence, motion } from 'framer-motion'
import { AlertTriangle, Check, Info, Loader2, X } from 'lucide-react'
import clsx from 'clsx'

export const cn = clsx

/* ─── Button ──────────────────────────────────────────────────────────────── */

type Variant = 'primary' | 'outline' | 'ghost' | 'danger'
type Size = 'sm' | 'md'

export function Button({
  variant = 'primary', size = 'md', loading = false, icon, className, children, disabled, ...rest
}: ButtonHTMLAttributes<HTMLButtonElement> & {
  variant?: Variant
  size?: Size
  loading?: boolean
  icon?: ReactNode
}) {
  const base = variant === 'primary' ? 'btn-primary' : variant === 'outline' ? 'btn-outline' : variant === 'danger' ? 'btn-danger' : 'btn-ghost'
  return (
    <button
      {...rest}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={cn(base, size === 'sm' && 'btn-sm', className)}
    >
      {loading ? <Loader2 size={14} className="animate-spin" /> : icon}
      {children}
    </button>
  )
}

export function IconButton({
  label, className, children, ...rest
}: ButtonHTMLAttributes<HTMLButtonElement> & { label: string }) {
  return (
    <button {...rest} aria-label={label} title={label} className={cn('btn-icon', className)}>
      {children}
    </button>
  )
}

/* ─── Switch ──────────────────────────────────────────────────────────────── */

export function Switch({
  checked, onChange, disabled, label,
}: { checked: boolean; onChange: (v: boolean) => void; disabled?: boolean; label?: string }) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      disabled={disabled}
      onClick={() => onChange(!checked)}
      className={cn(
        'relative inline-flex h-[22px] w-[38px] shrink-0 items-center rounded-full border transition-colors duration-200',
        checked ? 'border-accent-300/60 bg-accent-300/25' : 'border-border bg-field',
        disabled && 'opacity-40 pointer-events-none',
      )}
    >
      <span
        className={cn(
          'inline-block h-[16px] w-[16px] rounded-full transition-transform duration-200',
          checked ? 'translate-x-[19px] bg-accent-200' : 'translate-x-[3px] bg-[#6b6659]',
        )}
        style={{ transitionTimingFunction: 'cubic-bezier(0.34, 1.4, 0.64, 1)' }}
      />
    </button>
  )
}

/* ─── Segmented control ───────────────────────────────────────────────────── */

export function Segmented<T extends string>({
  value, options, onChange, className,
}: {
  value: T
  options: { value: T; label: ReactNode }[]
  onChange: (v: T) => void
  className?: string
}) {
  const index = Math.max(0, options.findIndex(o => o.value === value))
  return (
    <div
      role="tablist"
      className={cn('relative inline-flex items-center rounded-md border border-border bg-field p-[3px]', className)}
    >
      <span
        aria-hidden
        className="absolute top-[3px] bottom-[3px] rounded-[7px] bg-accent-300/10 ring-1 ring-inset ring-accent-300/25 transition-transform duration-200"
        style={{
          width: `calc((100% - 6px) / ${options.length})`,
          transform: `translateX(${index * 100}%)`,
          transitionTimingFunction: 'var(--ease-out)',
        }}
      />
      {options.map(o => (
        <button
          key={o.value}
          role="tab"
          aria-selected={o.value === value}
          onClick={() => onChange(o.value)}
          className={cn(
            'relative z-10 px-3 h-7 min-w-[64px] text-[12px] rounded-[7px] transition-colors duration-150',
            o.value === value ? 'text-accent-200' : 'text-muted hover:text-body',
          )}
        >
          {o.label}
        </button>
      ))}
    </div>
  )
}

/* ─── Slider ──────────────────────────────────────────────────────────────── */

export function Slider({
  value, min, max, step = 1, onChange, format, label,
}: {
  value: number
  min: number
  max: number
  step?: number
  onChange: (v: number) => void
  format?: (v: number) => string
  label?: string
}) {
  const pct = max === min ? 0 : ((value - min) / (max - min)) * 100
  return (
    <div>
      <div className="flex items-baseline justify-between mb-1.5">
        {label && <span className="text-[11px] text-muted">{label}</span>}
        <span className="text-[11px] font-mono text-accent-200 tabular-nums">{format ? format(value) : value}</span>
      </div>
      <input
        type="range"
        aria-label={label}
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={e => onChange(Number(e.target.value))}
        className="range w-full"
        style={{ ['--fill' as any]: `${pct}%` }}
      />
    </div>
  )
}

/* ─── Field ───────────────────────────────────────────────────────────────── */

export function Field({
  label, hint, aside, children, className,
}: { label: string; hint?: ReactNode; aside?: ReactNode; children: ReactNode; className?: string }) {
  return (
    <div className={className}>
      <div className="flex items-center justify-between">
        <label className="label">{label}</label>
        {aside}
      </div>
      {children}
      {hint && <p className="hint mt-1.5">{hint}</p>}
    </div>
  )
}

/* ─── Meter ───────────────────────────────────────────────────────────────── */

export function Meter({ value, max = 10, tone = 'gold' }: { value: number; max?: number; tone?: 'gold' | 'aurora' | 'ember' }) {
  const pct = Math.max(2, Math.min(100, (value / max) * 100))
  const color = tone === 'aurora' ? 'bg-aurora' : tone === 'ember' ? 'bg-ember' : 'bg-accent-300'
  return (
    <span className="block h-[3px] w-full overflow-hidden rounded-full bg-track">
      <span className={cn('block h-full rounded-full transition-[width] duration-500', color)} style={{ width: `${pct}%` }} />
    </span>
  )
}

/* ─── Modal ───────────────────────────────────────────────────────────────── */

export function Modal({
  open, onClose, title, description, children, footer, width = 'max-w-lg',
}: {
  open: boolean
  onClose: () => void
  title: string
  description?: string
  children: ReactNode
  footer?: ReactNode
  width?: string
}) {
  const panelRef = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!open) return
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose() }
    document.addEventListener('keydown', onKey)
    const prev = document.activeElement as HTMLElement | null
    panelRef.current?.focus()
    return () => { document.removeEventListener('keydown', onKey); prev?.focus?.() }
  }, [open, onClose])

  return createPortal(
    <AnimatePresence>
      {open && (
        <div className="fixed inset-0 z-50 flex items-center justify-center p-6" role="dialog" aria-modal="true" aria-label={title}>
          <motion.div
            className="absolute inset-0 bg-scrim backdrop-blur-[6px]"
            initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.18 }}
            onClick={onClose}
          />
          <motion.div
            ref={panelRef}
            tabIndex={-1}
            initial={{ opacity: 0, scale: 0.97, y: 8 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.98, y: 4 }}
            transition={{ duration: 0.22, ease: [0.16, 1, 0.3, 1] }}
            className={cn('relative w-full overflow-hidden rounded-lg border border-border bg-raised shadow-modal outline-none', width)}
          >
            <div className="flex items-start justify-between gap-6 border-b border-border px-5 py-4">
              <div>
                <h2 className="display text-[17px]">{title}</h2>
                {description && <p className="hint mt-1">{description}</p>}
              </div>
              <IconButton label="关闭" onClick={onClose} aria-label="关闭"><X size={16} /></IconButton>
            </div>
            <div className="max-h-[65vh] overflow-y-auto px-5 py-4">{children}</div>
            {footer && <div className="flex items-center justify-end gap-2 border-t border-border bg-field/60 px-5 py-3">{footer}</div>}
          </motion.div>
        </div>
      )}
    </AnimatePresence>,
    document.body,
  )
}

/* ─── Empty state ─────────────────────────────────────────────────────────── */

export function EmptyState({
  icon, title, description, action,
}: { icon: ReactNode; title: string; description?: string; action?: ReactNode }) {
  return (
    <div className="flex flex-col items-center justify-center rounded-lg border border-dashed border-border/80 px-8 py-14 text-center">
      <div className="mb-4 grid h-12 w-12 place-items-center rounded-full border border-accent-300/20 bg-accent-300/[0.06] text-accent-300">
        {icon}
      </div>
      <p className="display text-[15px]">{title}</p>
      {description && <p className="mt-1.5 max-w-sm text-[12px] leading-relaxed text-muted">{description}</p>}
      {action && <div className="mt-5">{action}</div>}
    </div>
  )
}

/* ─── Page header ─────────────────────────────────────────────────────────── */

export function PageHeader({
  title, subtitle, actions, aside,
}: { title: string; subtitle?: ReactNode; actions?: ReactNode; aside?: ReactNode }) {
  return (
    <header className="mb-7 flex items-end justify-between gap-6">
      <div className="min-w-0">
        <div className="flex items-center gap-3"><h1 className="display text-[26px] leading-tight">{title}</h1>{aside}</div>
        {subtitle && <p className="mt-1.5 max-w-2xl text-[12.5px] leading-relaxed text-muted">{subtitle}</p>}
      </div>
      {actions && <div className="flex shrink-0 items-center gap-2">{actions}</div>}
    </header>
  )
}

/* ─── Toasts ──────────────────────────────────────────────────────────────── */

type Toast = { id: string; message: string; tone: 'ok' | 'error' | 'info' }
type ToastApi = { push: (message: string, tone?: Toast['tone']) => void }
const ToastCtx = createContext<ToastApi>({ push: () => {} })
export const useToast = () => useContext(ToastCtx)

export function ToastProvider({ children }: { children: ReactNode }) {
  const [items, setItems] = useState<Toast[]>([])
  const push = useCallback((message: string, tone: Toast['tone'] = 'ok') => {
    const id = Math.random().toString(36).slice(2)
    setItems(prev => [...prev.slice(-2), { id, message, tone }])
    setTimeout(() => setItems(prev => prev.filter(t => t.id !== id)), 3200)
  }, [])
  const api = useMemo(() => ({ push }), [push])
  return (
    <ToastCtx.Provider value={api}>
      {children}
      <div aria-live="polite" aria-atomic="false" className="pointer-events-none fixed bottom-5 right-5 z-[60] flex w-[320px] flex-col gap-2">
        <AnimatePresence initial={false}>
          {items.map(t => (
            <motion.div
              key={t.id}
              layout
              initial={{ opacity: 0, y: 10, scale: 0.98 }}
              animate={{ opacity: 1, y: 0, scale: 1 }}
              exit={{ opacity: 0, y: 4, scale: 0.98, transition: { duration: 0.15 } }}
              transition={{ duration: 0.2, ease: [0.16, 1, 0.3, 1] }}
              className="pointer-events-auto flex items-start gap-2.5 rounded-md border border-border bg-elevated/95 px-3.5 py-2.5 shadow-modal backdrop-blur"
            >
              <span className={cn('mt-0.5', t.tone === 'error' ? 'text-ember' : t.tone === 'info' ? 'text-aurora' : 'text-accent-300')}>
                {t.tone === 'error' ? <AlertTriangle size={14} /> : t.tone === 'info' ? <Info size={14} /> : <Check size={14} />}
              </span>
              <p className="text-[12px] leading-relaxed text-body">{t.message}</p>
            </motion.div>
          ))}
        </AnimatePresence>
      </div>
    </ToastCtx.Provider>
  )
}

/* ─── Misc ────────────────────────────────────────────────────────────────── */

export function Spinner({ size = 14 }: { size?: number }) {
  return <Loader2 size={size} className="animate-spin" />
}

export function useMediaQuery(query: string) {
  const [match, setMatch] = useState(false)
  useEffect(() => {
    const mq = window.matchMedia(query)
    const update = () => setMatch(mq.matches)
    update()
    mq.addEventListener('change', update)
    return () => mq.removeEventListener('change', update)
  }, [query])
  return match
}
