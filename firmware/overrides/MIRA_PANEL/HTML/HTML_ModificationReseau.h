const char Page_ModificationReseau[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Modifier un réseau</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--danger:#9b4a4a;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64;line-height:1.45}.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10)}.card h2{margin:0 0 6px;font-size:22px}.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.field{margin:0 0 16px}label{display:block;font-weight:700;margin:0 0 7px}input[type=text],input[type=password]{display:block;width:100%% !important;min-height:46px;padding:11px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:16px;background:#fff;color:var(--ink)}input:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.btn{border:0;border-radius:14px;padding:12px 18px;background:var(--active);color:#fff;font-weight:800;font-size:16px;cursor:pointer;text-decoration:none}.btn.secondary{background:#607d8b}.btn.danger{background:var(--danger)}.dangerbox{border-left:4px solid var(--danger);padding-left:20px}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.hero h1{font-size:30px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div><nav class="nav"><a href="/">Accueil</a><a class="active" href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a href="/load?nav=system">Système</a></nav></div></header>
<main><div class="hero"><h1>Modifier le réseau</h1><p>Met à jour les paramètres de cette connexion Wi-Fi.</p></div><section class="card"><form action="/save?update=wifi" method="POST"><div class="field"><label>Nom du réseau</label><input id="ssid" name="ssid" value="%SSIDUPDATE%" type="text"></div><div class="field"><label>Mot de passe</label><input id="pass" name="pass" value="%PASSUPDATE%" type="password"></div><div class="field"><label>Adresse IP</label><input id="ip" name="ip" value="%IPUPDATE%" type="text" placeholder="Auto"></div><div class="actions"><button class="btn" type="submit">Enregistrer</button><a class="btn secondary" href="/load?nav=listnetwork">Retour</a></div></form></section><section class="card dangerbox" style="margin-top:16px"><h2>Supprimer ce réseau</h2><p class="hint">Le panneau oubliera cette connexion. Les autres réseaux enregistrés ne sont pas modifiés.</p><div class="actions"><button class="btn danger" onclick="if(confirm('Supprimer ce réseau ?'))window.location.href='/load?nav=removenetwork'">Supprimer</button></div></section></main>
</body>
</html>
)rawliteral";
