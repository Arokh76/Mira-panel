from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
sys_h = root / 'HTML' / 'HTML_Sys.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    if label in ('manifest dual-source freshness check',
                 'firmware freshness and version verification') and (
            'var TARGETS_URL=' in text and 'var HARDWARE_PROFILE=' in text):
        # A newer board-aware OTA page was supplied by final-overrides.
        # Never overwrite its per-target download safety with WT32-only logic.
        print('Mira profile-aware OTA override preserved: ' + label)
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

old_manifest = '''  async function fetchManifest(){
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
'''

new_manifest = '''  async function fetchManifest(){
    var sources=[
      {url:VERSION_API+'&ts='+Date.now(),opt:{cache:'no-store',headers:{'Accept':'application/vnd.github.raw+json'}}},
      {url:VERSION_URL+'?ts='+Date.now(),opt:{cache:'no-store'}}
    ];
    var manifests=[];
    var errors=[];
    for(var i=0;i<sources.length;i++){
      try{
        var r=await fetch(sources[i].url,sources[i].opt);
        if(!r.ok)throw new Error('HTTP '+r.status);
        var j=await r.json();
        if(j.project!=='Mira Panel'||!j.version)throw new Error('manifest invalide');
        manifests.push(j);
      }catch(e){errors.push(e&&e.message?e.message:String(e));}
    }
    if(!manifests.length)throw new Error(errors.join(' / ')||'GitHub inaccessible');
    var best=manifests[0];
    for(var n=1;n<manifests.length;n++){
      if(compareVersion(String(manifests[n].version||''),String(best.version||''))>0)best=manifests[n];
    }
    return best;
  }
'''
replace_once(sys_h, old_manifest, new_manifest, 'manifest dual-source freshness check')

old_firmware = '''  async function fetchFirmware(){
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
'''

new_firmware = '''  async function firmwareContainsVersion(blob,version){
    var buf=new Uint8Array(await blob.arrayBuffer());
    var needle=[];
    for(var i=0;i<version.length;i++)needle.push(version.charCodeAt(i)&255);
    outer:for(var p=0;p<=buf.length-needle.length;p++){
      for(var n=0;n<needle.length;n++)if(buf[p+n]!==needle[n])continue outer;
      return true;
    }
    return false;
  }

  async function fetchFirmware(){
    var candidates=[
      {url:BIN_API+'&ts='+Date.now(),opt:{cache:'no-store',headers:{'Accept':'application/vnd.github.raw+json'}},label:'Téléchargement via l’API GitHub…'},
      {url:remoteBin,opt:{cache:'no-store'},label:'Nouvel essai depuis GitHub…'}
    ];
    var lastError='';
    for(var i=0;i<candidates.length;i++){
      try{
        setProgress(0,candidates[i].label,'Mise à jour '+remoteVer);
        var requestUrl=candidates[i].url+(candidates[i].url.indexOf('?')>=0?'&':'?')+'ts='+Date.now();
        var r=await fetch(requestUrl,candidates[i].opt);
        if(!r.ok)throw new Error('HTTP '+r.status);
        var total=parseInt(r.headers.get('content-length')||'0',10);
        var blob;
        if(!r.body||!r.body.getReader){
          blob=await r.blob();
        }else{
          var reader=r.body.getReader(),chunks=[],loaded=0;
          while(true){
            var part=await reader.read();
            if(part.done)break;
            chunks.push(part.value);loaded+=part.value.length;
            var pct=total?Math.min(45,Math.round((loaded/total)*45)):15;
            setProgress(pct,'Téléchargement depuis GitHub…','Mise à jour '+remoteVer);
          }
          blob=new Blob(chunks,{type:'application/octet-stream'});
        }
        if(blob.size<500000)throw new Error('firmware trop petit');
        if(!(await firmwareContainsVersion(blob,remoteVer)))throw new Error('firmware GitHub pas encore synchronisé sur '+remoteVer);
        setProgress(45,'Firmware '+remoteVer+' vérifié. Préparation de l’envoi local…','Mise à jour '+remoteVer);
        return blob;
      }catch(e){lastError=e&&e.message?e.message:String(e);}
    }
    throw new Error(lastError||'Téléchargement impossible');
  }
'''
replace_once(sys_h, old_firmware, new_firmware, 'firmware freshness and version verification')

print('Mira Panel OTA browser sync hardening applied')
