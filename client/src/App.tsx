import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import {
  api,
  subscribe,
  type Hit,
  type Message,
  type Persona,
  type Session,
  type Slot,
  type View,
} from './api'

type Sel = { kind: 'scene' | 'dm'; id: string }
type Tab = 'memory' | 'events' | 'plan'

const LAYER_NAME: Record<string, string> = {
  K: '关系', R: '人格', E: '情景', F: '事实', WM: '工作', M: '元记忆',
}

function clockOf(m: number) {
  const t = (4 * 60 + m) % 1440
  const hh = String(Math.floor(t / 60)).padStart(2, '0')
  const mm = String(t % 60).padStart(2, '0')
  return `${hh}:${mm}`
}

function timeOf(iso: string) {
  const d = new Date(iso)
  return isNaN(d.getTime()) ? '' : d.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })
}

export default function App() {
  const [uid, setUid] = useState<string>('default')
  const [users, setUsers] = useState<{ id: string; tick: number; day: number; model: string }[]>([])
  const [view, setView] = useState<View | null>(null)
  const [sel, setSel] = useState<Sel>({ kind: 'scene', id: '' })
  const [tab, setTab] = useState<Tab>('memory')
  const [memPid, setMemPid] = useState('')
  const [draft, setDraft] = useState('')
  const [busy, setBusy] = useState(false)
  const [toast, setToast] = useState('')
  const [query, setQuery] = useState('')
  const [hits, setHits] = useState<Hit[]>([])
  const [recall, setRecall] = useState<{ block: string; remember: string } | null>(null)
  const [newUser, setNewUser] = useState('')
  const bottom = useRef<HTMLDivElement>(null)

  const refreshUsers = useCallback(() => {
    api.users().then((r) => setUsers(r.users)).catch(() => {})
  }, [])

  const applyView = useCallback((v: View) => {
    setView(v)
    setSel((s) => {
      if (s.id) return s
      const first = v.scenes[0]
      return first ? { kind: 'scene', id: first.id } : s
    })
    setMemPid((p) => (p && v.memory[p] ? p : v.personas[0]?.id ?? ''))
  }, [])

  useEffect(() => {
    refreshUsers()
  }, [refreshUsers])

  useEffect(() => {
    setView(null)
    setSel({ kind: 'scene', id: '' })
    setHits([])
    api.state(uid).then(applyView).catch((e) => setToast(String(e)))
    return subscribe(uid, applyView)
  }, [uid, applyView])

  useEffect(() => {
    bottom.current?.scrollIntoView({ behavior: 'smooth', block: 'end' })
  }, [view?.messageCount, sel.id])

  const messages: Message[] = useMemo(() => {
    if (!view) return []
    if (sel.kind === 'dm') return view.dms[sel.id] ?? []
    return view.scenes.find((s) => s.id === sel.id)?.messages ?? []
  }, [view, sel])

  const title = useMemo(() => {
    if (!view) return ''
    if (sel.kind === 'dm') return view.personas.find((p) => p.id === sel.id)?.name ?? '单聊'
    return view.scenes.find((s) => s.id === sel.id)?.name ?? '场景'
  }, [view, sel])

  const subtitle = useMemo(() => {
    if (!view) return ''
    if (sel.kind === 'dm') return '单聊 · 记忆贴着这条发言注入'
    const sc = view.scenes.find((s) => s.id === sel.id)
    return sc ? `${sc.topic}（在场：${(sc.here ?? []).join('、') || '无人'}）` : ''
  }, [view, sel])

  const memStats = view && memPid ? view.memory[memPid] : undefined

  async function send() {
    const text = draft.trim()
    if (!text || busy || !sel.id) return
    setBusy(true)
    setDraft('')
    try {
      const res =
        sel.kind === 'scene'
          ? await api.sceneChat(uid, sel.id, text)
          : await api.chat(uid, sel.id, text)
      if (res.result.error) {
        setToast('模型错误：' + res.result.error)
      } else {
        setRecall({ block: res.result.memBlock, remember: res.result.remember })
      }
      applyView(res.view)
    } catch (e) {
      setToast(String(e))
    } finally {
      setBusy(false)
    }
  }

  async function doTick() {
    try {
      applyView(await api.tick(uid))
    } catch (e) {
      setToast(String(e))
    }
  }

  async function doConsolidate() {
    try {
      const r = await api.consolidate(uid)
      setToast(
        `巩固完成：合并 ${r.report.merged} · 升降 ${r.report.rebalanced} · 遗忘 ${r.report.forgotten}`,
      )
      applyView(r.view)
    } catch (e) {
      setToast(String(e))
    }
  }

  async function doSearch() {
    if (!query.trim() || !memPid) return
    try {
      const r = await api.search(uid, memPid, query.trim())
      setHits(r.hits)
    } catch (e) {
      setToast(String(e))
    }
  }

  async function memOp(kind: 'forget' | 'revive', id: string) {
    try {
      const r = kind === 'forget' ? await api.forget(uid, id) : await api.revive(uid, id)
      applyView(r.view)
    } catch (e) {
      setToast(String(e))
    }
  }

  async function addUser() {
    if (!newUser.trim()) return
    try {
      const r = await api.createUser(newUser.trim())
      setNewUser('')
      refreshUsers()
      setUid(r.id)
    } catch (e) {
      setToast(String(e))
    }
  }

  useEffect(() => {
    if (!toast) return
    const t = window.setTimeout(() => setToast(''), 4200)
    return () => window.clearTimeout(t)
  }, [toast])

  if (!view) {
    return (
      <div className="boot">
        <div className="boot-mark">OLY</div>
        <div className="boot-sub">正在连接 {uid} 的世界…</div>
      </div>
    )
  }

  return (
    <div className="app">
      <header className="top">
        <div className="brand">
          OLY<span>世界引擎</span><em>MULTI-USER</em>
        </div>
        <div className="userswitch">
          <select value={uid} onChange={(e) => setUid(e.target.value)}>
            {users.map((u) => (
              <option key={u.id} value={u.id}>
                {u.id} · day{u.day}
              </option>
            ))}
          </select>
          <input
            value={newUser}
            placeholder="新用户名"
            onChange={(e) => setNewUser(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && addUser()}
          />
          <button className="btn small" onClick={addUser}>＋</button>
        </div>
        <div className="stats">
          <span>模型 <b className="badge">{view.model}</b></span>
          <span>day <b>{view.day}</b> <b className="gold">{view.clock}</b></span>
          <span>会话 <b>{view.sessions.filter((s) => s.status === 'active').length}</b></span>
          <span>预算 <b className={view.budget.over ? 'red' : ''}>{view.budget.used}/{view.budget.limit}</b></span>
          {view.dormant && <span className="dormant">沉睡中</span>}
        </div>
        <button className="btn primary" onClick={doTick}>＋1 分钟</button>
        <button className="btn" onClick={doConsolidate}>巩固</button>
      </header>

      <main className="grid">
        <aside className="left">
          <h3>角色</h3>
          {view.personas.map((p) => (
            <PersonaCard
              key={p.id}
              p={p}
              active={sel.kind === 'dm' && sel.id === p.id}
              onClick={() => setSel({ kind: 'dm', id: p.id })}
            />
          ))}
          <h3>场景</h3>
          {view.scenes.map((s) => (
            <div
              key={s.id}
              className={'room' + (sel.kind === 'scene' && sel.id === s.id ? ' on' : '')}
              onClick={() => setSel({ kind: 'scene', id: s.id })}
            >
              <div>
                <div className="rname">{s.name}</div>
                <div className="rtopic">{(s.here ?? []).join('、') || '空'}</div>
              </div>
              <span className="kind">{(s.here ?? []).length}/{s.capacity}</span>
            </div>
          ))}
          <h3>今日会话</h3>
          <div className="sessions">
            {view.sessions
              .filter((s) => s.day === view.day && (s.kind === 'pair' || s.status === 'active'))
              .slice(0, 6)
              .map((s) => (
                <div key={s.id} className={'sess ' + s.status}>
                  <span className="stime">{clockOf(s.start)}–{clockOf(s.end)}</span>
                  <span className="skind">{s.kind === 'pair' ? '串门' : '独处'}</span>
                  <span className="sname">{s.sceneName}</span>
                  <span className="sstatus">{statusLabel(s)}</span>
                </div>
              ))}
            {view.sessions.filter((s) => s.day === view.day && s.kind === 'pair').length === 0 && (
              <div className="none">今天暂时没有串门安排</div>
            )}
          </div>
        </aside>

        <section className="center">
          <div className="chead">
            <div>
              <div className="ctitle">{title}</div>
              <div className="csub">{subtitle}</div>
            </div>
            {recall?.remember && <span className="chip">本轮写入 · {recall.remember}</span>}
          </div>

          <div className="msgs">
            {messages.length === 0 && (
              <div className="empty">还没有消息。说一句，或者推进一分钟。</div>
            )}
            {messages.map((m) =>
              m.actor === 'system' ? (
                <div key={m.id} className="sys">{m.text}</div>
              ) : (
                <div key={m.id} className={'msg' + (m.actor === 'user' ? ' me' : '')}>
                  <div className="meta">
                    {m.name || (m.actor === 'user' ? '我' : '')}
                    {m.mock && <span className="mock">mock</span>}
                    {!!m.score && <span className="score">×{m.score.toFixed(2)}</span>}
                    <span className="t">{timeOf(m.at)}</span>
                  </div>
                  <div className="bubble">{m.text}</div>
                </div>
              ),
            )}
            {busy && <div className="typing">对方正在输入…</div>}
            <div ref={bottom} />
          </div>

          <form
            className="composer"
            onSubmit={(e) => {
              e.preventDefault()
              send()
            }}
          >
            <textarea
              value={draft}
              placeholder="说点什么…（Enter 发送，Shift+Enter 换行）"
              onChange={(e) => setDraft(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && !e.shiftKey) {
                  e.preventDefault()
                  send()
                }
              }}
            />
            <button className="btn primary" disabled={busy || !draft.trim()}>
              {busy ? '…' : '发送'}
            </button>
          </form>
        </section>

        <aside className="right">
          <div className="tabs">
            <button className={tab === 'memory' ? 'on' : ''} onClick={() => setTab('memory')}>
              记忆库
            </button>
            <button className={tab === 'plan' ? 'on' : ''} onClick={() => setTab('plan')}>
              日程
            </button>
            <button className={tab === 'events' ? 'on' : ''} onClick={() => setTab('events')}>
              事件流
            </button>
          </div>

          {tab === 'memory' && (
            <>
              <div className="pidpick">
                {view.personas.map((p) => (
                  <button
                    key={p.id}
                    className={memPid === p.id ? 'on' : ''}
                    onClick={() => setMemPid(p.id)}
                  >
                    {p.avatar} {p.name}
                  </button>
                ))}
              </div>
              <div className="search">
                <input
                  value={query}
                  placeholder={`检索 ${view.personas.find((p) => p.id === memPid)?.name ?? ''} 的记忆`}
                  onChange={(e) => setQuery(e.target.value)}
                  onKeyDown={(e) => e.key === 'Enter' && doSearch()}
                />
                <button className="btn small" onClick={doSearch}>检索</button>
              </div>

              {recall?.block && (
                <div className="recall">
                  <b>注入到这一轮</b>
                  <pre>{recall.block}</pre>
                </div>
              )}

              {hits.length > 0 && (
                <div className="hits">
                  <h3>检索结果</h3>
                  {hits.map((h) => (
                    <div key={h.slotId} className="hit">
                      <span className="score">{h.score.toFixed(3)}</span>
                      <span className="layer">{h.layer}</span>
                      {h.text}
                    </div>
                  ))}
                </div>
              )}

              {memStats && (
                <>
                  <h3>
                    槽 {memStats.slots} · 成员 {memStats.active}
                    {memStats.forgotten > 0 && ` · 撤回 ${memStats.forgotten}`}
                  </h3>
                  {(memStats.slotsList ?? []).slice().reverse().map((s) => (
                    <SlotCard key={s.id} slot={s} onOp={memOp} />
                  ))}
                </>
              )}
            </>
          )}

          {tab === 'plan' && (
            <div className="planbox">
              {view.personas.map((p) => (
                <div key={p.id} className="plan">
                  <h3>{p.avatar} {p.name} · day {view.day}</h3>
                  {(p.plan?.intents ?? []).map((it) => {
                    const now = view.tickOfDay
                    const cls =
                      it.status === 'met' ? 'met' : it.status === 'notmet' ? 'notmet'
                        : now >= it.start && now < it.end ? 'now' : ''
                    return (
                      <div key={it.id} className={'intent ' + cls}>
                        <span className="itime">{clockOf(it.start)}–{clockOf(it.end)}</span>
                        <span className="iscene">
                          {it.kind === 'visit' ? `→ 串门找 ${(view.personas.find((x) => x.id === it.target)?.name ?? it.target)}` : view.scenes.find((s) => s.id === it.sceneId)?.name ?? it.sceneId}
                        </span>
                        <span className="istatus">{intentLabel(it.status)}</span>
                      </div>
                    )
                  })}
                </div>
              ))}
            </div>
          )}

          {tab === 'events' && (
            <>
              <h3>最近事件</h3>
              {view.log.slice().reverse().map((e) => (
                <div key={e.seq} className="ev">
                  <span className="seq">{e.seq}</span>
                  <span className="etype">{e.type}</span>
                  <span className="etext">{e.text || (e.data ? JSON.stringify(e.data).slice(0, 42) : '')}</span>
                </div>
              ))}
            </>
          )}
        </aside>
      </main>

      {toast && <div className="toast">{toast}</div>}
    </div>
  )
}

function statusLabel(s: Session) {
  switch (s.status) {
    case 'active': return '进行中'
    case 'ended': return `已结束 · ${s.said} 句`
    case 'cancelled': return '已取消'
    case 'notmet': return '没遇上'
    default: return '已排定'
  }
}

function intentLabel(st: string) {
  switch (st) {
    case 'met': return '✓ 见面'
    case 'notmet': return '✗ 没遇上'
    default: return ''
  }
}

function PersonaCard({ p, active, onClick }: { p: Persona; active: boolean; onClick: () => void }) {
  const bonds = Object.entries(p.bond ?? {})
  return (
    <div className={'persona' + (active ? ' on' : '')} onClick={onClick}>
      <div className="avatar">{p.avatar}</div>
      <div className="pbody">
        <div className="pname">{p.name}</div>
        <div className="pmood">
          {p.mood} · 好感 {p.affection.toFixed(1)}
          {bonds.length > 0 && ` · ${bonds.map(([id, v]) => `${p.name === id ? '' : ''}${v.toFixed(1)}`).join('/')}`}
        </div>
        <div className={'bar' + (p.energy < 30 ? ' low' : '')}>
          <i style={{ width: `${Math.max(3, p.energy)}%` }} />
        </div>
        <div className="pact">{p.intentNow || p.activity}</div>
      </div>
    </div>
  )
}

function SlotCard({ slot, onOp }: { slot: Slot; onOp: (k: 'forget' | 'revive', id: string) => void }) {
  return (
    <div className="slot">
      <div className="shead">
        <span className="layer">{slot.layer}</span>
        <span className="lname">{LAYER_NAME[slot.layer] ?? ''}</span>
        <span className="members">{slot.members.length} 成员</span>
        <span className="vec">{slot.vec}</span>
      </div>
      <div className="stext">{slot.text}</div>
      {slot.members.map((m) => (
        <div key={m.id} className={'member' + (m.state === 'forgotten' ? ' off' : '')}>
          <span className="mtext">{m.text}</span>
          <span className="mw">w{m.w}</span>
          <button className="link" onClick={() => onOp(m.state === 'forgotten' ? 'revive' : 'forget', m.id)}>
            {m.state === 'forgotten' ? '恢复' : '撤回'}
          </button>
        </div>
      ))}
    </div>
  )
}
