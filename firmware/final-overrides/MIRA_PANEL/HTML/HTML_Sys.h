// Human-readable board name on the System page.
// NOTE: This follows the current build-target selection. The planned
// universal OTA firmware will replace it with actual runtime board detection.
#if defined(MIRA_TARGET_WAVESHARE_43C)
  #define MIRA_SYS_PLATFORM_LABEL "Waveshare ESP32-S3 Touch LCD 4.3C"
#else
  #define MIRA_SYS_PLATFORM_LABEL "WT32-SC01 Plus"
#endif

const char Page_Sys[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Système</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--ok:#2e7d5b;--warn:#a86f1d;--danger:#a34d4d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64;line-height:1.45}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10)}.card.full{grid-column:1/-1}.card h2{margin:0 0 6px;font-size:22px}.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.kv{display:flex;justify-content:space-between;gap:20px;padding:11px 0;border-bottom:1px solid var(--line)}.kv:last-child{border-bottom:0}.kv span:first-child{color:var(--muted)}.kv strong{text-align:right}.buttons{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.btn{border:0;border-radius:14px;padding:12px 18px;background:var(--active);color:#fff;font-weight:800;font-size:16px;cursor:pointer;text-decoration:none;width:auto}.btn.secondary{background:#607d8b}.btn.danger{background:var(--danger)}.btn:disabled{opacity:.55;cursor:default}.btn:active:not(:disabled){transform:scale(.98)}
.update-status{margin-top:18px;padding:16px 18px;border-radius:14px;background:var(--soft);border:1px solid var(--line);line-height:1.45}.update-status.ok{border-color:#b7d9ca;background:#f1faf6}.update-status.available{border-color:#b8d7e2;background:#f2f9fb}.update-status.error{border-color:#e3c2c2;background:#fff7f7}.status-title{font-weight:800;margin-bottom:4px}.status-detail{font-size:14px;color:var(--muted)}.install-row{display:none;margin-top:14px}.install-row.show{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.version-pill{display:inline-block;padding:7px 11px;border-radius:999px;background:#e8f1f4;font-weight:800}.manual{margin-top:22px;padding-top:22px;border-top:1px solid var(--line)}.manual h3{margin:0 0 6px;font-size:18px}input[type=file]{max-width:100%%;padding:10px;background:var(--soft);border:1px solid var(--line);border-radius:12px}.upload-row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.progress{display:none;margin-top:16px}.progress.show{display:block}.progress-head{display:flex;justify-content:space-between;gap:12px;font-weight:800;margin-bottom:8px}.track{height:14px;background:#e6edf0;border-radius:999px;overflow:hidden}.fill{width:0;height:100%%;background:var(--active);transition:width .15s ease}.progress-text{font-size:13px;color:var(--muted);margin-top:8px}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.grid{grid-template-columns:1fr}.card.full{grid-column:auto}.hero h1{font-size:30px}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand">%APP%</div><nav class="nav"><a href="/">Accueil</a><a href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a class="active" href="/load?nav=system">Système</a></nav></div></header>
<main>
  <div class="hero"><h1>Système</h1><p>Maintenance, informations et mises à jour de Mira Panel.</p></div>
  <div class="grid">
    <section class="card"><h2>Informations</h2><p class="hint">État général du panneau.</p><div class="kv"><span>Firmware</span><strong>%VERSION%</strong></div><div class="kv"><span>Adresse IP</span><strong>%LOCALIP%</strong></div><div class="kv"><span>Plateforme</span><strong>)rawliteral"
MIRA_SYS_PLATFORM_LABEL
R"rawliteral(</strong></div></section>
    <section class="card"><h2>Maintenance</h2><p class="hint">Actions locales sur le panneau.</p><div class="buttons"><button class="btn secondary" onclick="window.location.href='/load?nav=reboot'">Redémarrer</button><button class="btn" onclick="window.location.href='/load?nav=rst'">Gestion des données</button></div></section>

    <section class="card full">
      <h2>Mise à jour</h2>
      <p class="hint">La vérification et le téléchargement sont effectués par ton navigateur. L’ESP n’a plus besoin d’ouvrir une connexion HTTPS vers GitHub.</p>
      <div class="buttons"><button id="checkUpdate" class="btn" type="button">Vérifier les mises à jour</button></div>

      <div id="updateStatus" class="update-status">
        <div id="statusTitle" class="status-title">Version installée : %VERSION%</div>
        <div id="statusDetail" class="status-detail">Aucune vérification effectuée.</div>
      </div>

      <div id="installRow" class="install-row">
        <span id="remoteVersion" class="version-pill"></span>
        <button id="installUpdate" class="btn" type="button">Installer la mise à jour</button>
      </div>

      <div class="manual">
        <h3>Mise à jour manuelle</h3>
        <p class="hint">Utilise un fichier <b>.bin</b> uniquement si la mise à jour GitHub n'est pas disponible.</p>
        <form id="manualUpdate" action="/doUpdateFile" method="POST" enctype="multipart/form-data">
          <div class="upload-row"><input id="firmwareFile" type="file" name="update" accept=".bin"><button id="manualButton" class="btn secondary" type="submit">Installer le fichier .bin</button></div>
        </form>
        <div id="uploadProgress" class="progress">
          <div class="progress-head"><span>Mise à jour manuelle</span><span id="progressValue">0%%</span></div>
          <div class="track"><div id="progressFill" class="fill"></div></div>
          <div id="progressText" class="progress-text">Préparation…</div>
        </div>
      </div>
    </section>
  </div>
</main>
<script>
(function(){
  var VERSION_URL='https://raw.githubusercontent.com/Arokh76/Mira-panel/main/firmware/version.json';
  var VERSION_API='https://api.github.com/repos/Arokh76/Mira-panel/contents/firmware/version.json?ref=main';
  var BIN_API='https://api.github.com/repos/Arokh76/Mira-panel/contents/firmware/Mira_Panel.bin?ref=main';
  var CURRENT='%VERSION%';
  var remoteBin='';
  var remoteVer='';

  var checkBtn=document.getElementById('checkUpdate');
  var statusBox=document.getElementById('updateStatus');
  var statusTitle=document.getElementById('statusTitle');
  var statusDetail=document.getElementById('statusDetail');
  var installRow=document.getElementById('installRow');
  var remoteVersion=document.getElementById('remoteVersion');
  var installBtn=document.getElementById('installUpdate');
  var form=document.getElementById('manualUpdate');
  var file=document.getElementById('firmwareFile');
  var manualBtn=document.getElementById('manualButton');
  var progress=document.getElementById('uploadProgress');
  var fill=document.getElementById('progressFill');
  var value=document.getElementById('progressValue');
  var progressText=document.getElementById('progressText');

  function setStatus(kind,title,detail){
    statusBox.className='update-status'+(kind?' '+kind:'');
    statusTitle.textContent=title||'';
    statusDetail.textContent=detail||'';
  }

  function versionParts(v){return String(v||'').split('.').map(function(n){return parseInt(n,10)||0;});}
  function compareVersion(a,b){
    var aa=versionParts(a),bb=versionParts(b),n=Math.max(aa.length,bb.length);
    for(var i=0;i<n;i++){var x=aa[i]||0,y=bb[i]||0;if(x>y)return 1;if(x<y)return -1;}
    return 0;
  }

  function setProgress(pct,text,title){
    pct=Math.max(0,Math.min(100,Number(pct)||0));
    progress.classList.add('show');
    fill.style.width=pct+'%%';
    value.textContent=pct+'%%';
    progressText.textContent=(title?title+' · ':'')+(text||'');
  }

  async function fetchManifest(){
    var urls=[VERSION_URL+'?ts='+Date.now(),VERSION_API+'&ts='+Date.now()];
    var lastError='';
    for(var i=0;i<urls.length;i++){
      try{
        var opt={cache:'no-store'};
        if(i===1)opt.headers={'Accept':'application/vnd.github.raw+json'};
        var r=await fetch(urls[i],opt);
        if(!r.ok)throw new Error('HTTP '+r.status);
        var j=await r.json();
        if(j.project!=='Mira Panel'||!j.version)throw new Error('manifest invalide');
        return j;
      }catch(e){lastError=e&&e.message?e.message:String(e);}
    }
    throw new Error(lastError||'GitHub inaccessible');
  }

  checkBtn.addEventListener('click',async function(){
    checkBtn.disabled=true;
    checkBtn.textContent='Vérification…';
    installRow.classList.remove('show');
    remoteBin='';remoteVer='';
    setStatus('','Recherche d’une mise à jour…','Vérification effectuée par ton navigateur, pas par l’ESP.');
    try{
      var data=await fetchManifest();
      remoteVer=String(data.version||'');
      remoteBin=String(data.bin||'https://raw.githubusercontent.com/Arokh76/Mira-panel/main/firmware/Mira_Panel.bin');
      if(remoteBin.indexOf('https://raw.githubusercontent.com/Arokh76/Mira-panel/')!==0){
        throw new Error('adresse du firmware inattendue');
      }
      if(compareVersion(remoteVer,CURRENT)>0){
        setStatus('available','Nouvelle version disponible','Version installée : '+CURRENT+' · Version disponible : '+remoteVer);
        remoteVersion.textContent='Mira Panel '+remoteVer;
        installRow.classList.add('show');
      }else{
        setStatus('ok','Mira Panel est à jour','Version actuelle : '+CURRENT);
      }
      checkBtn.textContent='Vérifier à nouveau';
    }catch(e){
      setStatus('error','Impossible de lire GitHub depuis le navigateur',e&&e.message?e.message:'Erreur réseau');
      checkBtn.textContent='Réessayer';
    }
    checkBtn.disabled=false;
  });

  async function fetchFirmware(){
    var candidates=[{url:remoteBin,opt:{cache:'no-store'}},{url:BIN_API+'&ts='+Date.now(),opt:{cache:'no-store',headers:{'Accept':'application/vnd.github.raw+json'}}}];
    var lastError='';
    for(var i=0;i<candidates.length;i++){
      try{
        setProgress(0,i===0?'Téléchargement du firmware depuis GitHub…':'Nouvel essai via l’API GitHub…','Mise à jour '+remoteVer);
        var r=await fetch(candidates[i].url+(candidates[i].url.indexOf('?')>=0?'&':'?')+'ts='+Date.now(),candidates[i].opt);
        if(!r.ok)throw new Error('HTTP '+r.status);
        var total=parseInt(r.headers.get('content-length')||'0',10);
        if(!r.body||!r.body.getReader){
          var blob=await r.blob();
          if(blob.size<500000)throw new Error('firmware trop petit');
          setProgress(45,'Firmware téléchargé. Préparation de l’envoi local…','Mise à jour '+remoteVer);
          return blob;
        }
        var reader=r.body.getReader(),chunks=[],loaded=0;
        while(true){
          var part=await reader.read();
          if(part.done)break;
          chunks.push(part.value);loaded+=part.value.length;
          var pct=total?Math.min(45,Math.round((loaded/total)*45)):15;
          setProgress(pct,'Téléchargement depuis GitHub…','Mise à jour '+remoteVer);
        }
        var b=new Blob(chunks,{type:'application/octet-stream'});
        if(b.size<500000)throw new Error('firmware trop petit');
        setProgress(45,'Firmware téléchargé. Préparation de l’envoi local…','Mise à jour '+remoteVer);
        return b;
      }catch(e){lastError=e&&e.message?e.message:String(e);}
    }
    throw new Error(lastError||'Téléchargement impossible');
  }

  function uploadBlob(blob,fileName,startPct){
    return new Promise(function(resolve,reject){
      var fd=new FormData();
      fd.append('update',blob,fileName||'Mira_Panel.bin');
      var xhr=new XMLHttpRequest();
      xhr.open('POST','/doUpdateFile',true);
      xhr.upload.onprogress=function(e){
        if(e.lengthComputable){
          var base=startPct||0,span=100-base;
          var pct=base+Math.round((e.loaded/e.total)*span);
          setProgress(pct,pct<100?'Envoi du firmware vers Mira Panel…':'Finalisation du firmware…','Installation locale');
        }
      };
      xhr.onload=function(){if(xhr.status>=200&&xhr.status<300)resolve();else reject(new Error('ESP HTTP '+xhr.status));};
      xhr.onerror=function(){var pct=parseInt(value.textContent,10)||0;if(pct>=95)resolve();else reject(new Error('connexion locale interrompue'));};
      xhr.send(fd);
    });
  }

  installBtn.addEventListener('click',async function(){
    if(!remoteBin||!remoteVer)return;
    if(!confirm('Télécharger Mira Panel '+remoteVer+' depuis GitHub puis l’installer ?'))return;
    checkBtn.disabled=true;installBtn.disabled=true;manualBtn.disabled=true;file.disabled=true;
    try{
      var blob=await fetchFirmware();
      await uploadBlob(blob,'Mira_Panel_'+remoteVer+'.bin',45);
      setProgress(100,'Mise à jour terminée. Mira Panel redémarre…','Installation '+remoteVer);
      setTimeout(function(){window.location.href='/load?nav=system';},10000);
    }catch(e){
      setProgress(parseInt(value.textContent,10)||0,'Échec : '+(e&&e.message?e.message:'erreur inconnue'),'Mise à jour '+remoteVer);
      checkBtn.disabled=false;installBtn.disabled=false;manualBtn.disabled=false;file.disabled=false;
    }
  });

  form.addEventListener('submit',function(ev){
    ev.preventDefault();
    if(!file.files||!file.files.length){setProgress(0,'Choisis d’abord un fichier .bin.','Mise à jour manuelle');return;}
    file.disabled=true;manualBtn.disabled=true;checkBtn.disabled=true;installBtn.disabled=true;
    setProgress(0,'Envoi du firmware vers Mira Panel…','Mise à jour manuelle');
    uploadBlob(file.files[0],file.files[0].name,0).then(function(){
      setProgress(100,'Mise à jour terminée. Mira Panel redémarre…','Mise à jour manuelle');
      setTimeout(function(){window.location.href='/load?nav=system';},10000);
    }).catch(function(e){
      setProgress(parseInt(value.textContent,10)||0,'Échec : '+(e&&e.message?e.message:'erreur inconnue'),'Mise à jour manuelle');
      file.disabled=false;manualBtn.disabled=false;checkBtn.disabled=false;installBtn.disabled=false;
    });
  });
})();
</script>
</body>
</html>
)rawliteral";
