const char Page_Enregistrement[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Configuration enregistrée</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--ok:#4f7c68}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink);min-height:100vh}header{background:var(--panel);color:var(--txt);box-shadow:0 2px 12px rgba(0,0,0,.12)}.bar{max-width:820px;margin:auto;min-height:66px;padding:0 20px;display:flex;align-items:center;justify-content:space-between}.brand{font-size:22px;font-weight:800}
main{max-width:720px;margin:42px auto;padding:0 18px}.card{background:#fff;border-radius:22px;padding:34px;box-shadow:0 6px 24px rgba(0,0,0,.11);text-align:center}.check{width:62px;height:62px;margin:0 auto 18px;border-radius:50%%;display:flex;align-items:center;justify-content:center;background:#e7f2ed;color:var(--ok);font-size:34px;font-weight:900}.eyebrow{font-size:13px;font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:8px}h1{font-size:32px;margin:0 0 12px}.lead{margin:0;color:#455a64;line-height:1.55}.ipbox{margin:24px auto 18px;padding:15px 18px;background:var(--soft);border:1px solid var(--line);border-radius:15px}.ipbox span{display:block;color:var(--muted);font-size:13px;font-weight:800;text-transform:uppercase;letter-spacing:.05em;margin-bottom:6px}.ipbox strong{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:22px;overflow-wrap:anywhere}.loader{height:8px;background:#e7eef1;border-radius:999px;overflow:hidden;margin:24px 0 10px}.loader>div{height:100%%;width:100%%;background:var(--active);animation:pulse 1.1s ease-in-out infinite alternate}.status{color:var(--muted);font-size:14px}.btn{display:none;margin-top:20px;padding:12px 18px;border-radius:14px;background:var(--active);color:#fff;text-decoration:none;font-weight:800}.btn.show{display:inline-block}@keyframes pulse{from{opacity:.35}to{opacity:1}}
@media(max-width:620px){.card{padding:24px}h1{font-size:28px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div></div></header>
<main><section class="card"><div class="check">✓</div><div class="eyebrow">Configuration terminée</div><h1>Paramètres enregistrés</h1><p class="lead">Mira Panel redémarre maintenant pour rejoindre ton réseau local.</p><div class="ipbox"><span>Adresse configurée</span><strong id="ipValue">%IP%</strong></div><div class="loader"><div></div></div><div id="status" class="status">Redémarrage en cours…</div><a id="openPanel" class="btn" href="#">Ouvrir Mira Panel</a></section></main>
<script>
(function(){var ip=(document.getElementById('ipValue').textContent||'').trim();var status=document.getElementById('status'),btn=document.getElementById('openPanel');if(ip&&ip.toLowerCase()!=='auto'&&ip.toLowerCase()!=='null'){btn.href='http://'+ip+'/';btn.classList.add('show');setTimeout(function(){status.textContent='Le panneau devrait être de nouveau disponible.';},4500);}else{setTimeout(function(){status.textContent='DHCP actif : retrouve l’adresse attribuée par ton routeur puis ouvre Mira Panel.';},4500);}})();
</script>
</body>
</html>
)rawliteral";
