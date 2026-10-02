const char Page_Donnees[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Gestion des données</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--danger:#9b4a4a;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64;line-height:1.45}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10)}.card.full{grid-column:1/-1}.card h2{margin:0 0 6px;font-size:22px}.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.btn{border:0;border-radius:14px;padding:12px 18px;background:var(--active);color:#fff;font-weight:800;font-size:16px;cursor:pointer;text-decoration:none}.btn.secondary{background:#607d8b}.btn.danger{background:var(--danger)}.dangerbox{border-left:4px solid var(--danger);padding-left:20px}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.grid{grid-template-columns:1fr}.card.full{grid-column:auto}.hero h1{font-size:30px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div><nav class="nav"><a href="/">Accueil</a><a href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a class="active" href="/load?nav=system">Système</a></nav></div></header>
<main><div class="hero"><h1>Gestion des données</h1><p>Réinitialisation sélective ou complète de la configuration locale.</p></div><div class="grid"><section class="card"><h2>Réseaux Wi-Fi</h2><p class="hint">Supprime uniquement les réseaux enregistrés. Les autres paramètres restent en place.</p><div class="actions"><button class="btn danger" onclick="if(confirm('Effacer tous les réseaux enregistrés ?'))window.location.href='/load?nav=rstnetwork'">Effacer les réseaux</button></div></section><section class="card"><h2>Paramètres</h2><p class="hint">Supprime les paramètres enregistrés sans effacer la configuration réseau.</p><div class="actions"><button class="btn danger" onclick="if(confirm('Effacer les paramètres enregistrés ?'))window.location.href='/load?nav=rstparams'">Effacer les paramètres</button></div></section><section class="card full dangerbox"><h2>Réinitialisation complète</h2><p class="hint">Efface la configuration locale de Mira Panel. Le panneau devra ensuite être reconfiguré.</p><div class="actions"><button class="btn danger" onclick="if(confirm('Réinitialiser complètement Mira Panel ?'))window.location.href='/load?nav=rstall'">Reset total</button><a class="btn secondary" href="/load?nav=system">Annuler</a></div></section></div></main>
</body>
</html>
)rawliteral";
