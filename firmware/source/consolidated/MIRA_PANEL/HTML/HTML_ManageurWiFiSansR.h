const char Page_ManageurWiFiSansR[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Configuration Wi-Fi</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink);min-height:100vh}header{background:var(--panel);color:var(--txt);box-shadow:0 2px 12px rgba(0,0,0,.12)}.bar{max-width:900px;margin:auto;min-height:66px;padding:0 20px;display:flex;align-items:center;justify-content:space-between;gap:18px}.brand{font-size:22px;font-weight:800}.step{padding:8px 12px;border-radius:999px;background:rgba(255,255,255,.14);font-size:13px;font-weight:800}
main{max-width:820px;margin:34px auto;padding:0 18px 42px}.card{background:#fff;border-radius:22px;padding:30px;box-shadow:0 6px 24px rgba(0,0,0,.11)}.eyebrow{font-size:13px;font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:8px}h1{font-size:34px;margin:0 0 10px}.lead{margin:0 0 24px;color:#455a64;line-height:1.5}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.full{grid-column:1/-1}.field{margin:0}.field label,.field>label{display:block;font-weight:800;margin:0 0 7px}select,.box,.box1,input[type=text],input[type=password]{display:block;width:100%% !important;min-height:48px;padding:11px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:17px;background:#fff;color:var(--ink);text-align:left!important}select:focus,input:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}.note{margin-top:16px;padding:14px 16px;border-radius:14px;background:var(--soft);border:1px solid var(--line);color:#455a64;font-size:14px;line-height:1.45}.actions{display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap;margin-top:24px}.btn,.Bouton{border:0;border-radius:14px;padding:13px 20px;background:var(--active);color:#fff;font-size:16px;font-weight:800;cursor:pointer;text-decoration:none;width:auto}.btn.secondary{background:#607d8b}.btn.ghost{background:#e8eef0;color:#455a64}.btn:active,.Bouton:active{transform:scale(.98)}
@media(max-width:650px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.card{padding:22px}.grid{grid-template-columns:1fr}.full{grid-column:auto}h1{font-size:29px}.actions{flex-direction:column}.actions .btn,.actions .Bouton{width:100%%;text-align:center}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand"><img src="data:image/png;base64,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" alt="" aria-hidden="true" style="width:31px;height:25px;vertical-align:-6px;margin-right:1px">ira Panel</div><div class="step">Configuration · étape 2/2</div></div></header>
<main><section class="card"><div class="eyebrow">Mira Panel</div><h1>Connecter le panneau au Wi-Fi</h1><p class="lead">Choisis ton réseau local, puis indique le mot de passe et le mode d'adressage.</p>
<form name="reg" action="/save?config=wifi" onsubmit="return Verif();" method="POST">
  <div class="grid">
    <div class="field full">%SCANRESEAUX%</div>
    <div class="field"><label for="ssid">Nom du réseau</label><input class="box1" id="ssid" name="ssid" value="%SSID%" type="text" required></div>
    <div class="field"><label for="pass">Mot de passe Wi-Fi</label><input class="box1" id="pass" name="pass" type="password" autocomplete="current-password"></div>
    <div class="field full"><label for="ip">Adresse IP</label><input class="box1" id="ip" name="ip" value="%IP%" type="text" placeholder="Auto"></div>
  </div>
  <div class="note"><b>Conseil :</b> laisse <b>Auto</b> pour utiliser le DHCP. Une IP fixe peut être saisie si ton réseau l'exige.</div>
  <div class="actions"><a class="btn ghost" href="/load?nav=key">← Modifier la clé</a><button class="btn" type="submit">Enregistrer et redémarrer</button></div>
</form>
<div class="actions" style="margin-top:12px;justify-content:flex-end"><a class="btn secondary" href="/load?nav=reboot">Ignorer et rester en point d'accès</a></div>
</section></main>
<script>
(function(){var rs=document.getElementById('RS'),ssid=document.getElementById('ssid');if(rs&&ssid&&!ssid.value)ssid.value=rs.value||'';})();
function Click(value){var s=document.getElementById('ssid');if(s)s.value=value||'';}
function Verif(){var ip=document.forms['reg']['ip'];if(!ip.value)ip.value='Auto';if(ip.value!=='Auto'&&ip.value.length<7){alert('Saisis une adresse IP valide ou Auto.');ip.focus();return false;}return confirm('Enregistrer cette configuration Wi-Fi et redémarrer Mira Panel ?');}
</script>
</body>
</html>
)rawliteral";
