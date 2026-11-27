import { useMemo, useState } from 'react'
import { motion } from 'framer-motion'
import { Bell, Pin, Search, Settings2, Sparkles, UserPlus } from 'lucide-react'
import { cn } from '../ui'
import { previewOf, useNow, type Convo } from './parts'

type Tab = 'all' | 'direct' | 'group' | 'script'

export function ChatList({
  convos, activeId, onSelect, onOpenSettings,
}: {
  convos: Convo[]
  activeId: string
  onSelect: (id: string) => void
  onOpenSettings: () => void
}) {
  const [tab, setTab] = useState<Tab>('all')
  const [query, setQuery] = useState('')
  const now = useNow(20000)

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase()
    return convos
      .filter(c => tab === 'all' || (tab === 'direct' && c.type === 'persona') || (tab === 'group' && c.type === 'group') || (tab === 'script' && c.type === 'script'))
      .filter(c => !q || (c.name + c.subtitle + c.lastText).toLowerCase().includes(q))
      .sort((a, b) => +new Date(b.lastAt) - +new Date(a.lastAt))
  }, [convos, tab, query])

  const totalUnread = convos.reduce((n, c) => n + c.unread, 0)

  return (
    <aside className="flex w-[286px] shrink-0 flex-col border-r border-border/70 bg-panel/40">
      {/* Me */}
      <div className="flex items-center gap-3 px-4 pb-3 pt-4">
        <div className="relative">
          <div className="grid h-10 w-10 place-items-center rounded-full bg-gradient-to-br from-accent-200 to-accent-500 text-[15px] font-semibold text-[#241b0c]">
            你
          </div>
          <span className="absolute -bottom-0.5 -right-0.5 h-2.5 w-2.5 rounded-full border-2 border-surface bg-success" />
        </div>
        <div className="min-w-0 flex-1">
          <p className="truncate text-[13px] text-bright">你</p>
          <p className="text-[10px] text-muted">在线 · {totalUnread > 0 ? `${totalUnread} 条未读` : '一切安静'}</p>
        </div>
        <button className="btn-icon h-7 w-7" aria-label="通知"><Bell size={14} /></button>
        <button className="btn-icon h-7 w-7" aria-label="设置" onClick={onOpenSettings}><Settings2 size={14} /></button>
      </div>

      {/* Search */}
      <div className="px-3 pb-3">
        <div className="relative">
          <Search size={13} className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-muted" />
          <input
            value={query}
            onChange={e => setQuery(e.target.value)}
            placeholder="搜索会话、角色或房间"
            className="input h-8 bg-veil/[0.03] pl-8 text-[11.5px]"
            aria-label="搜索会话"
          />
        </div>
      </div>

      {/* Tabs */}
      <div className="flex items-center gap-1 px-3 pb-2">
        {([['all', '全部'], ['direct', '单聊'], ['group', '群聊'], ['script', '剧本']] as const).map(([k, label]) => (
          <button
            key={k}
            onClick={() => setTab(k)}
            className={cn('relative rounded-full px-2.5 py-1 text-[11px] transition-colors',
              tab === k ? 'text-accent-200' : 'text-muted hover:text-body')}
          >
            {tab === k && (
              <motion.span layoutId="chatlist-tab" className="absolute inset-0 rounded-full bg-accent-300/10 ring-1 ring-inset ring-accent-300/20"
                transition={{ type: 'spring', stiffness: 420, damping: 34 }} />
            )}
            <span className="relative">{label}</span>
          </button>
        ))}
      </div>

      {/* Conversations */}
      <div className="-mr-1 flex-1 overflow-y-auto px-2 pb-2">
        {filtered.length === 0 && (
          <p className="px-3 py-8 text-center text-[11px] text-muted">没有匹配的会话</p>
        )}
        {filtered.map(c => {
          const active = c.id === activeId
          return (
            <motion.button
              layout
              key={c.id}
              onClick={() => onSelect(c.id)}
              className={cn(
                'group relative flex w-full items-center gap-2.5 rounded-lg px-2.5 py-2.5 text-left transition-colors',
                active ? 'bg-accent-300/[0.09]' : 'hover:bg-veil/[0.04]',
              )}
            >
              {active && (
                <motion.span layoutId="chat-active" className="absolute left-0 top-1/2 h-6 w-[2px] -translate-y-1/2 rounded-full bg-accent-300"
                  transition={{ type: 'spring', stiffness: 420, damping: 34 }} />
              )}
              <div className="relative shrink-0">
                <div className={cn('grid h-10 w-10 place-items-center rounded-full border text-lg',
                  active ? 'border-accent-300/35 bg-accent-300/[0.08]' : 'border-border bg-field')}>
                  {c.avatar}
                </div>
                {c.online && <span className="absolute -bottom-0.5 -right-0.5 h-2.5 w-2.5 rounded-full border-2 border-surface bg-success" />}
                {c.type !== 'persona' && (
                  <span className="absolute -right-1 -top-1 rounded-full border border-border bg-raised px-1 py-[1px] text-[8px] text-muted">
                    {c.type === 'script' ? '剧' : '群'}
                  </span>
                )}
              </div>
              <div className="min-w-0 flex-1">
                <div className="flex items-baseline gap-2">
                  <span className={cn('truncate text-[12.5px]', active ? 'text-bright' : 'text-body/90')}>{c.name}</span>
                  <span className="ml-auto shrink-0 font-mono text-[9.5px] text-muted/60">
                    {relativeTime(c.lastAt, now)}
                  </span>
                </div>
                <div className="mt-0.5 flex items-center gap-1.5">
                  <p className="truncate text-[10.5px] text-muted/70">{c.lastText || c.subtitle}</p>
                  {c.unread > 0 && (
                    <span className="ml-auto grid h-4 min-w-4 shrink-0 place-items-center rounded-full bg-accent-300 px-1 text-[9px] font-semibold text-[#241b0c]">
                      {c.unread > 99 ? '99+' : c.unread}
                    </span>
                  )}
                </div>
              </div>
            </motion.button>
          )
        })}
      </div>

      {/* Footer */}
      <div className="border-t border-border/60 px-4 py-3">
        <p className="flex items-center gap-1.5 text-[10px] text-muted/70">
          <Sparkles size={11} className="text-accent-300" /> AI 角色正在房间里自己聊天
        </p>
        <div className="mt-2 flex items-center gap-2">
          <button className="btn-ghost h-7 flex-1 justify-start px-2 text-[10.5px]" onClick={() => setTab('group')}>
            <UserPlus size={11} /> 查看全部群聊
          </button>
          <button className="btn-ghost h-7 px-2 text-[10.5px]" aria-label="置顶会话"><Pin size={11} /></button>
        </div>
      </div>
    </aside>
  )
}

function relativeTime(iso: string, now: number) {
  const diff = Math.max(0, now - +new Date(iso))
  const min = Math.floor(diff / 60000)
  if (min < 1) return '刚刚'
  if (min < 60) return `${min} 分钟`
  const d = new Date(iso)
  const sameDay = d.toDateString() === new Date().toDateString()
  if (sameDay) return d.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })
  return d.toLocaleDateString('zh-CN', { month: '2-digit', day: '2-digit' })
}

export type { Tab }
