const char Page_ModificationReseau[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Modifier un réseau</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--danger:#9b4a4a;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink)}
header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px rgba(0,0,0,.12)}
.bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;align-items:center;justify-content:space-between;gap:20px}.brand{font-size:22px;font-weight:800;white-space:nowrap}.nav{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end}.nav a{color:var(--txt);text-decoration:none;font-weight:700;padding:10px 12px;border-radius:12px}.nav a:hover,.nav a.active{background:rgba(255,255,255,.14)}
main{max-width:940px;margin:28px auto;padding:0 18px 42px}.hero{margin-bottom:16px}.hero h1{font-size:36px;margin:0 0 7px}.hero p{margin:0;color:#455a64;line-height:1.45}.card{background:#fff;border-radius:20px;padding:24px;box-shadow:0 5px 20px rgba(0,0,0,.10)}.card h2{margin:0 0 6px;font-size:22px}.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.field{margin:0 0 16px}label{display:block;font-weight:700;margin:0 0 7px}input[type=text],input[type=password]{display:block;width:100%% !important;min-height:46px;padding:11px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:16px;background:#fff;color:var(--ink)}input:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.btn{border:0;border-radius:14px;padding:12px 18px;background:var(--active);color:#fff;font-weight:800;font-size:16px;cursor:pointer;text-decoration:none}.btn.secondary{background:#607d8b}.btn.danger{background:var(--danger)}.dangerbox{border-left:4px solid var(--danger);padding-left:20px}
@media(max-width:700px){.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}.nav{justify-content:flex-start}.hero h1{font-size:30px}}
select{display:block;width:100%;min-height:46px;padding:11px;border:1.8px solid #78909c;border-radius:12px;font-size:16px;background:#fff;color:#263238}.footnote{margin-top:12px;color:#607d8b;font-size:13px;line-height:1.45}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}@media(max-width:700px){.grid{grid-template-columns:1fr}}#mira-manual-fields{margin-top:6px}
</style>
</head>
<body>
<header><div class="bar"><div class="brand"><img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACMAAAAcCAYAAADr9QYhAAAGh0lEQVR4nLWXW2wcVxnH/9+ZWRxfQ4zj3EjVYFoQVEgolZAIUhMuURJ4aJGSqBIPREVVEaI8oUo8YOeJ54oKGvoAorxgP1BuQQgJ1+C6CaSUFBLHdoTj2LXj3fXuendn53Iufx521t712imX9pNGO0dz5ny/8//+38wsJicne4vr5Z8lic7VauEbC2+//RkAIOnjPQiSSkTw9NOXMmuF8o+jKC5UKrU3ZufnP4n19eAlNkUUJ6Xbt+8cey+A6iDA+Pi4X62Fv0hTOpIMgvAfqIVRlaQ1xjhrrSHJOEmKhULlE+8mEEkFAJcu/aorDOPLJGmt1dZakrRxoomgVgvTC3TO0RhjSVJrfW9lZe2RdCHv3QC5fPlK3/p69c8kaYzRzjk650iS1aDmEIZRpRnGOccNheJk5cbc3Mf/H6AGyOzsbF8QhFNprqSRyznbgCGiKF5PJ7jNCZtAYRgtzc+vPPi/AI2m8/P5fJ/W+rVGaZrzNJSpVAOHKIq2hXHO0RpjSDLReiafzx/6b4Aa86am/tmvtakrYtpBGjBBUDOI47i0E0zqoUbJpqfenDkEAKOj9wdqgIyPX91fq4V/20mRFs9UaxZhGJXvB8N6yTRJrlaC6d+/9vfBOtDotkCbirx5qFqt3XwnEGdTz1QDu6Nnmg9tHUuJ1UWSVyvx1elcrhcAhlNzbgWZnl44GNTC6fuVpk2ZoKYRxfeHsdayZhxXY8e3Kkb/heTLuXjy269M9koT0PDwsAKAhVzuYBgn76zIdjDbtXYKYa21RlvHde04H1peKTuOZY1+MSC/MRtMPvbCaA9JGR4f90lKoVB7IEn0je1ArLWW3AbONjwTGMUtUjdC1cPTji52QGAF6wYoWOUvrlpT6e06Vnr0zCsy8qo3cvw4RIQi+rlMxv+YtTZWSvlN5XNKKQUoX0S2uqz+I9IOQueciCCO4z+WyuVLnb5SNUtTsUTRAGsayBnlL61ak3t/9+fUiWNjY+fGKAASej8yxt7zPK/DOWchAEmjlFLW2hvFYvGbxhidArbkFYFrV0XEpScze3bvfiYX6p/s6lR+2UKXLFAwQF4Dy0b5q1lrXFfm8fNf+dLYz8+e9fb191yvxNEpa11WKeXRMlZK+Umi54pxfLK/v/8FrXWYqtNKQ0AB2KobAMBa10dSDXa978JCJf6pv0dlSpa6ZIGsAZYTwBrlSw6aXZ1PnD/90hiHH/P7e3quZ4uFLyaJLilPdYRhNLO0tPj5vd3dyxMTE4d3ygcAaitgU70oIu4amflU364L89nqr92AyqwZmqwFKg6ABWCYQc5p9O5+Qg387kUfwMG9e68tL6+erlSC709P3/zs0NDQXZJSKumEbIVpzq5koyytYaz1AeAogGESL9+89uW7heS3wQeUn3POEIA4gAYQIxnmnUZn51P2e9UfKgBHjhy+0tfX8+zRo0eXSXoiwroLUpbUM42xiFBtJW2EcxvmdgAwcfy4/c3kL8/+qxi8Xtnn+VA0lPrW6ABxkmEJGh3dz8hzlR8oAM/PsmN4mKqxRktsgSKJNgOrzbkbkBfr6snSuXPh7bu3TlubXMV+5cOjkUx6EwFYl5EytPN7vi5fW3v+Ww9LfBHAyMhI04Z3sIVsU6amQSuoiAOp5AuPrnN57oxIdBMPKp+7nJVOoP49KHDGZVB1mt39z6qvVr+Di+JexYgCgNiLZCfPCIRKZGPMLQTtWxBxHKWHTz9S6KgunZJuPYcjnscuZ9ENoAOAL4Ci7xI4+t538eStgT9dFAMAXehG2zOvKfztlFFo8UxrnBML0otEFjG/clJ690xxqOMAtLPQ4kEDNAIoKFj4MHYPgHzbDslWL4DwdzKwUrJDcQFIHQgid3hr8ZQMHPgDh7xBOOtApSAAdqHe+ta1mnejLrIxFAAiyrU9Z1pa6H4hYjE+7uOjh9+ijU7KAV3Cw57CgHMYBOqHEH1N93TXW7hlmSZMJfVGJwCy/sJgneU/+Lw8ccKA9LGv57pXzj8uH9QRHvIU9tFhP4hBetirNsrtRUqw2XtMEzZyKkUiACDpW1oo9Wb1PG+lHX5bhQxI33zo4ITS8Rn1gA3kI56HD0MwICUwyTlSSEouF5WVkkpzPqWUpDlCZLNrT8ZxcscYk9VaZ6212Vot/OvtxcWHWF9keyNvjfTPnre8dlpV4xl1L7mjXl87n15Tjc/UbD7/VBwny4nW+SRJ1rTW+TiOF1ZW8xf+DZ7DXkQ/J8ILAAAAAElFTkSuQmCC" alt="" aria-hidden="true" style="width:31px;height:25px;vertical-align:-6px;margin-right:1px">ira Panel</div><nav class="nav"><a href="/">Accueil</a><a class="active" href="/load?nav=wifi">Réseau</a><a href="/load?nav=mqtt">MQTT</a><a href="/load?nav=params">Paramètres</a><a href="/load?nav=system">Système</a></nav></div></header>
<main><div class="hero"><h1>Modifier le réseau</h1><p>Met à jour les paramètres de cette connexion Wi-Fi.</p></div><section class="card"><form action="/save?update=wifi" onsubmit="return Verif();" method="POST"><div class="field"><label>Nom du réseau</label><input id="ssid" name="ssid" value="%SSIDUPDATE%" type="text"></div><div class="field"><label>Mot de passe</label><input id="pass" name="pass" value="%PASSUPDATE%" type="password"></div>
<div class="field"><label>Configuration IPv4</label>
<select id="ip_mode" name="ip_mode" data-current="%IPMODEUPDATE%">
  <option value="auto">Automatique (DHCP)</option>
  <option value="fixed_auto">IP fixe, masque / passerelle / DNS automatiques</option>
  <option value="manual">Configuration manuelle avancée</option>
</select></div>
<div id="mira-ip-row" class="field"><label>Adresse IPv4 souhaitée</label>
<input id="ip" name="ip" value="%IPUPDATE%" type="text" inputmode="decimal" placeholder="192.168.1.45"></div>
<div id="mira-manual-fields" class="grid">
<div class="field"><label>Masque de sous-réseau</label><input name="mask" id="mask" type="text" value="%MASKUPDATE%" placeholder="255.255.255.0"></div>
<div class="field"><label>Passerelle</label><input name="gateway" id="gateway" type="text" value="%GATEWAYUPDATE%" placeholder="192.168.1.1"></div>
<div class="field"><label>DNS principal</label><input name="dns1" id="dns1" type="text" value="%DNS1UPDATE%" placeholder="192.168.1.1"></div>
<div class="field"><label>DNS secondaire (facultatif)</label><input name="dns2" id="dns2" type="text" value="%DNS2UPDATE%" placeholder="1.1.1.1"></div>
</div>
<p class="footnote">En mode « IP fixe, autres automatiques », le panneau récupère les paramètres du routeur par DHCP puis applique l’adresse demandée. Choisis une IP libre, idéalement réservée dans ton routeur.</p><div class="actions"><button class="btn" type="submit">Enregistrer</button><a class="btn secondary" href="/load?nav=listnetwork">Retour</a></div></form></section><section class="card dangerbox" style="margin-top:16px"><h2>Supprimer ce réseau</h2><p class="hint">Le panneau oubliera cette connexion. Les autres réseaux enregistrés ne sont pas modifiés.</p><div class="actions"><button class="btn danger" onclick="if(confirm('Supprimer ce réseau ?'))window.location.href='/load?nav=removenetwork'">Supprimer</button></div></section></main>

<script>
(function(){
  var select=document.getElementById("ip_mode");
  var ipRow=document.getElementById("mira-ip-row");
  var manual=document.getElementById("mira-manual-fields");
  if(!select)return;
  var current=select.getAttribute("data-current")||"auto";
  select.value=["auto","fixed_auto","manual"].indexOf(current)>=0?current:"auto";
  function render(){
    ipRow.style.display=select.value==="auto"?"none":"block";
    manual.style.display=select.value==="manual"?"grid":"none";
  }
  select.addEventListener("change",render);
  render();
})();
function validIPv4(value) {
  var chunks=value.trim().split(".");
  return chunks.length===4&&chunks.every(function(part){
    return /^\d{1,3}$/.test(part)&&Number(part)>=0&&Number(part)<=255;
  });
}
function Verif(){
  var mode=document.getElementById("ip_mode").value;
  var ip=document.getElementById("ip");
  if(mode!=="auto"&&!validIPv4(ip.value)){alert("Adresse IPv4 invalide");ip.focus();return false;}
  if(mode==="manual"){
    var fields=["mask","gateway","dns1","dns2"];
    for(var i=0;i<fields.length;i++){
      var input=document.getElementById(fields[i]);
      if((i<3||input.value.trim())&&!validIPv4(input.value)){
        alert("Renseigne une adresse IPv4 valide pour "+fields[i]);input.focus();return false;
      }
    }
  }
  return confirm("Enregistrer ce réseau ? Attention, un changement d'adresse peut interrompre la connexion.");
}
</script>
</body>
</html>
)rawliteral";
