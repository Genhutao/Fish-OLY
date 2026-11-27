import { useState, useEffect, useCallback } from 'react'
import { api, type Persona, type ChatMessage } from '../bridge'

export function usePersonas() {
  const [personas, setPersonas] = useState<Persona[]>([])
  const [loading, setLoading] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const list = await api.listPersonas()
      setPersonas(list || [])
    } catch (e) {
      console.error('listPersonas failed:', e)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { refresh() }, [refresh])
  return { personas, loading, refresh }
}

export function useChat(personaId: string) {
  const [messages, setMessages] = useState<ChatMessage[]>([])
  const [sending, setSending] = useState(false)

  const send = useCallback(async (text: string) => {
    if (!text.trim() || sending) return
    setSending(true)
    try {
      const reply = await api.chat({ personaId, message: text })
      setMessages(prev => [...prev, reply])
    } catch (e) {
      console.error('chat failed:', e)
    } finally {
      setSending(false)
    }
  }, [personaId, sending])

  return { messages, sending, send, setMessages }
}
