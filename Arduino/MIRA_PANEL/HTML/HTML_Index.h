const char Page_Index[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Accueil</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:20;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}
.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}
.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64}
.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10);margin-bottom:16px}.card h2{margin:0 0 6px;font-size:22px}
.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.actions{text-align:center;font-size:18px}.mira-info{font-size:clamp(22px,2.6vw,30px);line-height:1.25;margin:10px 0}.quick{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
.quick a{display:block;background:#fff;border-radius:16px;padding:18px;text-decoration:none;color:var(--ink);box-shadow:0 4px 16px rgba(0,0,0,.08)}
.quick a strong{display:block;margin-bottom:5px}.quick a span{font-size:13px;color:var(--muted)}.footer{text-align:center;color:var(--muted);font-size:13px;margin-top:20px}
.Bouton,.LittleBouton{visibility:visible;border:0;border-radius:14px;background:var(--active);color:#fff;font-weight:800;cursor:pointer;margin:7px;padding:12px 20px;transition:.15s}
.Bouton{font-size:17px;min-height:46px}.LittleBouton{font-size:14px}.Bouton:active,.LittleBouton:active{transform:scale(.98)}
.box,.inputbox{width:min(100%%,520px);min-height:48px;border:1px solid var(--line);background:#f7fafb;color:var(--ink);border-radius:12px;padding:10px 14px;font-size:16px;text-align:center;margin:8px 0 14px}
.inputbox:focus,.box:focus{outline:2px solid var(--active);outline-offset:1px}
.div_toogle{width:min(100%%,520px);min-height:58px;margin:8px auto;display:flex;align-items:center;justify-content:space-between;gap:18px;padding:10px 14px;background:#f7fafb;border-radius:14px}
.div_toogle_name{text-align:left;font-weight:700}.switch{position:relative;display:inline-block;width:58px;height:32px;flex:0 0 auto}.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;inset:0;background:#b0bec5;border-radius:999px;transition:.25s}.slider:before{position:absolute;content:"";height:24px;width:24px;left:4px;top:4px;background:#fff;border-radius:50%%;transition:.25s;box-shadow:0 1px 4px rgba(0,0,0,.25)}
.switch input:checked + .slider{background:var(--active)}.switch input:checked + .slider:before{transform:translateX(26px)}
input[type=range]{width:min(100%%,520px);accent-color:var(--active);margin:18px auto}.input_color{width:min(100%%,260px);height:54px;border:1px solid var(--line);border-radius:14px;background:#f7fafb}
.FondBlanc_Color{margin:14px 0}.Seg,.Segw{height:4px;border-radius:999px;margin:18px 30px;background:var(--panel)}.Segw{background:var(--txt)}
.picto-item{display:inline-flex;justify-content:center;align-items:center;margin:6px;width:48px;height:48px;border-radius:14px;color:#fff;background:var(--active);cursor:pointer;position:relative}
.picto-item:hover:after{content:attr(aria-label);position:absolute;bottom:58px;left:50%%;transform:translateX(-50%%);white-space:nowrap;padding:7px 10px;background:#263238;color:#fff;border-radius:8px;font-size:13px;z-index:3}
.tuile,.tuile_white,.container,.table_container{border-radius:16px;padding:14px;margin:10px auto}.tuile_white{background:#fff}.hide{display:none}
table{width:100%%;border-collapse:collapse}td,th{padding:10px;border-bottom:1px solid var(--line)}
@media(max-width:760px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.hero h1{font-size:30px}.quick{grid-template-columns:1fr 1fr}.card{padding:18px}}
@media(max-width:460px){.quick{grid-template-columns:1fr}.nav{gap:2px}.nav a{padding:8px}.div_toogle{width:100%%}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand"><img src="data:image/png;base64,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" alt="" aria-hidden="true" style="width:31px;height:25px;vertical-align:-6px;margin-right:1px">ira Panel</div><nav class="nav"><a class="active" href="/">Accueil</a><a href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a href="/load?nav=system">Système</a></nav></div></header>
<main>
  <div class="hero"><h1>Accueil</h1><p>Pilotage et informations de Mira Panel.</p></div>
  <section class="card"><h2>Commandes</h2><p class="hint">Les commandes et états ci-dessous sont synchronisés avec Jeedom.</p><div class="actions">%ACTIONS%</div></section>
  <section class="quick">
    <a href="/load?nav=mqtt"><strong>MQTT</strong><span>Broker, topics et widgets.</span></a>
    <a href="/load?nav=wifi"><strong>Réseau</strong><span>Wi-Fi et réseaux enregistrés.</span></a>
    <a href="/load?nav=params"><strong>Paramètres</strong><span>Réglages du panneau.</span></a>
    <a href="/load?nav=system"><strong>Système</strong><span>Maintenance et mises à jour.</span></a>
  </section>
  <div class="footer">Mira Panel · Interface locale</div>
</main>
<script>
function Click(element){var xhr=new XMLHttpRequest();xhr.open("POST","/update?send="+element.id,true);xhr.send();}
function ClickToogle(element,value0,value1,id0,id1){var xhr=new XMLHttpRequest();xhr.open("POST","/update?send="+(element.checked?id1:id0),true);xhr.send();}
function ClickToogleFA(element,value0,value1,id0,id1){var xhr=new XMLHttpRequest();xhr.open("POST","/update?send="+(element.className==value0?id1:id0),true);xhr.send();}
function printColor(element,ev){var color=ev.target.value;var r=parseInt(color.substr(1,2),16),g=parseInt(color.substr(3,2),16),b=parseInt(color.substr(5,2),16);var xhr=new XMLHttpRequest();xhr.open("POST","/update?send_color="+element.id+"&r="+r+"&g="+g+"&b="+b,true);xhr.send();}
function ValidRange(element){var xhr=new XMLHttpRequest();xhr.open("POST","/update?send_range="+element.id+"&value="+element.value,true);xhr.send();}
function Liste(element){var xhr=new XMLHttpRequest();xhr.open("POST","/update?send_list="+element.value+"&list="+element.id,true);xhr.send();}
function InputBoxValidate(element){var xhr=new XMLHttpRequest();xhr.open("POST","/update?send_textbox="+element.id+"&text="+encodeURIComponent(element.value),true);xhr.send();}
</script>
</body>
</html>
)rawliteral";
