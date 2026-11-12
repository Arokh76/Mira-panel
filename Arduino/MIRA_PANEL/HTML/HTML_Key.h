const char Page_Key[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%APP% - Configuration</title>
<style>
:root{--bg:%LIGHTCOLOR%;--panel:%DARKCOLOR%;--active:%ACTIVECOLOR%;--txt:%TEXTCOLOR%;--ink:#263238;--muted:#607d8b;--line:#d9e2e6;--soft:#f5f8f9;--ok:#4f7c68}
*{box-sizing:border-box}body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:var(--ink);min-height:100vh}
header{background:var(--panel);color:var(--txt);box-shadow:0 2px 12px rgba(0,0,0,.12)}.bar{max-width:900px;margin:auto;min-height:66px;padding:0 20px;display:flex;align-items:center;justify-content:space-between;gap:18px}.brand{font-size:22px;font-weight:800}.step{padding:8px 12px;border-radius:999px;background:rgba(255,255,255,.14);font-size:13px;font-weight:800}
main{max-width:760px;margin:34px auto;padding:0 18px 42px}.card{background:#fff;border-radius:22px;padding:30px;box-shadow:0 6px 24px rgba(0,0,0,.11)}.eyebrow{font-size:13px;font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:8px}h1{font-size:34px;margin:0 0 10px}.lead{margin:0 0 26px;color:#455a64;line-height:1.5}.field{margin:0 0 16px}label{display:block;font-weight:800;margin:0 0 7px}input[type=password],input[type=text]{width:100%%;min-height:48px;padding:11px 14px;border:1.8px solid #78909c;border-radius:12px;font-size:17px;color:var(--ink);background:#fff}input:focus{outline:none;border-color:var(--active);box-shadow:0 0 0 3px rgba(0,0,0,.05)}.showline{display:flex;align-items:center;gap:9px;margin-top:4px;color:var(--muted);font-size:14px}.showline input{width:18px;height:18px;accent-color:var(--active)}.info{margin:22px 0 0;padding:16px 18px;border:1px solid var(--line);border-radius:15px;background:var(--soft)}.info strong{display:block;margin-bottom:8px}.info ul{margin:0;padding-left:20px;color:#455a64;line-height:1.55}.actions{display:flex;justify-content:flex-end;margin-top:24px}.btn{border:0;border-radius:14px;padding:13px 22px;background:var(--active);color:#fff;font-size:16px;font-weight:800;cursor:pointer}.btn:active{transform:scale(.98)}.error{display:none;margin:0 0 16px;padding:12px 14px;border-radius:12px;background:#fff2f2;color:#8b3d3d;font-weight:700}.error.show{display:block}.foot{margin-top:16px;text-align:center;color:var(--muted);font-size:13px}
@media(max-width:620px){.card{padding:22px}.bar{align-items:flex-start;flex-direction:column;padding-top:14px;padding-bottom:12px}h1{font-size:29px}.actions .btn{width:100%%}}
</style>
</head>
<body>
<header><div class="bar"><div class="brand"><img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACMAAAAcCAYAAADr9QYhAAAGh0lEQVR4nLWXW2wcVxnH/9+ZWRxfQ4zj3EjVYFoQVEgolZAIUhMuURJ4aJGSqBIPREVVEaI8oUo8YOeJ54oKGvoAorxgP1BuQQgJ1+C6CaSUFBLHdoTj2LXj3fXuendn53Iufx521t712imX9pNGO0dz5ny/8//+38wsJicne4vr5Z8lic7VauEbC2+//RkAIOnjPQiSSkTw9NOXMmuF8o+jKC5UKrU3ZufnP4n19eAlNkUUJ6Xbt+8cey+A6iDA+Pi4X62Fv0hTOpIMgvAfqIVRlaQ1xjhrrSHJOEmKhULlE+8mEEkFAJcu/aorDOPLJGmt1dZakrRxoomgVgvTC3TO0RhjSVJrfW9lZe2RdCHv3QC5fPlK3/p69c8kaYzRzjk650iS1aDmEIZRpRnGOccNheJk5cbc3Mf/H6AGyOzsbF8QhFNprqSRyznbgCGiKF5PJ7jNCZtAYRgtzc+vPPi/AI2m8/P5fJ/W+rVGaZrzNJSpVAOHKIq2hXHO0RpjSDLReiafzx/6b4Aa86am/tmvtakrYtpBGjBBUDOI47i0E0zqoUbJpqfenDkEAKOj9wdqgIyPX91fq4V/20mRFs9UaxZhGJXvB8N6yTRJrlaC6d+/9vfBOtDotkCbirx5qFqt3XwnEGdTz1QDu6Nnmg9tHUuJ1UWSVyvx1elcrhcAhlNzbgWZnl44GNTC6fuVpk2ZoKYRxfeHsdayZhxXY8e3Kkb/heTLuXjy269M9koT0PDwsAKAhVzuYBgn76zIdjDbtXYKYa21RlvHde04H1peKTuOZY1+MSC/MRtMPvbCaA9JGR4f90lKoVB7IEn0je1ArLWW3AbONjwTGMUtUjdC1cPTji52QGAF6wYoWOUvrlpT6e06Vnr0zCsy8qo3cvw4RIQi+rlMxv+YtTZWSvlN5XNKKQUoX0S2uqz+I9IOQueciCCO4z+WyuVLnb5SNUtTsUTRAGsayBnlL61ak3t/9+fUiWNjY+fGKAASej8yxt7zPK/DOWchAEmjlFLW2hvFYvGbxhidArbkFYFrV0XEpScze3bvfiYX6p/s6lR+2UKXLFAwQF4Dy0b5q1lrXFfm8fNf+dLYz8+e9fb191yvxNEpa11WKeXRMlZK+Umi54pxfLK/v/8FrXWYqtNKQ0AB2KobAMBa10dSDXa978JCJf6pv0dlSpa6ZIGsAZYTwBrlSw6aXZ1PnD/90hiHH/P7e3quZ4uFLyaJLilPdYRhNLO0tPj5vd3dyxMTE4d3ygcAaitgU70oIu4amflU364L89nqr92AyqwZmqwFKg6ABWCYQc5p9O5+Qg387kUfwMG9e68tL6+erlSC709P3/zs0NDQXZJSKumEbIVpzq5koyytYaz1AeAogGESL9+89uW7heS3wQeUn3POEIA4gAYQIxnmnUZn51P2e9UfKgBHjhy+0tfX8+zRo0eXSXoiwroLUpbUM42xiFBtJW2EcxvmdgAwcfy4/c3kL8/+qxi8Xtnn+VA0lPrW6ABxkmEJGh3dz8hzlR8oAM/PsmN4mKqxRktsgSKJNgOrzbkbkBfr6snSuXPh7bu3TlubXMV+5cOjkUx6EwFYl5EytPN7vi5fW3v+Ww9LfBHAyMhI04Z3sIVsU6amQSuoiAOp5AuPrnN57oxIdBMPKp+7nJVOoP49KHDGZVB1mt39z6qvVr+Di+JexYgCgNiLZCfPCIRKZGPMLQTtWxBxHKWHTz9S6KgunZJuPYcjnscuZ9ENoAOAL4Ci7xI4+t538eStgT9dFAMAXehG2zOvKfztlFFo8UxrnBML0otEFjG/clJ690xxqOMAtLPQ4kEDNAIoKFj4MHYPgHzbDslWL4DwdzKwUrJDcQFIHQgid3hr8ZQMHPgDh7xBOOtApSAAdqHe+ta1mnejLrIxFAAiyrU9Z1pa6H4hYjE+7uOjh9+ijU7KAV3Cw57CgHMYBOqHEH1N93TXW7hlmSZMJfVGJwCy/sJgneU/+Lw8ccKA9LGv57pXzj8uH9QRHvIU9tFhP4hBetirNsrtRUqw2XtMEzZyKkUiACDpW1oo9Wb1PG+lHX5bhQxI33zo4ITS8Rn1gA3kI56HD0MwICUwyTlSSEouF5WVkkpzPqWUpDlCZLNrT8ZxcscYk9VaZ6212Vot/OvtxcWHWF9keyNvjfTPnre8dlpV4xl1L7mjXl87n15Tjc/UbD7/VBwny4nW+SRJ1rTW+TiOF1ZW8xf+DZ7DXkQ/J8ILAAAAAElFTkSuQmCC" alt="" aria-hidden="true" style="width:31px;height:25px;vertical-align:-6px;margin-right:1px">ira Panel</div><div class="step">Configuration · étape 1/2</div></div></header>
<main>
  <section class="card">
    <div class="eyebrow">Mira Panel</div>
    <h1>Créer la clé locale</h1>
    <p class="lead">Cette clé protège l'accès local et sera réutilisée par les fonctions HTTP et le point d'accès du panneau.</p>
    <div id="errorBox" class="error"></div>
    <form name="reg" onsubmit="return Verif()" action="/save?config=key" method="POST">
      <div class="field"><label for="key">Clé</label><input id="key" name="key" value="%KEY%" type="password" minlength="8" autocomplete="new-password" required></div>
      <div class="field"><label for="ver">Confirmer la clé</label><input id="ver" name="ver" type="password" minlength="8" autocomplete="new-password" required></div>
      <label class="showline"><input id="showKey" type="checkbox" onchange="ToggleKeys(this.checked)">Afficher les caractères</label>
      <div class="info"><strong>Cette clé sert à :</strong><ul><li>l'identification locale ;</li><li>l'authentification des requêtes HTTP ;</li><li>la protection du point d'accès Wi-Fi.</li></ul></div>
      <div class="actions"><button class="btn" type="submit">Continuer vers le Wi-Fi →</button></div>
    </form>
  </section>
  <div class="foot">Configuration locale de Mira Panel</div>
</main>
<script>
function ToggleKeys(show){document.getElementById('key').type=show?'text':'password';document.getElementById('ver').type=show?'text':'password';}
function ShowError(text){var b=document.getElementById('errorBox');b.textContent=text;b.classList.add('show');}
function Verif(){var ke=document.forms['reg']['key'],ve=document.forms['reg']['ver'];document.getElementById('errorBox').classList.remove('show');if(!ke.value||!ve.value){ShowError('Saisis et confirme la clé.');return false;}if(ke.value.length<8){ShowError('La clé doit contenir au moins 8 caractères.');ke.focus();return false;}if(ke.value!==ve.value){ShowError('Les deux clés ne correspondent pas.');ve.focus();return false;}return true;}
</script>
</body>
</html>
)rawliteral";
