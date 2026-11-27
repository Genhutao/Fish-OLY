import { useEffect, useMemo, useState } from 'react'
import {
  BedDouble, Compass, Footprints, MapPin, Pause, Play, Radio, Send,
  Settings2, Sparkles, StepForward, Sun, Users,
} from 'lucide-react'
import { api, type AppSettings, type RoamEvent, type RoamView } from '../bridge'
import {
  Button, EmptyState, Modal, PageHeader, Slider, cn, useToast,
} from '../components/ui'

const KIND_META: Record<RoamEvent['kind'], { icon: typeof Footprints; label: string; tone: string }> = {
  move: { icon: Footprints, label: '移动', tone: 'text-info' },
  action: { icon: Sparkles, label: '行动', tone: 'text-accent-300' },
  rest: { icon: BedDouble, label: '休息', tone: 'text-warning' },
  wake: { icon: Sun, label: '醒来', tone: 'text-success' },
  post: { icon: Send, label: '发帖', tone: 'text-aurora' },
}

const MOOD_LABEL: Record<string, string> = {
  happy: '愉快', calm: '平静', sad: '低落', angry: '恼火', tired: '疲惫', neutral: '平常',
}
const MOOD_DOT: Record<string, string> = {
  happy: 'bg-mood-happy', calm: 'bg-mood-calm', sad: 'bg-mood-sad',
  angry: 'bg-mood-angry', tired: 'bg-mood-tired', neutral: 'bg-mood-neutral',
}

function timeAgo(iso: string) {
  const sec = Math.max(0, Math.floor((Date.now() - new Date(iso).getTime()) / 1000))
  if (sec < 10) return '刚刚'
  if (sec < 60) return `${sec} 秒前`
  const min = Math.floor(sec / 60)
  if (min < 60) return `${min} 分钟前`
  return `${Math.floor(min / 60)} 小时前`
}

function energyTone(v: number) {
  if (v < 30) return 'bg-ember'
  if (v < 60) return 'bg-warning'
  return 'bg-aurora'
}

export function RoamPage() {
  const toast = useToast()
  const [view, setView] = useState<RoamView | null>(null)
  const [busy, setBusy] = useState(false)
  const [settings, setSettings] = useState<AppSettings | null>(null)
  const [paramOpen, setParamOpen] = useState(false)
  const [draft, setDraft] = useState({ roamAutoPost: 0.14, roamRestThreshold: 20, roamEnergyDecay: 1 })
  const [error, setError] = useState('')

  const personas = useMemo(() => view?.personas ?? [], [view])
  const scenes = useMemo(() => view?.scenes ?? [], [view])
  const events = useMemo(() => view?.events ?? [], [view])

  useEffect(() => {
    let alive = true
    const load = () => api.getRoamState().then(v => { if (alive) setView(v) }).catch(console.error)
    load()
    const timer = setInterval(load, 2000)
    return () => { alive = false; clearInterval(timer) }
  }, [])

  useEffect(() => {
    api.getSettings().then(s => {
      setSettings(s)
      setDraft({ roamAutoPost: s.roamAutoPost, roamRestThreshold: s.roamRestThreshold, roamEnergyDecay: s.roamEnergyDecay })
    }).catch(console.error)
  }, [])

  const act = async (fn: () => Promise<RoamView>) => {
    setBusy(true); setError('')
    try { setView(await fn()) } catch (e: any) { setError(String(e?.message || e)) }
    finally { setBusy(false) }
  }

  const saveParams = async () => {
    if (!settings) return
    const next = { ...settings, ...draft }
    try {
      await api.saveSettings(next)
      setSettings(next)
      setParamOpen(false)
      toast.push('漫游参数已更新，下一拍生效')
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const byScene = (sceneId: string) => personas.filter(p => p.sceneId === sceneId)

  return (
    <div className="mx-auto max-w-[1120px] px-8 py-7">
      <PageHeader
        title="OC 漫游"
        subtitle="角色按自己的节奏走动、做事、充电。漫游产生的日记会进入社区，成为下一次对话的记忆。"
        actions={
          <>
            <select
              className="input h-9 w-[104px] text-[11.5px]"
              value={view?.interval ?? 8}
              onChange={e => act(() => api.setRoamInterval(Number(e.target.value)))}
              aria-label="推进间隔"
            >
              <option value={4}>4 秒 / 拍</option>
              <option value={8}>8 秒 / 拍</option>
              <option value={16}>16 秒 / 拍</option>
            </select>
            <Button variant="outline" onClick={() => setParamOpen(true)} icon={<Settings2 size={13} />}>漫游参数</Button>
            <Button
              variant="outline"
              disabled={!!view?.running || busy}
              onClick={() => act(() => api.roamStep())}
              icon={<StepForward size={13} />}
            >
              推进一拍
            </Button>
            <Button
              onClick={() => act(() => api.setRoamRunning(!view?.running))}
              loading={busy}
              icon={view?.running ? <Pause size={13} /> : <Play size={13} />}
            >
              {view?.running ? '暂停漫游' : '开始漫游'}
            </Button>
          </>
        }
      />

      {/* Console strip */}
      <div className="mb-6 flex flex-wrap items-center gap-x-5 gap-y-2 rounded-lg border border-border bg-veil/[0.03] px-4 py-2.5 text-[11px] text-muted">
        <span className="flex items-center gap-1.5">
          <Radio size={13} className={view?.running ? 'animate-pulse-soft text-aurora' : 'text-muted/60'} />
          {view?.running ? <span className="text-aurora">漫游进行中</span> : '已暂停'}
        </span>
        <span className="text-border">|</span>
        <span>第 <b className="font-mono font-normal text-body">{view?.tick ?? 0}</b> 拍</span>
        <span>·</span>
        <span>{personas.length} 位角色</span>
        <span>·</span>
        <span>{events.length} 条动态</span>
        <span className="ml-auto flex items-center gap-1.5 text-muted/70">
          <Users size={12} /> 每拍自动保存，可随时暂停
        </span>
      </div>

      {error && <p className="mb-4 rounded-md border border-ember/30 bg-ember/[0.07] px-3 py-2 text-[11.5px] text-ember">{error}</p>}

      {/* Personas */}
      <div className="mb-6 grid grid-cols-1 gap-3 sm:grid-cols-2">
        {personas.map(p => (
          <div key={p.personaId} className="card p-4">
            <div className="flex items-start gap-3">
              <div className="grid h-10 w-10 place-items-center rounded-full border border-border bg-field text-lg">{p.avatar}</div>
              <div className="min-w-0 flex-1">
                <div className="flex items-center gap-2">
                  <span className="display text-[15px]">{p.name}</span>
                  <span className="ml-auto tag text-[10px]">{p.status === 'resting' ? '休息中' : '漫游中'}</span>
                </div>
                <p className="mt-0.5 flex items-center gap-2 text-[10.5px] text-muted">
                  <span className="flex items-center gap-1"><span className={cn('h-1.5 w-1.5 rounded-full', MOOD_DOT[p.mood] || 'bg-muted')} />{MOOD_LABEL[p.mood] || p.mood}</span>
                  <span className="flex items-center gap-0.5"><MapPin size={10} />{p.sceneName}</span>
                </p>
              </div>
            </div>
            <div className="mt-3 flex items-center gap-2">
              <span className="w-7 text-[9.5px] text-muted/70">精力</span>
              <span className="h-[3px] flex-1 overflow-hidden rounded-full bg-track">
                <span className={cn('block h-full rounded-full transition-[width] duration-500', energyTone(p.energy))} style={{ width: `${Math.max(3, p.energy)}%` }} />
              </span>
              <span className="w-7 text-right font-mono text-[10px] tabular-nums text-muted">{Math.round(p.energy)}</span>
            </div>
            <p className="mt-2 line-clamp-1 text-[11px] text-body/65">{p.activity}</p>
          </div>
        ))}
      </div>

      <div className="grid grid-cols-1 gap-5 lg:grid-cols-[1fr_340px]">
        {/* Scenes */}
        <div>
          <p className="section-title">场景</p>
          <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
            {scenes.map(scene => {
              const here = byScene(scene.id)
              const active = here.length > 0
              return (
                <div key={scene.id} className={cn('card relative overflow-hidden p-4 transition-colors', active && 'border-accent-300/30')}>
                  <div className="flex items-start gap-2.5">
                    <span className="text-xl">{scene.icon}</span>
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center gap-2">
                        <h3 className="display text-[14px]">{scene.name}</h3>
                        <span className={cn('ml-auto text-[10px]', active ? 'text-accent-200' : 'text-muted/50')}>{here.length ? `${here.length} 人` : '无人'}</span>
                      </div>
                      <p className="mt-0.5 text-[10.5px] leading-relaxed text-muted/80">{scene.description}</p>
                    </div>
                  </div>
                  <p className="mt-2 flex items-start gap-1 text-[10px] italic leading-relaxed text-muted/50">
                    <MapPin size={10} className="mt-0.5 shrink-0" />{scene.ambience}
                  </p>
                  <div className="mt-3 min-h-[28px] border-t border-border/60 pt-2.5">
                    {here.length === 0 ? (
                      <p className="text-[10px] text-muted/40">这里暂时没有人</p>
                    ) : here.map(p => (
                      <div key={p.personaId} className="flex items-baseline gap-1.5 text-[10.5px]">
                        <span>{p.avatar}</span>
                        <span className="text-body/80">{p.name}</span>
                        <span className="truncate text-muted/60">{p.status === 'resting' ? '在角落休息' : p.activity}</span>
                      </div>
                    ))}
                  </div>
                </div>
              )
            })}
          </div>
        </div>

        {/* Feed */}
        <div>
          <p className="section-title flex items-center justify-between">
            实时动态
            {view?.running && <span className="text-[9.5px] font-normal text-aurora/70">每 2 秒刷新</span>}
          </p>
          <div className="card max-h-[520px] overflow-y-auto p-0">
            {events.length === 0 ? (
              <div className="px-6 py-10">
                <EmptyState
                  icon={<Compass size={18} />}
                  title="还没有动态"
                  description="点「推进一拍」或开始漫游，看看角色们此刻在做什么。"
                />
              </div>
            ) : (
              <ol className="relative px-4 py-3">
                <span aria-hidden className="absolute bottom-4 left-[26px] top-4 w-px bg-border/60" />
                {events.map(ev => {
                  const meta = KIND_META[ev.kind] || KIND_META.action
                  const Icon = meta.icon
                  return (
                    <li key={ev.id} className="relative flex gap-3 py-2 pl-1">
                      <span className="relative z-10 mt-0.5 grid h-[18px] w-[18px] shrink-0 place-items-center rounded-full border border-border bg-field">
                        <Icon size={9} className={meta.tone} />
                      </span>
                      <div className="min-w-0 flex-1">
                        <p className="flex items-center gap-1.5 text-[10.5px]">
                          <span className="text-base leading-none">{ev.avatar}</span>
                          <span className="text-body/85">{ev.personaName}</span>
                          <span className="text-muted/50">在 {ev.sceneName}</span>
                          <span className={cn('ml-auto shrink-0', meta.tone)}>{meta.label}</span>
                        </p>
                        <p className="mt-0.5 text-[11px] leading-relaxed text-body/65">{ev.content}</p>
                        <p className="mt-0.5 text-[9.5px] text-muted/40">{timeAgo(ev.createdAt)}</p>
                      </div>
                    </li>
                  )
                })}
              </ol>
            )}
          </div>
        </div>
      </div>

      <Modal
        open={paramOpen}
        onClose={() => setParamOpen(false)}
        title="漫游参数"
        description="这些值会在下一拍生效，并保存到本地设置。"
        footer={
          <>
            <Button variant="ghost" onClick={() => setParamOpen(false)}>取消</Button>
            <Button onClick={saveParams}>保存参数</Button>
          </>
        }
      >
        <div className="space-y-5">
          <Slider label="精力消耗倍率" value={draft.roamEnergyDecay} min={0.2} max={3} step={0.1}
            format={v => `${v.toFixed(1)}×`} onChange={v => setDraft(d => ({ ...d, roamEnergyDecay: v }))} />
          <Slider label="休息阈值" value={draft.roamRestThreshold} min={5} max={80} step={1}
            format={v => `${v} 精力`} onChange={v => setDraft(d => ({ ...d, roamRestThreshold: v }))} />
          <Slider label="发帖概率 / 拍" value={draft.roamAutoPost} min={0} max={1} step={0.01}
            format={v => `${Math.round(v * 100)}%`} onChange={v => setDraft(d => ({ ...d, roamAutoPost: v }))} />
          <p className="hint">阈值越高，角色越早休息；发帖概率决定漫游日记出现在社区里的频率。</p>
        </div>
      </Modal>
    </div>
  )
}
