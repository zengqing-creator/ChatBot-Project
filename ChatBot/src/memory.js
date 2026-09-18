const MemoryModule = {
  KEY: 'chatbot_memory_v1',
  facts: [],
  keywordIndex: {},
  preferences: [],
  emotion: null,

  // 持久化
  save() {
    localStorage.setItem(this.KEY, JSON.stringify({
      facts: this.facts.map(f => ({...f, vec: Array.from(f.vec)})),
      preferences: this.preferences,
      emotion: this.emotion
    }));
  },
  load() {
    const raw = localStorage.getItem(this.KEY);
    if (!raw) return;
    try {
      const d = JSON.parse(raw);
      this.facts = (d.facts || []).map(f => ({...f, vec: new Float32Array(f.vec)}));
      this.preferences = d.preferences || [];
      this.emotion = d.emotion || null;
      this._rebuildKeywordIndex();
    } catch (e) { console.warn('[memory] load failed', e); }
  },

  // 轻量分词：中文单字+双字，英文按词
  _tokenize(text) {
    const toks = new Set();
    const lower = text.toLowerCase();
    lower.split(/[^\w]+/).forEach(w => { if (w.length > 1) toks.add(w); });
    (text.match(/[\u4e00-\u9fa5]+/g) || []).forEach(seg => {
      for (let i = 0; i < seg.length; i++) {
        toks.add(seg[i]);
        if (i + 1 < seg.length) toks.add(seg.slice(i, i + 2));
      }
    });
    return toks;
  },
  _rebuildKeywordIndex() {
    this.keywordIndex = {};
    for (const f of this.facts) {
      for (const t of this._tokenize(f.text + ' ' + (f.tags||[]).join(' '))) {
        (this.keywordIndex[t] ||= []).push(f.id);
      }
    }
  },

  // 写事实
  async addFact(text, tags, embedFn) {
    const vec = await embedFn(text);
    if (!vec || !vec.length) return null;
    const id = Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
    const fact = { id, text, tags: tags || [], vec: new Float32Array(vec), ts: Date.now() };
    this.facts.push(fact);
    for (const t of this._tokenize(text + ' ' + fact.tags.join(' ')))
      (this.keywordIndex[t] ||= []).push(id);
    this.save();
    return id;
  },

  // 检索：语义 + 关键词混合打分
  async retrieve(query, embedFn, topK = 3) {
    if (!this.facts.length) return [];
    const qVec = new Float32Array(await embedFn(query));   // 每轮最多一次
    const qToks = this._tokenize(query);

    const scored = this.facts.map(f => {
      const sem = this._cosine(qVec, f.vec);
      const kw  = this._keywordScore(qToks, f);
      return { f, score: 0.7 * sem + 0.3 * kw, sem, kw };
    }).sort((a, b) => b.score - a.score);

    return scored.slice(0, topK).filter(s => s.score > 0.15).map(s => s.f);
  },
  _cosine(a, b) {
    let dot = 0, na = 0, nb = 0, n = Math.min(a.length, b.length);
    for (let i = 0; i < n; i++) { dot += a[i]*b[i]; na += a[i]*a[i]; nb += b[i]*b[i]; }
    return (na && nb) ? dot / Math.sqrt(na * nb) : 0;
  },
  _keywordScore(qToks, f) {
    const fToks = this._tokenize(f.text + ' ' + f.tags.join(' '));
    let hit = 0;
    for (const t of qToks) if (fToks.has(t)) hit++;
    return qToks.size ? hit / Math.sqrt(qToks.size * Math.max(fToks.size, 1)) : 0;
  },

  // Prompt 拼装
  buildContextPrompt(facts) {
    if (!facts.length) return '';
    return '【相关记忆（不要原样复述，仅作参考）】\n' +
      facts.map((f, i) => `${i+1}. ${f.text}`).join('\n');
  },

  // 偏好轨（独立）   
  addPreference(text) { this.preferences.push({ id: Date.now(), text, spoken: false }); this.save(); },
  markSpoken(id) { const p = this.preferences.find(x => x.id === id); if (p) { p.spoken = true; this.save(); } },
  getSpokenPreferences() { return this.preferences.filter(p => p.spoken).map(p => p.text); },

  // 情绪轨（独立，只影响语气）   
  setEmotion(mood, intensity = 0.5) { this.emotion = { mood, intensity, ts: Date.now() }; this.save(); },
  getEmotionHint() {
    if (!this.emotion) return '';
    const { mood, intensity } = this.emotion;
    return `【当前语气】${mood}（强度 ${(intensity*100|0)}%）`;
  }
};