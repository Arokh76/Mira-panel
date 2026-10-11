const char Page_MQTT[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - MQTT</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}
.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}
.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{text-align:left;margin:0 0 16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64}
.card{background:#fff;border-radius:20px;padding:24px;margin:16px 0;box-shadow:0 5px 20px rgba(0,0,0,.10)}
.card h2{margin:0 0 4px;font-size:23px}.hint{font-size:14px;color:var(--muted);margin:0 0 18px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.full{grid-column:1/-1}
label{display:block;font-weight:700;margin:15px 0 7px}input[type=text],input[type=password],input[type=number]{width:100%%;padding:12px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:17px;background:#fff;color:var(--ink)}
input:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}
.switchline{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px 16px;background:#f5f8f9;border-radius:14px;margin-bottom:6px}.switchline strong{font-size:18px}.switchline input{width:26px;height:26px}
.actions{display:flex;justify-content:flex-end;margin-top:22px}.btn,.Bouton{border:0;border-radius:14px;padding:12px 24px;background:var(--active);color:#fff;font-weight:800;font-size:17px;cursor:pointer;text-decoration:none}.btn:active,.Bouton:active{transform:scale(.98)}
.topic{padding:13px 15px;background:#f5f8f9;border:1px solid var(--line);border-radius:14px;margin:10px 0}.topic b{display:block;margin-bottom:5px}.topic code{display:block;overflow-wrap:anywhere;font-size:15px;color:#37474f}.note{font-size:14px;color:#546e7a;line-height:1.45;margin-top:14px}
table{width:100%%;border-collapse:collapse;margin-top:12px}th,td{padding:11px 10px;border-bottom:1px solid var(--line);text-align:left}th{font-size:13px;text-transform:uppercase;letter-spacing:.04em;color:#546e7a}tr:last-child td{border-bottom:0}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.grid{grid-template-columns:1fr}.hero h1{font-size:30px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand"><img src="data:image/png;base64,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" alt="" aria-hidden="true" style="width:31px;height:25px;vertical-align:-6px;margin-right:1px">ira Panel</div><nav class="nav"><a href="/">Accueil</a><a href="/load?nav=wifi">Réseau</a><a class="active" href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a href="/load?nav=system">Système</a></nav></div></header>
<main>
  <div class="hero"><h1>MQTT</h1><p>Connexion du panneau au broker et configuration des topics Mira.</p></div>
  <form action="/save?config=params" method="POST">
    <div class="card">
      <div class="switchline"><div><strong>Connexion MQTT</strong><div class="hint" style="margin:3px 0 0">Active ou désactive le client MQTT du panneau.</div></div><input id="mqttActiveBox" type="checkbox" %MQTTCHECKED% onchange="document.getElementById('MQTT_Actif').value=this.checked?'1':'0'"></div>
      <input id="MQTT_Actif" name="MQTT_Actif" type="hidden" value="%MQTTACTIF%">
      <div class="grid">
        <div><label>Nom MQTT de l'écran</label><input name="MQTT_Nom" type="text" value="%MQTTNOM%" placeholder="panel"></div>
        <div><label>Adresse du broker</label><input name="MQTT_Serveur" type="text" value="%MQTTSERVEUR%" placeholder="192.168.1.x"></div>
        <div><label>Port</label><input name="MQTT_Port" type="number" min="1" max="65535" value="%MQTTPORT%"></div>
        <div><label>Utilisateur</label><input name="MQTT_Utilisateur" type="text" value="%MQTTUSER%" autocomplete="username"></div>
        <div class="full"><label>Mot de passe</label><input name="MQTT_Mot_de_passe" type="password" value="%MQTTPASS%" autocomplete="current-password"></div>
      </div>
      <div class="actions"><button class="btn" type="submit">Enregistrer</button></div>
    </div>
  </form>

  <div class="card">
    <h2>Topics Mira</h2><p class="hint">Le nom de l'écran est intégré automatiquement dans les deux chemins.</p>
    <div class="topic"><b>Commandes émises · Actions</b><code>mira/%MQTTNOM%/Actions/&lt;topic&gt;</code></div>
    <div class="topic"><b>États reçus · States</b><code>mira/%MQTTNOM%/States/&lt;topic&gt;</code></div>
    <p class="note">Une interaction tactile publie dans <b>Actions</b>. Un message reçu dans <b>States</b> met uniquement à jour l'affichage : il ne déclenche jamais une action Jeedom.</p>
  </div>

  <div class="card"><h2>Widgets déclarés</h2><p class="hint">Liste des noms utilisables comme topics MQTT.</p><table><thead><tr><th>Nom / topic</th><th>Type</th></tr></thead><tbody>%MQTTTABLE%</tbody></table></div>
</main>
</body>
</html>
)rawliteral";
