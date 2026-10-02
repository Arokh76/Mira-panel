const char Page_Message[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Système</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:840px;margin:32px auto;padding:0 18px 42px}.card{background:#fff;border-radius:20px;padding:30px;box-shadow:0 5px 20px rgba(0,0,0,.10);text-align:center}.eyebrow{display:inline-block;font-size:13px;font-weight:800;text-transform:uppercase;letter-spacing:.08em;color:#607d8b;margin-bottom:8px}h1{font-size:32px;margin:0 0 18px}.message{font-size:19px;line-height:1.55;color:#37474f}.message form{margin-top:18px}.Bouton,.btn{border:0;border-radius:14px;padding:12px 22px;background:var(--active);color:#fff;font-weight:800;font-size:17px;cursor:pointer;text-decoration:none;width:auto;margin:8px}.Bouton:active,.btn:active{transform:scale(.98)}input[type=file]{max-width:100%%;padding:10px;background:#f5f8f9;border:1px solid var(--line);border-radius:12px}.back{margin-top:22px}.back a{color:#546e7a;text-decoration:none;font-weight:700}
.progress-panel{display:none;margin:24px auto 8px;max-width:560px;text-align:left}.progress-panel.show{display:block}.progress-head{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:8px;font-weight:800}.progress-track{height:16px;background:#e7eef1;border-radius:999px;overflow:hidden}.progress-bar{height:100%%;width:0;background:var(--active);transition:width .18s ease}.progress-status{font-size:14px;color:var(--muted);margin-top:9px;line-height:1.4}.upload-lock{opacity:.55;pointer-events:none}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}h1{font-size:27px}.card{padding:22px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div><nav class="nav"><a href="/">Accueil</a><a href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a class="active" href="/load?nav=system">Système</a></nav></div></header>
<main><section class="card"><span class="eyebrow">Mira Panel</span><h1>%TITRE%</h1><div class="message">%MESSAGE%</div>
<div id="progressPanel" class="progress-panel">
  <div class="progress-head"><span id="progressLabel">Mise à jour</span><span id="progressValue">0%%</span></div>
  <div class="progress-track"><div id="progressBar" class="progress-bar"></div></div>
  <div id="progressStatus" class="progress-status">Préparation…</div>
</div>
<div class="back"><a href="/load?nav=system">← Retour au système</a></div></section></main>
<script>
function MAJ(element){var xhr=new XMLHttpRequest();xhr.open("GET","/updatemodule",true);xhr.send();}

(function(){
  var panel=document.getElementById('progressPanel');
  var bar=document.getElementById('progressBar');
  var value=document.getElementById('progressValue');
  var status=document.getElementById('progressStatus');
  var label=document.getElementById('progressLabel');
  var message=document.getElementById('divmessage');

  function showProgress(percent,text,title){
    if(!panel)return;
    panel.classList.add('show');
    percent=Math.max(0,Math.min(100,Number(percent)||0));
    bar.style.width=percent+'%';
    value.textContent=percent+'%';
    if(text)status.textContent=text;
    if(title)label.textContent=title;
  }

  if(message){
    var watch=function(){
      var txt=message.textContent||'';
      var m=txt.match(/Progression\\s*:\\s*(\\d+)\\s*%/i);
      if(m)showProgress(parseInt(m[1],10),'Téléchargement et installation du firmware…','Mise à jour OTA');
      if(/Redémarrage/i.test(txt))showProgress(100,'Installation terminée. Mira Panel redémarre…','Mise à jour OTA');
      if(/impossible|erreur|échec|echec/i.test(txt)){
        panel.classList.add('show');
        status.textContent=txt;
      }
    };
    new MutationObserver(watch).observe(message,{childList:true,subtree:true,characterData:true});
    watch();
  }

  var form=document.querySelector('form[action="/doUpdateFile"]');
  if(form){
    form.addEventListener('submit',function(ev){
      ev.preventDefault();
      var file=form.querySelector('input[type="file"]');
      var button=form.querySelector('input[type="submit"],button[type="submit"]');
      if(!file || !file.files || !file.files.length){
        showProgress(0,'Choisis d’abord un fichier .bin.','Mise à jour manuelle');
        return;
      }

      if(button)button.classList.add('upload-lock');
      if(file)file.classList.add('upload-lock');
      showProgress(0,'Envoi du firmware vers Mira Panel…','Mise à jour manuelle');

      var xhr=new XMLHttpRequest();
      xhr.open('POST','/doUpdateFile',true);
      xhr.upload.onprogress=function(e){
        if(e.lengthComputable){
          var pct=Math.round((e.loaded/e.total)*100);
          showProgress(pct,pct<100?'Envoi et écriture du firmware…':'Firmware envoyé, finalisation…','Mise à jour manuelle');
        }
      };
      xhr.onload=function(){
        if(xhr.status>=200 && xhr.status<300){
          showProgress(100,'Mise à jour terminée. Mira Panel redémarre…','Mise à jour manuelle');
        }else{
          showProgress(100,'La mise à jour a échoué. Mira Panel reste sur le firmware actuel.','Erreur de mise à jour');
          if(button)button.classList.remove('upload-lock');
          if(file)file.classList.remove('upload-lock');
        }
      };
      xhr.onerror=function(){
        if(bar && parseInt(value.textContent,10)>=95){
          showProgress(100,'Connexion interrompue pendant le redémarrage de Mira Panel…','Mise à jour manuelle');
        }else{
          showProgress(parseInt(value.textContent,10)||0,'Connexion perdue avant la fin de la mise à jour.','Erreur de mise à jour');
          if(button)button.classList.remove('upload-lock');
          if(file)file.classList.remove('upload-lock');
        }
      };
      xhr.send(new FormData(form));
    });
  }
})();
</script>
</body>
</html>
)rawliteral";
