#pragma once

// FRONTEND WEB EMBUTIDO - Constelação do Consórcio (estilo Skyrim)
//
// Endpoints JSON esperados pelo painel (servidos por rotas_consorcio):
//   GET /api/consorcio/peers   -> [{ "nome": "...", "url": "http://...", "eu": true/false }]
//   GET /api/consorcio/health  -> { "valida": true/false, "altura": 3, "hashTopo": "..." }
//   GET /api/consorcio/saldos  -> { "passageiro1": 1000, "motorista1": 250 }
//   GET /api/consorcio/chain   -> [{ "indice": 0, "timestamp": 123, "proponente": "...",
//                                    "hashAnterior": "...", "hash": "...",
//                                    "txs": [{ "tipo": "...", "origem": "...", "destino": "...",
//                                              "valor": 0, "detalhe": "..." }] }]

inline const char *const PAINEL_HTML = R"HTML(<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>VaiJunto · Constelação do Consórcio</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@400;500;600&display=swap');

    :root {
      --ciano: #6fd6e6;
      --gelo: #dfeef2;
      --mudo: #8fa9b3;
      --linha: #4b6a78;
      --estrela: #ffe3a1;
      --fonte: 'Barlow Condensed', 'Futura Condensed Medium', 'Arial Narrow', sans-serif;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      font-family: var(--fonte);
      font-size: 18px;
      color: var(--gelo);
      min-height: 100vh;
      padding: 14px 14px 78px;
      background:
        radial-gradient(ellipse at 50% 38%, transparent 35%, #02040a 100%),
        url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='900' height='600'%3E%3Cfilter id='f'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='.005 .008' numOctaves='5' seed='7'/%3E%3CfeColorMatrix values='0 0 0 0 .1 0 0 0 0 .78 0 0 0 0 .82 0 0 0 1.7 -.62'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23f)'/%3E%3C/svg%3E") center/cover,
        url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='900' height='600'%3E%3Cfilter id='f'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='.007 .004' numOctaves='4' seed='21'/%3E%3CfeColorMatrix values='0 0 0 0 .3 0 0 0 0 .32 0 0 0 0 .9 0 0 0 1.5 -.62'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23f)'/%3E%3C/svg%3E") center/cover,
        #050912;
      background-attachment: fixed;
    }

    /* Fundo estrelado */
    .estrelas {
      position: fixed;
      inset: 0;
      pointer-events: none;
      background:
        radial-gradient(1.2px 1.2px at 40px 60px, #fff, transparent),
        radial-gradient(1px 1px at 150px 120px, #cfe9ff, transparent),
        radial-gradient(1.4px 1.4px at 230px 30px, #fff, transparent),
        radial-gradient(1px 1px at 90px 200px, #fff, transparent),
        radial-gradient(1.2px 1.2px at 280px 170px, #bfe6ff, transparent);
      background-size: 300px 250px;
      animation: cintila 7s ease-in-out infinite alternate;
    }
    @keyframes cintila { from { opacity: .45; } to { opacity: 1; } }
    @media (prefers-reduced-motion: reduce) { .estrelas { animation: none; } }

    /* HUD Superior */
    .hud {
      max-width: 640px;
      margin: 0 auto;
      padding: 7px 22px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 14px;
      border-top: 1px solid #8aa0a8;
      border-bottom: 1px solid #8aa0a8;
      background: #03070ecc;
      position: relative;
    }
    .hud::before, .hud::after {
      content: '◁';
      position: absolute;
      top: 50%;
      transform: translateY(-50%);
      color: #8aa0a8;
      font-size: .9rem;
    }
    .hud::before { left: 4px; }
    .hud::after { content: '▷'; right: 4px; }

    .cel { display: flex; align-items: baseline; gap: 8px; min-width: 0; }
    .rot { font-size: .72rem; letter-spacing: .12em; color: var(--mudo); text-transform: uppercase; }
    .cel b { font-weight: 500; font-size: 1.2rem; white-space: nowrap; }

    .nivel { width: clamp(50px, 12vw, 120px); height: 9px; border: 1px solid #8aa0a8; align-self: center; padding: 1px; }
    .nivel i { display: block; height: 100%; width: 0; background: linear-gradient(90deg, #3a55c8, #bcd2ff); transition: width .5s; }

    .faixa {
      width: min(300px, 80%);
      margin: 5px auto 0;
      padding: 2px 0 3px;
      text-align: center;
      background: #03070ecc;
      border: 1px solid #8aa0a8;
      clip-path: polygon(0 0, 100% 0, calc(100% - 12px) 100%, 12px 100%);
    }
    .faixa b { font-weight: 600; }

    /* Constelação SVG */
    .ceu { display: block; width: min(900px, 100%); height: auto; margin: 14px auto 0; }
    .fio { stroke: #bcd9e6; stroke-width: 1; opacity: .3; }
    .fio.meu { opacity: .9; }
    .fio.off { stroke-dasharray: 2 6; opacity: .14; }

    .glow { filter: url(#borra); opacity: .85; fill: var(--ciano); }
    .nucleo { fill: var(--estrela); stroke: #fff; stroke-width: 1.2; }
    .astro { cursor: pointer; }
    .astro:focus-visible { outline: 2px solid var(--ciano); }
    .astro.inv .glow { fill: #ff5a4a; }
    .astro.inv .nucleo { fill: #ff9a8e; }
    .astro.dif .glow { fill: #f0b840; }
    .astro.off .glow { display: none; }
    .astro.off .nucleo { fill: #566872; stroke: #7b8d96; }
    .astro text { fill: #cfe3ea; font-family: var(--fonte); font-size: 15px; letter-spacing: .12em; text-anchor: middle; }
    .sigilo { fill: none; stroke: var(--ciano); stroke-width: 1.6; filter: url(#borra2); }

    /* Fita de navegação entre os nós */
    .fita {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      align-items: flex-end;
      gap: 12px clamp(16px, 3.4vw, 46px);
      margin-top: 6px;
    }
    .hab {
      position: relative;
      color: #9fb7c0;
      text-decoration: none;
      text-transform: uppercase;
      letter-spacing: .05em;
      font-size: 1.05rem;
      padding-bottom: 9px;
      border-bottom: 2px solid #3b5666;
    }
    .hab b { color: #fff; font-weight: 600; margin-left: 4px; }
    .hab:hover { color: #fff; }
    .hab.sel { font-size: clamp(1.7rem, 4.4vw, 2.6rem); color: #fff; border: 0; padding-bottom: 14px; line-height: 1.1; }
    .hab.sel::after {
      content: '';
      position: absolute;
      left: -8%; right: -8%; bottom: 0;
      height: 7px;
      border-top: 1px solid #b4c5cc;
      border-bottom: 1px solid #b4c5cc;
      background: linear-gradient(90deg, transparent, #b4c5cc40 30%, #b4c5cc40 70%, transparent);
    }

    .desc { max-width: 600px; margin: 16px auto 0; text-align: center; font-size: 1.12rem; line-height: 1.35; text-shadow: 0 1px 4px #000; }

    /* Painéis de Contas e Blocos */
    .painel {
      position: relative;
      max-width: 900px;
      margin: 44px auto 0;
      padding: 28px 24px 22px;
      background: #040910b8;
      border: 1px solid #3e5967;
    }
    .painel::before {
      content: '◆';
      position: absolute;
      top: -9px; left: 50%;
      transform: translateX(-50%);
      padding: 0 9px;
      background: #050a12;
      color: var(--ciano);
      font-size: .75rem;
    }
    .painel h3 {
      text-align: center;
      font-weight: 500;
      font-size: 1.15rem;
      letter-spacing: .16em;
      text-transform: uppercase;
      color: var(--ciano);
      margin-bottom: 20px;
    }

    .contas { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 12px 22px; }
    .conta { display: flex; justify-content: space-between; align-items: baseline; padding-bottom: 5px; border-bottom: 1px solid var(--linha); text-transform: uppercase; letter-spacing: .05em; }
    .conta b { font-size: 1.4rem; font-weight: 600; color: var(--estrela); }
    .aviso { text-align: center; color: #ff9a8e; }

    .cadeia { position: relative; }
    .cadeia::before { content: ''; position: absolute; left: 8px; top: 12px; bottom: 12px; width: 1px; background: linear-gradient(#bcd9e655, #bcd9e6); }
    .bloco { position: relative; padding: 0 0 18px 32px; }
    .bloco::before {
      content: '';
      position: absolute;
      left: 3px; top: 6px;
      width: 11px; height: 11px;
      border-radius: 50%;
      background: var(--estrela);
      box-shadow: 0 0 0 3px #0a1822, 0 0 12px 3px #6fd6e6aa;
    }
    .bt { display: flex; flex-wrap: wrap; align-items: baseline; gap: 2px 14px; }
    .bt b { font-weight: 600; font-size: 1.25rem; letter-spacing: .06em; text-transform: uppercase; }
    .bt span { color: var(--ciano); }
    .bt time { margin-left: auto; color: var(--mudo); }
    .hs { color: var(--mudo); font-size: .95rem; margin: 2px 0 4px; }
    code { font-family: Consolas, monospace; font-size: .78rem; padding: 0 6px; border: 1px solid var(--linha); background: #000a; }
    .tx { list-style: none; }
    .tx li { padding: 2px 0; }
    .tx em { font-style: normal; font-weight: 600; color: var(--estrela); }
    .tx .tag { color: var(--ciano); font-size: .85rem; margin-right: 6px; text-transform: uppercase; }
    .tx .detalhe { color: var(--mudo); font-size: .95rem; margin-left: 6px; }

    /* Barra de Status Inferior */
    .base {
      position: fixed;
      left: 0; right: 0; bottom: 0;
      height: 48px;
      z-index: 5;
      display: flex;
      justify-content: center;
      align-items: center;
      gap: clamp(10px, 4vw, 56px);
      padding: 0 8px;
      background: linear-gradient(#000b, #000);
      border-top: 1px solid #31434d;
    }
    .barra { display: flex; align-items: center; gap: 7px; font-size: .98rem; white-space: nowrap; }
    .barra label { letter-spacing: .04em; }
    .barra label.m { color: #5aa2ff; }
    .barra label.v { color: #ff6a5c; }
    .barra label.g { color: #6fe07a; }
    .barra strong { font-weight: 500; }
    .trilho {
      width: clamp(56px, 13vw, 170px);
      height: 10px;
      border: 1px solid #8aa0a8;
      padding: 1px;
      clip-path: polygon(6px 0, 100% 0, calc(100% - 6px) 100%, 0 100%);
    }
    .trilho i { display: block; height: 100%; width: 0; transition: width .5s; }
    .m + strong + .trilho i { background: linear-gradient(#6f8cff, #1b2f9c); }
    .v + strong + .trilho i { background: linear-gradient(#ff6a5c, #8c1c14); }
    .g + strong + .trilho i { background: linear-gradient(#6fe07a, #1c7c2a); }

    @media (max-width: 560px) {
      body { font-size: 16px; }
      .hud { padding: 6px 18px; gap: 8px; }
      .base { gap: 8px; }
      .barra { font-size: .8rem; }
      .barra label { display: none; }
      .painel { padding: 24px 14px 18px; }
    }
  </style>
</head>
<body>
  <div class="estrelas"></div>

  <header>
    <div class="hud">
      <div class="cel"><span class="rot">Corte</span><b id="hud-no">—</b></div>
      <div class="cel"><span class="rot">Blocos</span><b id="hud-nivel">—</b><span class="nivel"><i id="hud-barra"></i></span></div>
      <div class="cel"><span class="rot">Cadeia</span><b id="hud-cadeia">—</b></div>
    </div>
    <div class="faixa">Tokens em circulação: <b id="tokens">—</b></div>
  </header>

  <main>
    <svg class="ceu" viewBox="0 0 900 330" role="img" aria-label="Constelação das cinco cortes do consórcio">
      <defs>
        <filter id="borra" x="-200%" y="-200%" width="500%" height="500%"><feGaussianBlur stdDeviation="6"/></filter>
        <filter id="borra2" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="1.6"/></filter>
      </defs>
      <g id="fios"></g>
      <g id="astros"></g>
    </svg>

    <nav class="fita" id="fita"></nav>
    <p class="desc" id="desc"></p>

    <section class="painel">
      <h3>Contas</h3>
      <div class="contas" id="saldos"></div>
    </section>

    <section class="painel">
      <h3>Crônica dos blocos</h3>
      <div class="cadeia" id="cadeia"></div>
    </section>
  </main>

  <footer class="base">
    <div class="barra"><label class="m">Cortes no ar</label><strong id="v1">—</strong><span class="trilho"><i id="f1"></i></span></div>
    <div class="barra"><label class="v">Cadeias íntegras</label><strong id="v2">—</strong><span class="trilho"><i id="f2"></i></span></div>
    <div class="barra"><label class="g">Em sincronia</label><strong id="v3">—</strong><span class="trilho"><i id="f3"></i></span></div>
  </footer>

  <script>
    const el = id => document.getElementById(id);
    const escaparHtml = str => String(str ?? '').replace(/[&<>"']/g, ch => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[ch]));

    // Posições das 5 cortes na constelação (índice = meuID do nó)
    const POSICOES_ESTRELAS = [
      [150, 210],
      [310,  90],
      [480, 185],
      [650,  75],
      [760, 235]
    ];

    async function buscarJson(url) {
      try {
        const resposta = await fetch(url, { cache: 'no-store', signal: AbortSignal.timeout(1500) });
        return resposta.ok ? await resposta.json() : null;
      } catch {
        return null;
      }
    }

    function atualizarBarra(indice, valor, total) {
      el('v' + indice).textContent = `${valor}/${total}`;
      el('f' + indice).style.width = `${(valor / total) * 100}%`;
    }

    function formatarTransacao(tx) {
      const tipo = escaparHtml(tx.tipo || 'TRANSFERENCIA');
      const origem = escaparHtml(tx.origem);
      const destino = tx.destino ? ` → ${escaparHtml(tx.destino)}` : '';
      const valor = tx.valor > 0 ? ` <em>${tx.valor}</em>` : '';
      const detalhe = tx.detalhe ? `<span class="detalhe">(${escaparHtml(tx.detalhe)})</span>` : '';
      return `<li><span class="tag">[${tipo}]</span>${origem}${destino}${valor}${detalhe}</li>`;
    }

    function renderizarConstelacao(peers, statusPeers, meuIndice, statusLocal) {
      let fiosHtml = '';
      for (let a = 0; a < peers.length; a++) {
        for (let b = a + 1; b < peers.length; b++) {
          const classe = (!statusPeers[a] || !statusPeers[b])
            ? 'off'
            : (a === meuIndice || b === meuIndice) ? 'meu' : '';
          fiosHtml += `<line class="fio ${classe}" x1="${POSICOES_ESTRELAS[a][0]}" y1="${POSICOES_ESTRELAS[a][1]}" x2="${POSICOES_ESTRELAS[b][0]}" y2="${POSICOES_ESTRELAS[b][1]}"/>`;
        }
      }
      el('fios').innerHTML = fiosHtml;

      el('astros').innerHTML = peers.map((peer, i) => {
        const st = statusPeers[i];
        const [x, y] = POSICOES_ESTRELAS[i];
        const classe = !st
          ? 'off'
          : !st.valida ? 'inv'
          : (statusLocal && st.hashTopo !== statusLocal.hashTopo) ? 'dif' : 'ok';

        const sigilo = peer.eu
          ? `<circle class="sigilo" cx="${x}" cy="${y}" r="26"/><circle class="sigilo" cx="${x}" cy="${y}" r="35" stroke-dasharray="3 7" opacity=".7"/>`
          : '';

        return `
          <a class="astro ${classe}" href="${escaparHtml(peer.url)}/">
            <title>${escaparHtml(peer.nome)}${st ? ` · ${st.altura} blocos` : ' · fora do ar'}</title>
            <circle cx="${x}" cy="${y}" r="${peer.eu ? 15 : 10}" class="glow"/>
            ${sigilo}
            <circle cx="${x}" cy="${y}" r="${peer.eu ? 5.5 : 4}" class="nucleo"/>
            <text x="${x}" y="${y + (peer.eu ? 56 : 32)}">${escaparHtml(peer.nome.toUpperCase())}</text>
          </a>`;
      }).join('');
    }

    function renderizarContas(saldos) {
      el('saldos').innerHTML = saldos
        ? Object.entries(saldos)
            .map(([conta, valor]) => `<div class="conta"><span>${escaparHtml(conta)}</span><b>${valor}</b></div>`)
            .join('')
        : '<p class="aviso">A cadeia desta corte está inválida: os saldos não podem ser calculados.</p>';
    }

    function renderizarCadeia(blocos) {
      el('cadeia').innerHTML = blocos.slice().reverse().map(bloco => {
        const dataHora = bloco.timestamp
          ? new Date(bloco.timestamp * 1000).toLocaleString('pt-BR')
          : 'origem';
        const eloCurto = escaparHtml((bloco.hashAnterior || '').slice(0, 12));
        const seloCurto = escaparHtml((bloco.hash || '').slice(0, 12));

        return `
          <div class="bloco">
            <div class="bt">
              <b>Bloco ${bloco.indice}</b>
              <span>${escaparHtml(bloco.proponente)}</span>
              <time>${dataHora}</time>
            </div>
            <div class="hs">elo <code>${eloCurto}…</code> selo <code>${seloCurto}…</code></div>
            <ul class="tx">${(bloco.txs || []).map(formatarTransacao).join('')}</ul>
          </div>`;
      }).join('');
    }

    async function renderizarPainel() {
      const peers = await buscarJson('/api/consorcio/peers');
      if (!peers) return;

      const meuIndice = peers.findIndex(p => p.eu);
      const noLocal = peers[meuIndice];
      const statusPeers = await Promise.all(peers.map(p => buscarJson(`${p.url}/api/consorcio/health`)));
      const statusLocal = statusPeers[meuIndice];

      const noAr = statusPeers.filter(Boolean).length;
      const integras = statusPeers.filter(s => s && s.valida).length;
      const emSincronia = statusPeers.filter(s => s && statusLocal && s.hashTopo === statusLocal.hashTopo).length;
      const alturaMaxima = Math.max(1, ...statusPeers.map(s => s ? s.altura : 0));
      const saldos = await buscarJson('/api/consorcio/saldos');

      // HUD Superior
      el('hud-no').textContent = noLocal.nome;
      el('hud-nivel').textContent = statusLocal ? statusLocal.altura : '—';
      el('hud-barra').style.width = statusLocal ? `${(statusLocal.altura / alturaMaxima) * 100}%` : '0';
      el('hud-cadeia').textContent = statusLocal ? (statusLocal.valida ? 'Íntegra' : 'Adulterada') : '—';
      el('tokens').textContent = saldos ? Object.values(saldos).reduce((soma, v) => soma + v, 0) : '—';

      // Barras Inferiores
      atualizarBarra(1, noAr, peers.length);
      atualizarBarra(2, integras, peers.length);
      atualizarBarra(3, emSincronia, peers.length);

      // Constelação e Fita de Habilidades
      renderizarConstelacao(peers, statusPeers, meuIndice, statusLocal);
      el('fita').innerHTML = peers.map((peer, i) =>
        `<a class="hab ${peer.eu ? 'sel' : ''}" href="${escaparHtml(peer.url)}/">${escaparHtml(peer.nome)}<b>${statusPeers[i] ? statusPeers[i].altura : '—'}</b></a>`
      ).join('');

      el('desc').textContent = statusLocal && !statusLocal.valida
        ? `O histórico guardado pela corte de ${noLocal.nome} foi adulterado. Ela descarta a cópia ruim e se refaz a partir das outras cortes.`
        : `A corte de ${noLocal.nome} guarda uma cópia completa do livro-razão. Um bloco só entra com a aprovação de 3 das 5 cortes, e qualquer adulteração do histórico é rejeitada pelas demais. Toque numa estrela para ver o livro pelos olhos de outra corte.`;

      renderizarContas(saldos);

      const cadeia = await buscarJson('/api/consorcio/chain');
      if (cadeia) renderizarCadeia(cadeia);
    }

    let atualizando = false;
    async function cicloAtualizacao() {
      if (atualizando) return;
      atualizando = true;
      try {
        await renderizarPainel();
      } finally {
        atualizando = false;
      }
    }

    cicloAtualizacao();
    setInterval(cicloAtualizacao, 2000);
  </script>
</body>
</html>)HTML";
