import { NavLink, useLocation } from 'react-router-dom'
import {
  Users, BookOpen, MessageCircle, UsersRound, Settings, Cat,
  Sparkles, Compass, Library,
} from 'lucide-react'
import clsx from 'clsx'

const nav = [
  { to: '/personas', label: '人设', icon: Users },
  { to: '/story', label: '剧情演绎', icon: BookOpen },
  { to: '/community', label: 'OC社区', icon: UsersRound },
  { to: '/roam', label: 'OC漫游', icon: Compass },
  { to: '/pet', label: '卓宠', icon: Cat },
  { to: '/library', label: '技能库', icon: Library },
  { to: '/settings', label: '设置', icon: Settings },
]

export function Sidebar() {
  const loc = useLocation()
  return (
    <aside className="w-60 shrink-0 border-r border-border bg-raised flex flex-col">
      {/* Logo */}
      <div className="px-5 py-5 border-b border-border">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-accent-500 to-accent-700 flex items-center justify-center shadow-glow">
            <Sparkles size={18} className="text-white" />
          </div>
          <div>
            <div className="text-sm font-semibold text-bright tracking-tight">OLY Persona</div>
            <div className="text-[10px] text-muted">AI-driven roleplay</div>
          </div>
        </div>
      </div>

      {/* Nav */}
      <nav className="flex-1 px-3 py-4 space-y-1">
        {nav.map(({ to, label, icon: Icon }) => {
          const active = loc.pathname === to || loc.pathname.startsWith(to + '/')
          return (
            <NavLink
              key={to}
              to={to}
              className={clsx(
                'flex items-center gap-3 px-3 py-2.5 rounded-md text-sm transition-all duration-150',
                active
                  ? 'bg-accent-600/20 text-accent-300 border border-accent-600/30'
                  : 'text-muted hover:text-body hover:bg-surface border border-transparent'
              )}
            >
              <Icon size={18} className={active ? 'text-accent-400' : ''} />
              <span className="font-medium">{label}</span>
            </NavLink>
          )
        })}
      </nav>

      {/* Footer */}
      <div className="px-3 py-3 border-t border-border">
        <div className="px-3 py-2 rounded-md bg-surface/50 text-[11px] text-muted/70 leading-relaxed">
          Demo v0.1 · 所有数据存储在本地内存中
        </div>
      </div>
    </aside>
  )
}
