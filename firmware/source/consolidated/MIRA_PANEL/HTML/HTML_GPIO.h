#pragma once
// Local WT32-only GPIO test console. No Jeedom interaction.
const char Page_GPIO[] PROGMEM = R"mira_gpio(
<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mira Panel - GPIO WT32</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--muted:#5f7987}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:#24343d;font:15px Arial,sans-serif}
header{background:var(--panel);color:var(--txt);padding:18px 24px}
header .bar{max-width:1000px;margin:auto;display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between}
.brand{font-weight:800;font-size:22px}
nav{display:flex;gap:12px;flex-wrap:wrap}
nav a{color:var(--txt);text-decoration:none;font-weight:bold;padding:7px 10px;border-radius:9px}
nav a.active,nav a:hover{background:rgba(255,255,255,.14)}
main{max-width:1000px;margin:26px auto;padding:0 18px 40px}
h1{margin:0 0 7px;font-size:31px}p{line-height:1.48}
.intro{color:var(--muted);margin-bottom:20px}
.notice{padding:14px 16px;background:#fff6e7;border:1px solid #e9c990;border-radius:12px;margin-bottom:18px}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}
.card{background:white;border-radius:18px;padding:18px;box-shadow:0 4px 18px rgba(0,0,0,.10)}
.card h2{font-size:19px;margin:0 0 12px}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:9px 0}
select{min-width:185px;max-width:100%;padding:9px;border:1px solid #cbd9df;border-radius:9px;background:#fff}
button{padding:10px 13px;background:var(--active);color:white;border:0;border-radius:10px;font-weight:bold;cursor:pointer}
button:disabled{opacity:.45;cursor:default}
.state{font-weight:bold;padding:6px 10px;border-radius:8px;background:#edf4f5}
.state.high{background:#d8f5e5;color:#176b42}
.small{color:var(--muted);font-size:13px}
#message{min-height:24px;font-weight:bold;margin:12px 0}
@media(max-width:660px){.grid{grid-template-columns:1fr}h1{font-size:26px}}
</style></head>
<body><header><div class="bar"><div class="brand">Mira Panel</div>
<nav><a href="/">Accueil</a><a href="/load?nav=params">Paramètres</a>
<a href="/load?nav=system">Système</a><a class="active" href="/load?nav=gpio">GPIO</a></nav>
</div></header>
<main><h1>GPIO · WT32-SC01 Plus</h1>
<p class="intro">Pilotage local des 6 broches du connecteur EXT. Aucune liaison avec Jeedom.</p>
<div class="notice"><b>Attention :</b> signaux 3,3 V uniquement, jamais de 230 V ni de relais direct.
Toutes les broches sont désactivées par défaut. Les sorties redémarrent à OFF ; prévois une résistance
de rappel externe adaptée si ton montage exige un état garanti pendant le démarrage.</div>
<p id="message" role="status"></p><div class="grid" id="pins"></div>
<p class="small">Modes et inversion enregistrés en mémoire. Le niveau ON/OFF d'une sortie n'est pas mémorisé.
Actualisation automatique des entrées toutes les 2 secondes.</p></main>
<script>
(function(){
const modes=[["off","Désactivé"],["input","Entrée"],["pullup","Entrée + pull-up"],
["pulldown","Entrée + pull-down"],["output","Sortie numérique"]];
const root=document.getElementById('pins'),msg=document.getElementById('message');
let busy=false,ready=false;
function message(s,bad){msg.textContent=s;msg.style.color=bad?'#a33c3c':'#1e7051';}
async function api(url,body){
  const options={credentials:'same-origin',cache:'no-store'};
  if(body){options.method='POST';options.headers={'Content-Type':'application/x-www-form-urlencoded',
      'X-Mira-GPIO':'1'};options.body=new URLSearchParams(body);}
  const r=await fetch(url,options);
  if(!r.ok)throw new Error('HTTP '+r.status+' : '+(await r.text()));
  return r;
}
function build(p){
  const box=document.createElement('section');box.className='card';box.dataset.pin=p.gpio;
  box.innerHTML='<h2>'+p.connector+' · GPIO'+p.gpio+'</h2>'+
    '<div class="row"><label>Mode <select class="mode"></select></label></div>'+
    '<div class="row"><label><input class="invert" type="checkbox"> Inverser HIGH / LOW</label></div>'+
    '<div class="row"><span class="state"></span><button class="toggle" disabled>ON / OFF</button></div>';
  const select=box.querySelector('.mode'),inverse=box.querySelector('.invert'),
        toggle=box.querySelector('.toggle');
  modes.forEach(function(m){const op=document.createElement('option');op.value=m[0];op.textContent=m[1];select.appendChild(op);});
  async function configure(){
    if(select.value==='output' && p.mode!=='output' &&
        !confirm('Configurer GPIO'+p.gpio+' en sortie ? Elle sera initialisée à OFF.')) {
      select.value=p.mode;return;
    }
    try{busy=true;await api('/gpio/config',{gpio:String(p.gpio),mode:select.value,inverted:inverse.checked?'1':'0'});
      message('Configuration GPIO'+p.gpio+' enregistrée.');await refresh();
    }catch(e){message(e.message,true);}finally{busy=false;}
  }
  select.addEventListener('change',configure);
  inverse.addEventListener('change',configure);
  toggle.addEventListener('click',async function(){
    try{busy=true;await api('/gpio/set',{gpio:String(p.gpio),state:toggle.dataset.next});
      await refresh();message('GPIO'+p.gpio+' mis à jour.');
    }catch(e){message(e.message,true);}finally{busy=false;}
  });
  root.appendChild(box);
}
function paint(p){
  const box=root.querySelector('[data-pin="'+p.gpio+'"]');if(!box)return;
  const select=box.querySelector('.mode'),inv=box.querySelector('.invert'),
       badge=box.querySelector('.state'),toggle=box.querySelector('.toggle');
  if(document.activeElement!==select)select.value=p.mode;
  if(document.activeElement!==inv)inv.checked=p.inverted;
  const configured=p.mode!=='off',output=p.mode==='output',on=!!p.state;
  badge.textContent=!configured?'INACTIF':(on?'HIGH / ON':'LOW / OFF');
  badge.className='state'+(on?' high':'');
  toggle.disabled=!output;
  toggle.dataset.next=on?'0':'1';
  toggle.textContent=on?'Passer à OFF':'Passer à ON';
}
async function refresh(){
  if(busy && ready)return;
  try{
    const r=await api('/gpio/state');const data=await r.json();
    if(!ready){data.pins.forEach(build);ready=true;}
    data.pins.forEach(paint);
    if(!msg.textContent)message('GPIO disponibles.');
  }catch(e){message('Lecture GPIO impossible : '+e.message,true);}
}
refresh();setInterval(refresh,2000);
})();
</script></body></html>
)mira_gpio";
