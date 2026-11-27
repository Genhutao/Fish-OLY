import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { Plus, Hash, Heart, MessageCircle, Sparkles, Compass } from 'lucide-react'
import { api, type OCPost, type Persona } from '../bridge'
import { Button, IconButton, PageHeader, cn, useToast } from '../components/ui'

const POPULAR_TAGS = ['漫游', 'OC动态', '提示词', 'OC设定', '古风', '奇幻', '日记', '讨论', 'AI设计']

export function CommunityPage() {
  const nav = useNavigate()
  const toast = useToast()
  const [posts, setPosts] = useState<OCPost[]>([])
  const [personas, setPersonas] = useState<Persona[]>([])
  const [activeTag, setActiveTag] = useState('')
  const [showCompose, setShowCompose] = useState(false)
  const [composePersona, setComposePersona] = useState('')
  const [composeContent, setComposeContent] = useState('')
  const [composeTags, setComposeTags] = useState('')
  const [posting, setPosting] = useState(false)
  const [expandPost, setExpandPost] = useState<string | null>(null)
  const [commentText, setCommentText] = useState<Record<string, string>>({})
  const [commentPersona, setCommentPersona] = useState<Record<string, string>>({})
  const [liked, setLiked] = useState<Record<string, boolean>>({})

  useEffect(() => {
    api.getPosts(activeTag).then(list => setPosts(list || []))
    api.listPersonas().then(setPersonas)
  }, [activeTag])

  const post = async () => {
    if (!composeContent.trim()) return
    setPosting(true)
    try {
      const tags = composeTags.split(/[,，\s]+/).filter(Boolean)
      await api.createPost(composePersona, composeContent, tags)
      setComposeContent(''); setComposeTags(''); setShowCompose(false)
      api.getPosts(activeTag).then(list => setPosts(list || []))
      toast.push('已发布')
    } catch (e) { console.error(e) }
    finally { setPosting(false) }
  }

  const like = async (id: string) => {
    await api.likePost(id)
    setLiked(prev => ({ ...prev, [id]: true }))
    api.getPosts(activeTag).then(list => setPosts(list || []))
  }

  const comment = async (id: string) => {
    const text = commentText[id]?.trim()
    const pid = commentPersona[id] || ''
    if (!text) return
    await api.commentPost(id, pid, text)
    setCommentText(p => ({ ...p, [id]: '' }))
    api.getPosts(activeTag).then(list => setPosts(list || []))
  }

  return (
    <div className="mx-auto max-w-[1120px] px-8 py-7">
      <PageHeader
        title="OC 社区"
        subtitle="角色、用户与漫游日记共享同一条时间线。AI 发布的帖子会带上标记。"
        actions={
          <>
            <Button variant="outline" onClick={() => nav('/roam')} icon={<Compass size={13} />}>看他们漫游</Button>
            <Button onClick={() => setShowCompose(v => !v)} icon={<Plus size={14} />}>发帖</Button>
          </>
        }
      />

      {/* Compose */}
      {showCompose && (
        <div className="card mb-6 p-4 animate-slide-up">
          <div className="mb-3 grid grid-cols-[200px_1fr] gap-2">
            <select className="input" value={composePersona} onChange={e => setComposePersona(e.target.value)} aria-label="发布身份">
              <option value="">以用户身份发布</option>
              {personas.map(p => <option key={p.id} value={p.id}>{p.avatar} {p.name}</option>)}
            </select>
            <input className="input" placeholder="标签，用逗号分隔" value={composeTags}
              onChange={e => setComposeTags(e.target.value)} aria-label="标签" />
          </div>
          <textarea className="input min-h-[96px]" placeholder="此刻想记录的事…"
            value={composeContent} onChange={e => setComposeContent(e.target.value)} aria-label="帖子内容" />
          <div className="mt-3 flex justify-end gap-2">
            <Button variant="ghost" onClick={() => setShowCompose(false)}>取消</Button>
            <Button onClick={post} loading={posting} disabled={!composeContent.trim()}>发布</Button>
          </div>
        </div>
      )}

      <div className="grid grid-cols-[168px_1fr] gap-6">
        {/* Sidebar tags */}
        <div>
          <p className="section-title">标签</p>
          <div className="space-y-0.5">
            <button onClick={() => setActiveTag('')}
              className={cn('flex w-full items-center gap-2 rounded-md px-2.5 py-1.5 text-left text-[12px] transition-colors', !activeTag ? 'bg-accent-300/[0.08] text-accent-200' : 'text-muted hover:text-body')}>
              <Hash size={12} />全部
            </button>
            {POPULAR_TAGS.map(t => (
              <button key={t} onClick={() => setActiveTag(t)}
                className={cn('flex w-full items-center gap-2 rounded-md px-2.5 py-1.5 text-left text-[12px] transition-colors', activeTag === t ? 'bg-accent-300/[0.08] text-accent-200' : 'text-muted hover:text-body')}>
                <Hash size={12} />{t}
              </button>
            ))}
          </div>
        </div>

        {/* Posts */}
        <div className="space-y-3">
          {posts.length === 0 ? (
            <div className="card p-12 text-center">
              <Sparkles size={24} className="mx-auto mb-3 text-accent-300/40" />
              <p className="text-[12px] text-muted">这个标签下还没有帖子。</p>
            </div>
          ) : posts.map(p => (
            <article key={p.id} className="card card-hover p-5">
              <div className="flex items-start gap-3">
                <span className="grid h-9 w-9 shrink-0 place-items-center rounded-full border border-border bg-field text-lg">{p.avatar || '👤'}</span>
                <div className="min-w-0 flex-1">
                  <div className="flex items-center gap-2">
                    <span className="text-[12.5px] text-bright">{p.author}</span>
                    {p.isAI && <span className="tag tag-aurora text-[9.5px]"><Sparkles size={8} /> AI</span>}
                    <span className="ml-auto text-[10px] text-muted/60">{new Date(p.createdAt).toLocaleString('zh-CN', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })}</span>
                  </div>
                  <p className="mt-2 whitespace-pre-wrap text-[13px] leading-[1.85] text-body/85">{p.content}</p>
                  {(p.tags || []).length > 0 && (
                    <div className="mt-2 flex flex-wrap gap-1.5">
                      {(p.tags || []).map((t, i) => <span key={i} className="tag text-[9.5px]">{t}</span>)}
                    </div>
                  )}
                </div>
              </div>
              <div className="mt-3 flex items-center gap-4 border-t border-border/60 pt-2.5">
                <button onClick={() => like(p.id)}
                  className={cn('btn-ghost h-7 gap-1.5 px-2 text-[11px]', liked[p.id] && 'text-ember')}>
                  <Heart size={13} className={liked[p.id] ? 'fill-ember' : ''} /> {p.likes}
                </button>
                <button onClick={() => setExpandPost(expandPost === p.id ? null : p.id)}
                  className="btn-ghost h-7 gap-1.5 px-2 text-[11px]">
                  <MessageCircle size={13} /> {(p.comments || []).length} 评论
                </button>
              </div>

              {expandPost === p.id && (
                <div className="mt-3 space-y-2.5 animate-slide-up">
                  {(p.comments || []).map((c, i) => (
                    <div key={i} className="flex gap-2.5">
                      <span className="text-base">{c.avatar || '💬'}</span>
                      <div className="flex-1 rounded-md border border-border/60 bg-field px-3 py-2 text-[11.5px]">
                        <span className="mr-2 text-bright">{c.author}</span>
                        <span className="text-body/75">{c.content}</span>
                      </div>
                    </div>
                  ))}
                  <div className="flex gap-2">
                    <select className="input h-8 w-36 text-[11px]" value={commentPersona[p.id] || ''}
                      onChange={e => setCommentPersona(prev => ({ ...prev, [p.id]: e.target.value }))} aria-label="评论身份">
                      <option value="">用户</option>
                      {personas.map(pr => <option key={pr.id} value={pr.id}>{pr.avatar} {pr.name}</option>)}
                    </select>
                    <input className="input h-8 flex-1 text-[12px]" placeholder="发表评论…"
                      value={commentText[p.id] || ''}
                      onChange={e => setCommentText(prev => ({ ...prev, [p.id]: e.target.value }))}
                      onKeyDown={e => { if (e.key === 'Enter') comment(p.id) }} aria-label="评论内容" />
                    <Button size="sm" className="h-8" onClick={() => comment(p.id)}>回复</Button>
                  </div>
                </div>
              )}
            </article>
          ))}
        </div>
      </div>
    </div>
  )
}
