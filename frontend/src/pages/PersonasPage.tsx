import { useEffect, useMemo, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { motion } from 'framer-motion'
import { ArrowUpRight, Compass, Plus, Search, Sparkles, Users } from 'lucide-react'
import { api, type AppStats, type Persona } from '../bridge'
import { Button, EmptyState, Meter, PageHeader, cn } from '../components/ui'

const MOOD_LABEL: Record<string, string> = {
  happy: '愉快', calm: '平静', sad: '低落', angry: '恼火', tired: '疲惫', neutral: '平常',
}
const MOOD_DOT: Record<string, string> = {
  happy: 'bg-mood-happy', calm: 'bg-mood-calm', sad: 'bg-mood-sad',
  angry: 'bg-mood-angry', tired: 'bg-mood-tired', neutral: 'bg-mood-neutral',
}

function Constellation({ count }: { count: number }) {
  const stars = useMemo(() => {
    const pts: { x: number; y: number; r: number; d: number }[] = []
    for (let i = 0; i < 16; i++) {
      const a = i * 2.399963
      const rad = 8 + (i / 16) * 40
      pts.push({
        x: 50 + Math.cos(a) * rad * 1.55,
        y: 50 + Math.sin(a) * rad * 0.85,
        r: i % 5 === 0 ? 1.5 : 0.9,
        d: (i % 7) * 0.45,
      })
    }
    return pts
  }, [])
  const links = [[0, 4], [4, 7], [7, 12], [12, 15], [2, 5], [5, 9], [9, 14], [3, 8], [8, 11]]
  return (
    <svg viewBox="0 0 100 66" className="h-full w-full text-accent-300" aria-hidden>
      {links.map(([a, b], i) => (
        <line
          key={i}
          x1={stars[a].x} y1={stars[a].y} x2={stars[b].x} y2={stars[b].y}
          stroke="currentColor" strokeWidth="0.18" strokeOpacity="0.3"
        />
      ))}
      {stars.map((s, i) => (
        <g key={i} className="animate-twinkle" style={{ animationDelay: `${s.d}s`, animationDuration: `${4 + (i % 4)}s` }}>
          <circle cx={s.x} cy={s.y} r={s.r * 3.6} fill="currentColor" opacity={0.12} />
          <circle cx={s.x} cy={s.y} r={s.r} fill="currentColor" />
        </g>
      ))}
      {count > 0 && (
        <g fill="none" stroke="currentColor">
          <circle cx="50" cy="33" r="9" strokeOpacity="0.32" strokeWidth="0.25" />
          <circle cx="50" cy="33" r="14" strokeOpacity="0.14" strokeWidth="0.2" />
        </g>
      )}
    </svg>
  )
}

function ObservationPanel({ stats }: { stats: AppStats | null }) {
  const nav = useNavigate()
  const roaming = stats?.roaming ?? 0
  return (
    <section className="hero-panel relative mb-7 overflow-hidden rounded-lg border border-border">
      <div className="pointer-events-none absolute inset-y-0 right-0 w-[42%] opacity-60"
        style={{ maskImage: 'linear-gradient(to right, transparent, black 35%)', WebkitMaskImage: 'linear-gradient(to right, transparent, black 35%)' }}>
        <Constellation count={stats?.personas ?? 0} />
      </div>
      <div className="relative p-6">
        <div className="max-w-lg">
          <div className="mb-2 flex items-center gap-2 text-[10.5px] tracking-[0.14em] text-muted/80">
            <Sparkles size={11} className="text-accent-300" /> 今晚的观测
          </div>
          <p className="display text-[21px] leading-snug">
            {roaming > 0
              ? <>此刻有 {roaming} 位角色在场景里走动。</>
              : <>夜色安静，角色们各自停在自己的位置。</>}
          </p>
          <p className="mt-2 text-[12px] leading-relaxed text-muted">
            人设会随对话与漫游缓慢变化。打开漫游，让他们自己决定今晚去哪儿。
          </p>
          <div className="mt-4 flex items-center gap-3">
            <Button variant="outline" size="sm" icon={<Compass size={13} />} onClick={() => nav('/roam')}>
              进入漫游
            </Button>
            <Link to="/library" className="text-[11.5px] text-muted underline-offset-4 hover:text-accent-200 hover:underline">
              查看技能与预设
            </Link>
          </div>
        </div>
        <dl className="mt-6 flex flex-wrap gap-x-8 gap-y-3 border-t border-border/60 pt-4">
          {[
            ['角色', stats?.personas ?? 0],
            ['记忆', stats?.memories ?? 0],
            ['社区', stats?.posts ?? 0],
            ['平均好感', stats ? `${stats.avgAffection.toFixed(1)} / 10` : '0.0 / 10'],
          ].map(([label, value]) => (
            <div key={label as string} className="flex items-baseline gap-2">
              <dt className="text-[10px] text-muted/70">{label}</dt>
              <dd className="font-mono text-[13px] tabular-nums text-body">{value}</dd>
            </div>
          ))}
        </dl>
      </div>
    </section>
  )
}

function PersonaCard({ p, index }: { p: Persona; index: number }) {
  const nav = useNavigate()
  const ring = Math.max(0, Math.min(1, (p.affection + 10) / 20))
  return (
    <motion.article
      initial={{ opacity: 0, y: 8 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.28, delay: index * 0.04, ease: [0.16, 1, 0.3, 1] }}
      className="card card-hover group relative cursor-pointer overflow-hidden p-5"
      onClick={() => nav(`/personas/${p.id}`)}
      role="link"
      tabIndex={0}
      onKeyDown={e => { if (e.key === 'Enter') nav(`/personas/${p.id}`) }}
      aria-label={`打开${p.name}的对话`}
    >
      <div className="flex items-start gap-4">
        <div
          className="relative grid h-[52px] w-[52px] shrink-0 place-items-center rounded-full text-2xl"
          style={{
            background: `conic-gradient(#e7c67d ${ring * 360}deg, rgba(241,236,225,0.07) ${ring * 360}deg)`,
          }}
        >
          <span className="grid h-[46px] w-[46px] place-items-center rounded-full bg-field">{p.avatar || '🌙'}</span>
        </div>
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            <h3 className="display truncate text-[18px] leading-tight">{p.name}</h3>
            <span className="ml-auto flex items-center gap-1.5 text-[10.5px] text-muted">
              <span className={cn('h-1.5 w-1.5 rounded-full', MOOD_DOT[p.mood] || 'bg-muted')} />
              {MOOD_LABEL[p.mood] || p.mood}
            </span>
          </div>
          <p className="quote mt-1 truncate text-[12px]">「{p.coreAnchor || '尚未设定核心锚点'}」</p>
          <p className="mt-2 line-clamp-2 text-[12px] leading-relaxed text-body/65">{p.description}</p>
        </div>
      </div>

      <div className="mt-4 flex flex-wrap gap-1.5">
        {(p.tags || []).slice(0, 4).map((t, i) => <span key={i} className="tag">{t}</span>)}
        {(p.tags || []).length === 0 && <span className="tag">未打标签</span>}
      </div>

      <div className="mt-4 flex items-center gap-3">
        <span className="text-[10px] text-muted/70">好感</span>
        <span className="flex-1"><Meter value={p.affection + 10} max={20} /></span>
        <span className="font-mono text-[11px] tabular-nums text-body/80">{p.affection.toFixed(1)}</span>
      </div>

      <div className="mt-4 flex items-center justify-between border-t border-border/70 pt-3">
        <span className="text-[10.5px] text-muted/60">
          v{p.evoVersion} · {(p.skillIds || []).length} 个技能
        </span>
        <div className="flex items-center gap-1">
          <Link to={`/personas/${p.id}`} className="btn-ghost btn-sm text-[11.5px]">
            对话 <ArrowUpRight size={12} />
          </Link>
          <Link to={`/memory/${p.id}`} className="btn-ghost btn-sm text-[11.5px]">记忆</Link>
        </div>
      </div>
    </motion.article>
  )
}

export function PersonasPage() {
  const [personas, setPersonas] = useState<Persona[]>([])
  const [stats, setStats] = useState<AppStats | null>(null)
  const [query, setQuery] = useState('')
  const [tag, setTag] = useState('')
  const [sort, setSort] = useState<'recent' | 'affection' | 'name'>('recent')

  useEffect(() => {
    api.listPersonas().then(list => setPersonas(list || [])).catch(console.error)
    api.getStats().then(setStats).catch(console.error)
  }, [])

  const tags = useMemo(() => {
    const set = new Set<string>()
    personas.forEach(p => (p.tags || []).forEach(t => set.add(t)))
    return Array.from(set).slice(0, 8)
  }, [personas])

  const shown = useMemo(() => {
    let list = personas
    const q = query.trim().toLowerCase()
    if (q) list = list.filter(p => (p.name + p.description + (p.tags || []).join('')).toLowerCase().includes(q))
    if (tag) list = list.filter(p => (p.tags || []).includes(tag))
    const copy = [...list]
    if (sort === 'affection') copy.sort((a, b) => b.affection - a.affection)
    else if (sort === 'name') copy.sort((a, b) => a.name.localeCompare(b.name, 'zh-Hans-CN'))
    else copy.sort((a, b) => +new Date(b.updatedAt) - +new Date(a.updatedAt))
    return copy
  }, [personas, query, tag, sort])

  return (
    <div className="mx-auto max-w-[1120px] px-8 py-7">
      <PageHeader
        title="人设"
        subtitle="为你的 OC 建立可被记住的人格：七维属性、核心锚点，以及会被对话改写的心情。"
        actions={<Link to="/personas/new"><Button icon={<Plus size={15} />}>新建人设</Button></Link>}
        aside={<span className="tag tag-gold">{personas.length} 位</span>}
      />

      <ObservationPanel stats={stats} />

      <div className="mb-5 flex flex-wrap items-center gap-2">
        <div className="relative w-[240px]">
          <Search size={13} className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-muted" />
          <input
            className="input pl-8"
            placeholder="搜索名字、设定或标签"
            value={query}
            onChange={e => setQuery(e.target.value)}
            aria-label="搜索人设"
          />
        </div>
        <div className="flex flex-1 flex-wrap items-center gap-1.5">
          <button
            onClick={() => setTag('')}
            className={cn('h-7 rounded-full border px-3 text-[11.5px] transition-colors',
              !tag ? 'border-accent-300/40 bg-accent-300/10 text-accent-200' : 'border-border text-muted hover:text-body')}
          >
            全部
          </button>
          {tags.map(t => (
            <button
              key={t}
              onClick={() => setTag(tag === t ? '' : t)}
              className={cn('h-7 rounded-full border px-3 text-[11.5px] transition-colors',
                tag === t ? 'border-accent-300/40 bg-accent-300/10 text-accent-200' : 'border-border text-muted hover:text-body')}
            >
              {t}
            </button>
          ))}
        </div>
        <select
          value={sort}
          onChange={e => setSort(e.target.value as typeof sort)}
          className="input h-8 w-[116px] text-[11.5px]"
          aria-label="排序"
        >
          <option value="recent">最近更新</option>
          <option value="affection">好感度</option>
          <option value="name">名字</option>
        </select>
      </div>

      {shown.length === 0 ? (
        <EmptyState
          icon={<Users size={20} />}
          title={personas.length ? '没有匹配的人设' : '还没有人设'}
          description={personas.length ? '换个关键词或清空标签筛选试试。' : '从一段文字、一张设定图，或一个核心锚点开始。'}
          action={<Link to="/personas/new"><Button size="sm" icon={<Plus size={13} />}>新建人设</Button></Link>}
        />
      ) : (
        <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
          {shown.map((p, i) => <PersonaCard key={p.id} p={p} index={i} />)}
        </div>
      )}
    </div>
  )
}
