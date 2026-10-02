from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
sys_h = root / "HTML" / "HTML_Sys.h"


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 match in {path}, got {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(ino, '"0.4.7"', '"0.4.8"', "firmware version")
replace_once(ino, '[MIRA] Firmware 0.4.7 initialise', '[MIRA] Firmware 0.4.8 initialise', "startup log")

replace_once(
    sys_h,
    '      <p class="hint">La vérification et le téléchargement sont effectués par ton navigateur. L’ESP n’a plus besoin d’ouvrir une connexion HTTPS vers GitHub.</p>\n',
    '',
    "remove technical update hint",
)

replace_once(
    sys_h,
    ".progress{display:none;margin-top:16px}.progress.show{display:block}",
    ".progress{display:none;margin-top:16px;padding:16px 18px;border:1px solid var(--line);border-radius:14px;background:var(--soft)}.progress.show{display:block}",
    "progress panel styling",
)

replace_once(
    sys_h,
    "    progress.classList.add('show');\n    fill.style.width=pct+'%%';\n    value.textContent=pct+'%%';\n    progressText.textContent=(title?title+' · ':'')+(text||'');",
    "    progress.classList.add('show');\n    fill.style.width=pct+'%%';\n    value.textContent=pct+'%%';\n    progressText.textContent=(title?title+' · ':'')+(text||'');\n    if(progress.scrollIntoView)progress.scrollIntoView({behavior:'smooth',block:'center'});",
    "progress visibility",
)

replace_once(
    sys_h,
    "          var base=startPct||0,span=100-base;\n          var pct=base+Math.round((e.loaded/e.total)*span);\n          setProgress(pct,pct<100?'Envoi du firmware vers Mira Panel…':'Finalisation du firmware…','Installation locale');",
    "          var base=startPct||0,span=95-base;\n          var pct=base+Math.round((e.loaded/e.total)*span);\n          setProgress(pct,pct<95?'Envoi du firmware vers Mira Panel…':'Firmware envoyé, écriture finale…','Installation locale');",
    "reserve final flash progress",
)

old_manual = """  form.addEventListener('submit',function(ev){
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
  });"""

new_manual = """  form.addEventListener('submit',function(ev){
    ev.preventDefault();
    if(!file.files||!file.files.length){setProgress(0,'Choisis d’abord un fichier .bin.','Mise à jour manuelle');return;}
    var selectedFile=file.files[0];
    file.disabled=true;manualBtn.disabled=true;checkBtn.disabled=true;installBtn.disabled=true;
    manualBtn.textContent='Mise à jour en cours…';
    setProgress(1,'Préparation du firmware…','Mise à jour manuelle');

    // Laisse au navigateur le temps d'afficher la barre avant de saturer la liaison locale.
    requestAnimationFrame(function(){
      setTimeout(function(){
        uploadBlob(selectedFile,selectedFile.name,1).then(function(){
          setProgress(100,'Mise à jour terminée. Mira Panel redémarre…','Mise à jour manuelle');
          setTimeout(function(){window.location.href='/load?nav=system';},10000);
        }).catch(function(e){
          setProgress(parseInt(value.textContent,10)||0,'Échec : '+(e&&e.message?e.message:'erreur inconnue'),'Mise à jour manuelle');
          file.disabled=false;manualBtn.disabled=false;checkBtn.disabled=false;installBtn.disabled=false;
          manualBtn.textContent='Installer le fichier .bin';
        });
      },120);
    });
  });"""
replace_once(sys_h, old_manual, new_manual, "manual upload UX")

print("Mira Panel 0.4.8 post-processing applied")
