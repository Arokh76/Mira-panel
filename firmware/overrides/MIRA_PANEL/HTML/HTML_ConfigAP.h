const char Page_ConfigAP[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Point d'accès Wi-Fi</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--danger:#9b4a4a;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64;line-height:1.45}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10)}.card.full{grid-column:1/-1}.field{margin:0 0 16px}label{display:block;font-weight:700;margin:0 0 7px}input[type=text]{display:block;width:100%% !important;min-height:46px;padding:11px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:16px;background:#fff;color:var(--ink)}input:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.btn{border:0;border-radius:14px;padding:12px 18px;background:var(--active);color:#fff;font-weight:800;font-size:16px;cursor:pointer;text-decoration:none}.btn.secondary{background:#607d8b}.switchline{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px 16px;background:var(--soft);border-radius:14px;margin:10px 0}.switchline input[type=checkbox]{width:26px;height:26px;accent-color:var(--active)}.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0}.hide{display:none!important}.footnote{margin-top:18px;color:var(--muted);font-size:13px;line-height:1.45}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.grid{grid-template-columns:1fr}.card.full{grid-column:auto}.hero h1{font-size:30px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div><nav class="nav"><a href="/">Accueil</a><a href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a class="active" href="/load?nav=params">Paramètres</a><a href="/load?nav=system">Système</a></nav></div></header>
<main><div class="hero"><h1>Point d'accès Wi-Fi</h1><p>Configuration du réseau direct utilisé pour accéder localement à Mira Panel.</p></div><div class="grid"><section class="card full"><form action="/save?config=AP" method="POST"><div class="field"><label>Clé d'accès</label><input id="pass" name="pass" value="%MDPAP%" type="text"></div><div class="field"><label>Adresse IP du point d'accès</label><input id="ip" name="ip" value="%IPAP%" type="text"></div>%ACTIFAP%<p class="footnote">Cette clé est également utilisée par les fonctions HTTP historiques du panneau.</p><div class="actions"><button class="btn" type="submit">Enregistrer</button><a class="btn secondary" href="/load?nav=params">Retour</a></div></form></section></div></main>
<script>function ClickToogle(box){document.getElementById('actif').value=box.checked?'1':'0';var t=document.getElementById('texte');if(t)t.textContent=box.checked?'Actif':'Inactif';}</script>
</body>
</html>
)rawliteral";
