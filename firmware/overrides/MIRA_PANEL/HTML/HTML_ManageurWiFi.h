const char Page_ManageurWiFi[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Réseau</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--danger:#9b4a4a;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64;line-height:1.45}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10)}.card.full{grid-column:1/-1}.card h2{margin:0 0 6px;font-size:22px}.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.field{margin:0 0 16px}.field:last-child{margin-bottom:0}label,.field-label{display:block;font-weight:700;margin:0 0 7px}input[type=text],input[type=password],input[type=number],select,.box,.box1{display:block;width:100%% !important;min-height:46px;padding:11px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:16px;background:#fff;color:var(--ink);text-align:left !important}input:focus,select:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.btn,.Bouton{border:0;border-radius:14px;padding:12px 18px;background:var(--active);color:#fff;font-weight:800;font-size:16px;cursor:pointer;text-decoration:none;width:auto}.btn.secondary{background:#607d8b}.btn.danger{background:var(--danger)}.btn:active,.Bouton:active{transform:scale(.98)}.quick{display:grid;gap:10px}.quick a{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:13px 15px;border:1px solid var(--line);border-radius:14px;background:var(--soft);color:var(--ink);font-weight:700;text-decoration:none}.quick a span{color:var(--muted);font-weight:400;font-size:13px}.switchline{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px 16px;background:var(--soft);border-radius:14px;margin:10px 0}.switchline input[type=checkbox]{width:26px;height:26px;accent-color:var(--active)}.hide{display:none!important}.section-title{font-size:17px;font-weight:800;margin:22px 0 12px;padding-top:18px;border-top:1px solid var(--line)}.empty{padding:16px;border:1px dashed #b0bec5;border-radius:14px;color:var(--muted);background:var(--soft)}.network-list{display:grid;gap:10px}.network-item{display:flex;align-items:center;gap:10px;padding:10px 12px;border:1px solid var(--line);border-radius:14px;background:var(--soft)}.network-id{display:inline-flex;align-items:center;justify-content:center;min-width:36px;height:36px;border-radius:10px;background:#e5ecef;color:#546e7a;font-weight:800}.network-button{flex:1;border:0;background:transparent;color:var(--ink);text-align:left;font-size:16px;font-weight:800;cursor:pointer;padding:8px}.dangerbox{border-left:4px solid var(--danger);padding-left:16px}.status{padding:16px;border-radius:14px;background:var(--soft);border:1px solid var(--line);font-weight:700}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;overflow-wrap:anywhere}.footnote{margin-top:18px;color:var(--muted);font-size:13px;line-height:1.45}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.grid{grid-template-columns:1fr}.card.full{grid-column:auto}.hero h1{font-size:30px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div><nav class="nav"><a href="/">Accueil</a><a class="active" href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a href="/load?nav=system">Système</a></nav></div></header>
<main>
  <div class="hero"><h1>Réseau</h1><p>Ajout d'un réseau Wi-Fi et configuration de l'adresse IP du panneau.</p></div>
  <div class="grid">
    <section class="card full"><h2>Connexion Wi-Fi</h2><p class="hint">Choisis un réseau détecté ou saisis son nom manuellement.</p>
      <form name="reg" action="/save?config=wifi" onsubmit="return Verif();" method="POST">
        <div class="field">%SCANRESEAUX%</div>
        <div class="grid"><div class="field"><label>Nom du réseau</label><input id="ssid" name="ssid" value="%SSID%" type="text"></div><div class="field"><label>Mot de passe</label><input id="pass" name="pass" type="password" autocomplete="current-password"></div><div class="field"><label>Adresse IP</label><input id="ip" name="ip" value="%IP%" type="text" placeholder="Auto"></div></div>
        <p class="footnote">Laisse <b>Auto</b> pour utiliser le DHCP, ou indique une adresse IP fixe.</p><div class="actions"><button class="btn" type="submit">Enregistrer le réseau</button></div>
      </form>
    </section>
    <section class="card"><h2>Réseaux enregistrés</h2><p class="hint">Consulte ou modifie les connexions déjà connues.</p><div class="quick"><a href="/load?nav=listnetwork">Gérer les réseaux <span>Liste complète</span></a></div></section>
    <section class="card"><h2>Point d'accès</h2><p class="hint">Réglages du Wi-Fi direct utilisé pour la configuration locale.</p><div class="quick"><a href="/load?nav=configAP">Configurer le point d'accès <span>IP et activation</span></a></div></section>
  </div>
</main>
<script>
(function(){var rs=document.getElementById('RS'),ssid=document.getElementById('ssid');if(rs&&ssid&&!ssid.value)ssid.value=rs.value||'';})();
function Click(value){document.getElementById('ssid').value=value||'';}
function Verif(){var ip=document.forms['reg']['ip'];if(ip.value!=='Auto'&&ip.value.length<7){alert('Veuillez saisir une adresse IP valide ou Auto');ip.focus();return false;}return confirm('Voulez-vous enregistrer ces informations ?');}
</script>
</body>
</html>
)rawliteral";
