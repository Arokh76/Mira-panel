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
<header><div class="bar"><div class="brand">%APP%</div><div class="step">Configuration · étape 2/2</div></div></header>
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
