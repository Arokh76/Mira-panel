/* This file is part of Jeedom.
*
* Jeedom is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Jeedom is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Jeedom. If not, see <http://www.gnu.org/licenses/>.
*/

var oBtSup = [null,null];
var nbr = 0;
var MiraDisplayInfo = {profile:'legacy', width:320, height:480, contentWidth:320, contentHeight:434, navHeight:46, logicalWidth:1000, logicalHeight:1000, freeLayout:false};
var MiraDisplayInfoLoaded = false;
var MiraDesignerPage = 'accueil';
var MiraDesignerSelected = null;
var MiraDesignerGridEnabled = true;
var MiraDesignerScale = 1;
var MiraDesignerNumericDirty = false; // Saisie manuelle en cours, ne jamais réécrire les champs


$("#bt_installMiraPanel").on('click', function (event) {
    event.preventDefault();
    var installerWindow = window.open('https://arokh76.github.io/Mira-panel/', '_blank', 'noopener,noreferrer');
    if (installerWindow) {
        installerWindow.opener = null;
    }
});

$("#bt_addmiralvgl").on('click', function (event) {
    var r = document.querySelector(':root');
  	var rs = getComputedStyle(r);
  	var button = document.getElementById('miraboutontester');
  	button.style.backgroundColor = rs.getPropertyValue('--al-warning-color');
  
  	document.getElementById('miraip').disabled = false;
    document.getElementById('miramdp').disabled = false;
    $("#miraboutontester").show();
  
    $("#miraemplacement").hide();
    $("#mirastylezone").hide();
    $("#miralayoutzone").hide();
    $("#miradesignerbar").hide();
    $("#miradesigner").hide();
    $("#miraClassicEditor").show();
    $("#PopupDiv").removeClass("designer-open");
    $("#divmiraname").hide();
    $("#divmiramin").hide();
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#divmiraunit").hide();
    $("#miraboutonenvoyer").hide();
    $("#mirabuttonajouter").hide();
    $("#labelcmd").hide();
    $("#miratype").hide();
    $("#miratable").hide();    
    $("#divmiralist").hide();
    $("#divmirathermomodes").hide();
     	
    $("#PopupDiv").show();  
})

$("#miraboutontester").on('click', function (event) {
  	
    var ip = $("#miraip").val();
    var mdp = $("#miramdp").val();
    var url = 'http://'+ip+'/testmodule?key='+mdp;
    HTTPTestModule(url,ip,mdp);
   
})

function HTTPTestModule(url,ip,mdp){
  $.ajax({
    type: 'POST',
    url: 'plugins/mira/core/ajax/mira.ajax.php',
    data: {
      action: 'GetHTTP',
      url: url,

    },
    dataType: 'json',
    timeout: 8000,
    error: function(request, status, error) {
      handleAjaxError(request, status, error)
    },
    success: function(data) {
      if (data.result == 'Tout est OK'){
        var r = document.querySelector(':root');
        var rs = getComputedStyle(r);
        var button = document.getElementById('miraboutontester');
        button.style.backgroundColor = rs.getPropertyValue('--bt-success-color');
        $("#labelcmd").show();
        $("#miratype").show();
        $("#miratable").show();
        document.getElementById('miraip').disabled = true;
 		document.getElementById('miramdp').disabled = true;
        $("#miraboutontester").hide();
        var urlget = 'http://'+ip+'/getlvgl?key='+mdp;
        /* 1.2.9 : profil écran et configuration se chargent en parallèle. */
        MiraLoadDisplayInfo(ip,mdp);
        RempHTTPPost(urlget);
  
      }else{
        var r = document.querySelector(':root');
        var rs = getComputedStyle(r);
        var button = document.getElementById('miraboutontester');
        button.style.backgroundColor = rs.getPropertyValue('--al-danger-color');
      }
         
    }     
  })      
}



function HTTPTestModuleSend(url,ip,mdp){
  $.ajax({
    type: 'POST',
    url: 'plugins/mira/core/ajax/mira.ajax.php',
    data: {
      action: 'GetHTTP',
      url: url,

    },
    dataType: 'json',
    timeout: 8000,
    error: function(request, status, error) {
      handleAjaxError(request, status, error)
    },
    success: function(data) {
      if (data.result == 'Tout est OK'){
        var r = document.querySelector(':root');
        var rs = getComputedStyle(r);
        var button = document.getElementById('miraboutontester');
        button.style.backgroundColor = rs.getPropertyValue('--bt-success-color');
        SendListHTTP(ip,mdp);
      }else{
        var r = document.querySelector(':root');
        var rs = getComputedStyle(r);
        var button = document.getElementById('miraboutontester');
        button.style.backgroundColor = rs.getPropertyValue('--al-danger-color');
        alert("Oups! N'y aurait-il pas un petit problème dans les informations d'identification saisies?");
      }
         
    }     
  })      
}

$("#miraboutonenvoyer").on('click', function (event) {
  var ip = $("#miraip").val();
    var mdp = $("#miramdp").val();
    var url = 'http://'+ip+'/testmodule?key='+mdp;
    HTTPTestModuleSend(url,ip,mdp);
  
})
                            
                            
function MiraSafeText(value){
  if (value === undefined || value === null) return '';
  var text = String(value);
  return text === 'undefined' || text === 'null' ? '' : text;
}

function MiraValidateListRow(row){
  var type = row.cells[0].dataset.value || row.cells[0].innerText.trim();
  if (type !== 'list' && type !== 'listbutton') return true;

  var name = MiraDisplayName(row);
  var value = MiraSafeText(row.cells[7].innerText).trim();
  if (!value) {
    alert('La liste « '+name+' » ne contient plus aucune option. Renseigne son contenu avant de programmer l’écran.');
    return false;
  }

  var options = value.split('|').map(function(v){ return v.trim(); }).filter(function(v){ return v !== ''; });
  if (!options.length) {
    alert('La liste « '+name+' » ne contient aucune option valide.');
    return false;
  }
  return true;
}

/* Mira 1.2.15 — informations de presentation calculees par Jeedom.
 * Le firmware recoit uniquement l'identifiant de l'icone et le mode LVGL.
 * Garder les index de colonnes pour ne pas modifier le designer existant.
 */
var MiraInfoModes = [
  ['auto','Automatique'],
  ['text','Nom'],
  ['icon','Icône'],
  ['both','Nom + icône']
];
var MiraInfoIcons = [
  ['auto','Auto (Jeedom)'],
  ['temperature','Temperature'],
  ['humidity','Humidite'],
  ['pressure','Pression'],
  ['light','Luminosite'],
  ['energy','Energie / puissance'],
  ['plug','Prise'],
  ['door','Porte / fenetre']
];

function MiraIconNormalize(value){
  return MiraSafeText(value).normalize('NFD').replace(/[\u0300-\u036f]/g,'').toLowerCase();
}

function MiraPredictIcon(name, unit){
  var n=MiraIconNormalize(name), u=MiraIconNormalize(unit).trim();
  if(/temp|thermo/.test(n) || /°/.test(u)) return 'temperature';
  if(/humid|hygro/.test(n)) return 'humidity';
  if(/press/.test(n) || /^(hpa|mbar|bar)$/.test(u)) return 'pressure';
  if(/lumi|eclair|lux|ensoleill/.test(n) || u==='lux') return 'light';
  if(/porte|fenetre|ouvrant|door|window/.test(n)) return 'door';
  if(/prise|plug|socket/.test(n)) return 'plug';
  if(/energie|puissance|consomm|power|energie solaire/.test(n) || /^(w|kw|wh|kwh)$/.test(u)) return 'energy';
  return 'auto';
}

function MiraInfoValue(options, value){
  return options.some(function(o){return o[0] === value;}) ? value : 'auto';
}

function MiraSelectOptions(select, options, value){
  options.forEach(function(o){
    var option=document.createElement('option');
    option.value=o[0]; option.textContent=o[1];
    select.appendChild(option);
  });
  select.value=MiraInfoValue(options,value);
  return select;
}

function MiraRefreshInfoSelectors(type){
  var show=type==='info';
  $('#miralabelmodezone').toggle(show);
  $('#miraiconzone').toggle(show && ['icon','both'].includes($('#miralabelmode').val()));
  if(!show) return;
  var icon=$('#miraicon').val() || 'auto';
  var predicted=MiraPredictIcon($('#miraname').val(),$('#miraunit').val());
  var label=predicted==='auto' ? 'Auto (nom affiche si inconnu)' :
    'Auto (Jeedom : '+MiraIconLabel(predicted)+')';
  $('#miraicon option[value="auto"]').text(label);
  $('#miraicon').val(icon);
}

function MiraIconLabel(icon){
  for(var i=0;i<MiraInfoIcons.length;i++) if(MiraInfoIcons[i][0]===icon) return MiraInfoIcons[i][1];
  return 'Auto';
}

function MiraRowInfoSelect(className,options,value){
  var select=document.createElement('select');
  select.className=className;
  MiraSelectOptions(select, options, value);
  select.addEventListener('change',function(){
    var row=this.closest('tr');
    if(row) MiraRefreshRowInfoSummary(row);
    $("#miraboutonenvoyer").show();
    if($('#miradesigner').is(':visible')) MiraDesignerRender();
  });
  return select;
}

function MiraRefreshRowInfoSummary(row){
  var summary=row.querySelector('.mira-row-info-summary');
  if(!summary) return;
  var choice=row.querySelector('.mira-row-icon');
  var mode=row.querySelector('.mira-row-label-mode');
  var icon=choice ? choice.value : 'auto';
  var prediction=MiraPredictIcon(MiraDisplayName(row),row.cells[4].textContent);
  var displayed=icon==='auto' ? (prediction==='auto' ? 'Auto' : MiraIconLabel(prediction)) : MiraIconLabel(icon);
  summary.textContent=(mode && mode.value==='text' ? 'Sans icone' : displayed);
  summary.title='Affichage : '+(mode ? mode.options[mode.selectedIndex].text : 'Auto')+' ; icone : '+displayed;
}

function MiraAppendRowPresentation(cell,type,style,labelMode,icon){
  cell.dataset.value=style;
  var styleSelect=MiraStyleSelect(type,style);
  styleSelect.hidden=true;  // Styles historiques conservés mais plus exposés.
  cell.appendChild(styleSelect);
  if(type!=='info') { cell.appendChild(document.createTextNode('—')); return; }
  var wrapper=document.createElement('div');
  wrapper.className='mira-row-presentation';
  var mode=MiraRowInfoSelect('mira-row-label-mode',MiraInfoModes,labelMode);
  mode.setAttribute('aria-label','Affichage');
  var iconSelect=MiraRowInfoSelect('mira-row-icon',MiraInfoIcons,icon);
  iconSelect.setAttribute('aria-label','Icône');
  iconSelect.title='Icône du widget';
  function refresh(){ iconSelect.hidden=!(mode.value==='icon' || mode.value==='both'); }
  mode.addEventListener('change',refresh);
  wrapper.appendChild(mode); wrapper.appendChild(iconSelect);
  cell.appendChild(wrapper);
  refresh();
}

function MiraTechnicalName(row){
  return MiraSafeText(Object.prototype.hasOwnProperty.call(row.cells[3].dataset,'technicalName') ? row.cells[3].dataset.technicalName : row.cells[3].textContent).trim();
}
function MiraDisplayName(row){
  return MiraSafeText(Object.prototype.hasOwnProperty.call(row.cells[3].dataset,'displayName') ? row.cells[3].dataset.displayName : row.cells[3].textContent).trim();
}
function MiraSetRowName(row,technicalName,displayName){
  var cell=row.cells[3];
  cell.dataset.technicalName=technicalName;
  cell.dataset.displayName=displayName || technicalName;
  cell.textContent='';
  var label=document.createElement('span');
  label.className='mira-row-name';
  label.textContent=displayName || technicalName || 'Horloge';
  cell.appendChild(label);
  if(MiraRowType(row)!=='datetime'){
    var edit=document.createElement('button');
    edit.type='button'; edit.className='mira-row-options';
    edit.textContent='Modifier';
    edit.title='Changer le nom affiché et les paramètres utiles';
    edit.addEventListener('click',function(){ MiraOpenQuickOptions(row); });
    cell.appendChild(edit);
  }
}
function MiraUniqueTechnicalName(label){
  var used=new Set(MiraGetRows().map(function(row){ return MiraTechnicalName(row).toLocaleLowerCase(); }));
  var technical=label, n=2;
  while(used.has(technical.toLocaleLowerCase())) technical=label+' ('+(n++)+')';
  return technical;
}

function MiraRowIconConfig(row){
  var mode=row.querySelector('.mira-row-label-mode');
  var choice=row.querySelector('.mira-row-icon');
  var labelMode=mode ? mode.value : 'auto';
  var icon=choice ? choice.value : 'auto';
  return {
    labelMode: MiraInfoValue(MiraInfoModes,labelMode),
    // Automatic choice is resolved here in Jeedom before the POST /programui.
    icon: icon==='auto' ? MiraPredictIcon(MiraDisplayName(row),row.cells[4].textContent)
                        : MiraInfoValue(MiraInfoIcons,icon)
  };
}

function MiraStyleOptions(type){
  if (type === 'info') return [['standard','Standard'],['value','Valeur dominante'],['compact','Compact']];
  if (type === 'button' || type === 'camera') return [['standard','Standard'],['compact','Compact']];
  if (type === 'toggle') return [['standard','Standard'],['compact','Compact']];
  if (type === 'range' || type === 'roundrange' || type === 'list') return [['standard','Standard'],['compact','Compact']];
  if (type === 'listbutton') return [['standard','Standard'],['compact','Compact'],['segmented','Segmenté']];
  return [];
}

function MiraRefreshStyleSelector(type, selected){
  var options=MiraStyleOptions(type), select=$('#mirastyle');
  select.empty();
  if(!options.length){ $('#mirastylezone').hide(); return; }
  options.forEach(function(o){ select.append($('<option>',{value:o[0],text:o[1]})); });
  select.val(selected || 'standard');
  if(!select.val()) select.val('standard');
  $('#mirastylezone').hide();
}

function MiraStyleLabel(type, style){
  var options=MiraStyleOptions(type);
  for(var i=0;i<options.length;i++) if(options[i][0]===style) return options[i][1];
  return 'Standard';
}

function MiraStyleSelect(type, style){
  var select=document.createElement('select');
  select.className='mira-row-style';
  var options=MiraStyleOptions(type);
  if(!options.length) options=[['standard','Standard']];
  options.forEach(function(o){
    var opt=document.createElement('option');
    opt.value=o[0]; opt.textContent=o[1];
    if(o[0]===(style||'standard')) opt.selected=true;
    select.appendChild(opt);
  });
  select.addEventListener('change',function(){ $('#miraboutonenvoyer').show(); });
  return select;
}

function MiraLayoutOptions(type){
  if (type === 'info') return [
    ['auto','Auto'],
    ['tile','Tuile'],
    ['wide','Pleine largeur'],
    ['hero','Mise en avant'],
    ['free','Libre (designer)']
  ];
  return [['auto','Auto'],['free','Libre (designer)']];
}

function MiraRefreshLayoutSelector(type, selected){
  var options=MiraLayoutOptions(type), select=$('#miralayout');
  select.empty();
  /* Le formulaire classique reste simple : la disposition manuelle se fait
   * dans le designer. Les autres types restent donc sur Auto ici. */
  if(type !== 'info'){
    select.append($('<option>',{value:'auto',text:'Auto'})).val('auto');
    $('#miralayoutzone').hide();
    return;
  }
  options.filter(function(o){ return o[0] !== 'free'; }).forEach(function(o){ select.append($('<option>',{value:o[0],text:o[1]})); });
  select.val(selected || 'auto');
  if(!select.val()) select.val('auto');
  $('#miralayoutzone').hide();
}

function MiraLayoutSelect(type,layout){
  var select=document.createElement('select');
  select.className='mira-row-layout';
  // Valeurs des anciennes versions conservées sans forcer leur conversion.
  (type==='thermostat' ? [['auto','Auto']] : [['auto','Auto'],['free','Manuel · designer']]).forEach(function(o){
    var opt=document.createElement('option'); opt.value=o[0]; opt.textContent=o[1]; select.appendChild(opt);
  });
  if(layout && layout!=='auto' && layout!=='free'){
    var legacy=document.createElement('option');
    legacy.value=layout; legacy.textContent='Auto';
    select.appendChild(legacy);
  }
  select.value=layout||'auto';
  select.addEventListener('change',function(){
    var row=this.closest('tr');
    if(this.value==='free' && !MiraDisplayInfo.freeLayout){
      alert('Le firmware de cet écran ne prend pas en charge le designer visuel.');
      this.value=row && row.dataset.freeLayout==='1' ? 'free' : 'auto';
      return;
    }
    if(row){
      if(this.value==='free' && !MiraEnsureRowGeometry(row)){ this.value='auto'; return; }
      else row.dataset.freeLayout='0';
    }
    $('#miraboutonenvoyer').show();
    if($('#miradesigner').is(':visible')) MiraDesignerRender();
    if(this.value==='free' && MiraDisplayInfo.freeLayout){
      $('#mirabuttondesigner').trigger('click');
      var page=MiraRowPage(row);
      MiraDesignerPage=page;
      $('.mira-page-tab').removeClass('active').filter('[data-page="'+page+'"]').addClass('active');
      setTimeout(MiraDesignerRender,0);
    }
  });
  return select;
}

function MiraLimitCell(type, value, kind){
  if (type !== 'range' && type !== 'roundrange' && type !== 'thermostat') {
    return document.createTextNode(value == null ? '' : value);
  }

  var input=document.createElement('input');
  input.type='number';
  input.step='1';
  input.className='mira-row-limit';
  input.value=(value == null ? '' : value);
  input.setAttribute('aria-label', kind === 'min' ? 'Valeur mini' : 'Valeur maxi');
  input.addEventListener('input', function(){ $('#miraboutonenvoyer').show(); });
  input.addEventListener('change', function(){ $('#miraboutonenvoyer').show(); });
  return input;
}

function MiraCellValue(cell){
  var input=cell.querySelector('.mira-row-limit');
  return input ? input.value : MiraSafeText(cell.innerText).trim();
}

function MiraValidateRangeRow(row){
  var type=row.cells[0].dataset.value || row.cells[0].innerText.trim();
  if (type !== 'range' && type !== 'roundrange' && type !== 'thermostat') return true;

  var min=parseFloat(MiraCellValue(row.cells[5]));
  var max=parseFloat(MiraCellValue(row.cells[6]));
  if (!Number.isFinite(min) || !Number.isFinite(max) || min >= max) {
    alert('Pour le curseur « '+MiraDisplayName(row)+' », la valeur mini doit être inférieure à la valeur maxi.');
    return false;
  }
  return true;
}

async function SendListHTTP(ip,mdp){
  var miratable = document.getElementById('miratable');
  var expectedWidgets = [];
  var payload = {lvgl:[]};
  var sendButton = $('#miraboutonenvoyer');
  var designerSave = $('#miradesignersave');
  var originalLabel = sendButton.text();
  var originalDesignerLabel = designerSave.html();

  sendButton.text('Programmation en cours…');
  designerSave.html('<i class="fas fa-spinner fa-spin"></i> Enregistrement…');
  sendButton.add(designerSave).css({'pointer-events':'none','opacity':'0.65'});

  try {
    for (let i = 2; i < miratable.rows.length; i++) {
      if (!MiraValidateRangeRow(miratable.rows[i])) return;
      if (!MiraValidateListRow(miratable.rows[i])) return;
    }

    /* 1.2.9 : on construit le JSON complet côté Jeedom puis on l'envoie en
     * une seule requête HTTP à l'écran. HTTP reste le transport principal. */
    for (let i = 2; i < miratable.rows.length; i++) {
      var row = miratable.rows[i];
      var miratype = row.cells[0].dataset.value || row.cells[0].innerText;
      var styleSelect = row.cells[1].querySelector('.mira-row-style');
      var mirastyle = styleSelect ? styleSelect.value : (row.cells[1].dataset.value || 'standard');
      var miraemplacement = MiraSafeText(row.cells[2].innerText);
      var miraname = MiraTechnicalName(row);
      var miradisplayname = MiraDisplayName(row);
      var miraunit = MiraSafeText(row.cells[4].innerText);
      var miramin = MiraCellValue(row.cells[5]);
      var miramax = MiraCellValue(row.cells[6]);
      var miraval = MiraSafeText(row.cells[7].innerText);
      var layoutSelect = row.cells[8].querySelector('.mira-row-layout');
      var miralayout = layoutSelect ? layoutSelect.value : (row.cells[8].dataset.value || 'auto');

      if (row.dataset.freeLayout === '1' || miralayout === 'free') {
        MiraEnsureRowGeometry(row);
        miralayout = 'free';
      }

      var item = {
        type: miratype,
        name: miraname,
        emplacement: miraemplacement,
        min: String(miramin),
        max: String(miramax),
        value: miraval,
        unit: miraunit,
        style: mirastyle,
        layout: miralayout
      };
      if(miradisplayname && miradisplayname !== miraname) item.displayName=miradisplayname;
      if(miratype === 'thermostat') {
        item.modes=MiraSafeText(row.dataset.thermoModes)||'Confort|Éco|Hors gel|Arrêt';
      }
      if(miratype === 'info') {
        var presentation=MiraRowIconConfig(row);
        item.labelMode=presentation.labelMode;
        item.icon=presentation.icon;
      }
      if(miralayout === 'free'){
        item.lx=parseInt(row.dataset.lx||'0',10);
        item.ly=parseInt(row.dataset.ly||'0',10);
        item.lw=parseInt(row.dataset.lw||'500',10);
        item.lh=parseInt(row.dataset.lh||'150',10);
      }
      if(miratype !== 'info' && miratype !== 'datetime') item.url='';
      payload.lvgl.push(item);

      expectedWidgets.push({
        modes: miratype==='thermostat' ? item.modes : undefined,
        type: miratype,
        style: mirastyle,
        layout: miralayout,
        name: miraname,
        value: miraval
      });
    }

    // 1.2.17 : le relais confirme aussi les ecritures si l'ESP redemarre
    // avant de repondre, par relecture exacte de /getlvgl.
    await HTTPPostJson('http://'+ip+'/programui',mdp,JSON.stringify(payload));

    SyncJeedomCommands(ip, expectedWidgets);
    Vider();
  } catch (e) {
    console.error('Mira: erreur pendant la programmation', e);
    var profile=MiraDisplayInfoLoaded ? (MiraDisplayInfo.profile+' '+MiraDisplayInfo.width+'×'+MiraDisplayInfo.height) : 'profil non détecté';
    var detail=e && e.message ? String(e.message) : 'Erreur inconnue';
    // Show the actual failure (HTTP code, server error or timeout),
    // instead of incorrectly suggesting the password is always at fault.
    alert('Mira : programmation non terminée ('+profile+').\n\nDétail : '+detail.slice(0,300));
  } finally {
    sendButton.text(originalLabel);
    designerSave.html(originalDesignerLabel);
    sendButton.add(designerSave).css({'pointer-events':'','opacity':''});
  }
}

function SyncJeedomCommands(ip, expectedWidgets){
  $.ajax({
    type: 'POST',
    url: 'plugins/mira/core/ajax/mira.ajax.php',
    data: {
      action: 'SyncByScreen',
      ip: ip,
      expected: JSON.stringify(expectedWidgets || [])
    },
    dataType: 'json',
    timeout: 35000,
    error: function(request, status, error) {
      /* La programmation de l'ecran reste valide meme si la synchro tarde. */
      console.warn('Mira: synchronisation Jeedom impossible', status, error);
    },
    success: function(data) {
      if (data && data.result) {
        console.log('Mira autosync:', data.result);
      }
    }
  });
}

function HTTPPost(url){
  return $.ajax({
    type: 'POST',
    url: 'plugins/mira/core/ajax/mira.ajax.php',
    data: {
      action: 'GetHTTP',
      url: url
    },
    dataType: 'json',
    timeout: 8000
  }).then(function(data){
    return data.result;
  });
}


/* Diagnostics de programmation Mira — rapporter la vraie structure renvoyee par
 * Jeedom lorsqu'elle n'est pas celle attendue. Ne jamais inclure la cle ni
 * le JSON de programmation dans les messages ou les journaux navigateur. */
function MiraProgramResult(data, browserCode){
  var envelope = data;
  var ajaxState = envelope && typeof envelope === 'object' ? envelope.state : '';
  if (ajaxState === 'error') {
    var serverError = envelope.result || envelope.message || envelope.error || 'Erreur non precisée';
    throw new Error('Jeedom a refusé l’action PostHTTPJson : '+String(serverError).slice(0,220));
  }
  var result = envelope && typeof envelope === 'object' &&
    Object.prototype.hasOwnProperty.call(envelope, 'result') ? envelope.result : envelope;

  // Certains relais AJAX encapsulent une seconde fois le résultat.
  for (var i=0;i<2;i++){
    if (typeof result === 'string' && result.trim().charAt(0) === '{') {
      try { result=JSON.parse(result); } catch(e) { break; }
    }
    if(result && typeof result === 'object' && result.httpCode == null &&
       result.result != null && typeof result.result === 'object'){
      result=result.result;
    } else { break; }
  }
  if(result && typeof result === 'object' && result.httpCode != null){
    var screenHttp=Number(result.httpCode);
    var body=result.body == null ? '' : String(result.body);
    if(screenHttp === 200 && body.trim() === 'OK') return body;
    throw new Error('Le WT32 a répondu HTTP '+screenHttp+', contenu '+
      (body.trim() ? '« '+body.slice(0,160)+' »' : 'vide')+
      '. Le contenu envoyé n’a pas été confirmé.');
  }
  var kind=Array.isArray(result) ? 'tableau' : typeof result;
  var keys=result && typeof result==='object' ? Object.keys(result).slice(0,12).join(',') : '';
  var raw=typeof result==='string' ? result.slice(0,120) : '';
  throw new Error('Réponse AJAX Jeedom inattendue (HTTP navigateur '+browserCode+
    ', état '+(ajaxState || '?')+', type '+kind+
    (keys ? ', champs '+keys : '')+
    (raw ? ', début « '+raw+' »' : '')+
    '). Le retour de /programui n’est pas vérifiable.');
}

function HTTPPostJson(url,key,json){
  return $.ajax({
    type: 'POST',
    url: 'plugins/mira/core/ajax/mira.ajax.php',
    data: {
      action: 'PostHTTPJson',
      url: url,
      key: key,
      json: json
    },
    dataType: 'json',
    timeout: 40000
  }).then(function(data, status, xhr){
    return MiraProgramResult(data, xhr && xhr.status ? xhr.status : '?');
  }, function(xhr,status,error){
    if(status==='timeout') throw new Error('Délai de 40 s dépassé entre Jeedom et le navigateur.');
    var server=xhr && xhr.responseJSON;
    var details=server && (server.result || server.message || server.error);
    if(details && typeof details==='object') details=JSON.stringify(details);
    var response=xhr && xhr.responseText ? String(xhr.responseText).slice(0,100).replace(/\s+/g,' ') : '';
    throw new Error('Erreur AJAX Jeedom '+(xhr && xhr.status ? xhr.status : '?')+
      ' ('+(status||'inconnu')+') : '+String(details || error || response || 'aucun détail').slice(0,180));
  });
}

function RempHTTPPost(url){  
  $.ajax({
    type: 'POST',
    url: 'plugins/mira/core/ajax/mira.ajax.php',
    data: {
      action: 'GetHTTP',
      url: url,

    },
    dataType: 'json',
    timeout: 8000,
    error: function(request, status, error) {
      handleAjaxError(request, status, error)
    },
    success: function(data) {  
        Remplissage(data.result); 
    }     
  })
}


function Vider(){
  MiraCloseQuickOptions();
  document.getElementById('miraip').disabled = false;
  document.getElementById('miramdp').disabled = false;
  $("#miraboutontester").show();
  
  $("#labelcmd").hide();
  $("#miratype").hide();
  $("#miratable").hide();
  
  var r = document.querySelector(':root');
  var rs = getComputedStyle(r);
  var button = document.getElementById('miraboutontester');
  button.style.backgroundColor = rs.getPropertyValue('--al-warning-color');
  
  $("#miraip").val('');
  $("#miramdp").val('');
  $("#miraname").val('');
  $("#miramin").val('');
  $("#miramax").val('');
  $("#miraval").val('');
  $("#miraunit").val(''); 
  $("#miratype").val('nope'); 
  
  
  $("#miraemplacement").hide();
  $("#mirastylezone").hide();
  $("#mirastyle").empty();
  $("#miralayoutzone").hide();
  $("#miralayout").empty();
  $("#miralabelmodezone,#miraiconzone").hide();
  $("#miralabelmode").val("text");
  $("#miraicon").val("auto");
  $("#miradesignerbar").hide();
  $("#miradesigner").hide();
  $("#miraClassicEditor").show();
  $("#PopupDiv").removeClass("designer-open");
  MiraDesignerSelected = null;
  MiraDisplayInfo = {profile:'legacy', width:320, height:480, contentWidth:320, contentHeight:434, navHeight:46, logicalWidth:1000, logicalHeight:1000, freeLayout:false};
  MiraDisplayInfoLoaded = false;
  $("#divmiraname").hide();
  $("#divmiramin").hide();
  $("#divmiramax").hide();
  $("#divmiraval").hide();
  $("#divmiraunit").hide();
  $("#divmiralist").hide();
  $("#miraboutonenvoyer").hide();
  $("#mirabuttonajouter").hide();
  
  var miratable = document.getElementById('miratable'); 
  if (miratable.rows.length>2){
    var taille = miratable.rows.length;
    for (let i = 2; i < taille; i++) { 
      var miraname = MiraTechnicalName(miratable.rows[2]);      
      miratable.deleteRow(2);

    }
  }
  nbr = 0;
}


function Remplissage(contjson){
  const obj = JSON.parse(contjson);
  var keyCount  = Object.keys(obj["lvgl"]).length;
  
    	
  for (let i = 0; i < keyCount; i++) {
    
    var miraemplacement = MiraSafeText(obj["lvgl"][i]["emplacement"]);
  	var miratype = MiraSafeText(obj["lvgl"][i]["type"]);
    var mirastyle = MiraSafeText(obj["lvgl"][i]["style"]) || 'standard';
    var miralayout = MiraSafeText(obj["lvgl"][i]["layout"]) || 'auto';
  	var miraname = MiraSafeText(obj["lvgl"][i]["name"]);
    var miradisplayname = MiraSafeText(obj["lvgl"][i]["displayName"]) || miraname;
  	var miramin = MiraSafeText(obj["lvgl"][i]["min"]);
  	var miramax = MiraSafeText(obj["lvgl"][i]["max"]);
  	var miraval = MiraSafeText(obj["lvgl"][i]["value"]);
    var miraunit = MiraSafeText(obj["lvgl"][i]["unit"]);
    var miralabelmode=MiraInfoValue(MiraInfoModes,MiraSafeText(obj["lvgl"][i]["labelMode"])||'auto');
    var iconFromScreen=MiraSafeText(obj["lvgl"][i]["icon"])||'auto';
    // Auto was resolved before send: reconstruct a sensible Auto selection.
    var miraicon=iconFromScreen===MiraPredictIcon(miraname,miraunit) ? 'auto' :
      MiraInfoValue(MiraInfoIcons,iconFromScreen);

       
    var nmiraemplacement = document.createTextNode(miraemplacement);
    var nmiratype = document.createTextNode(miratype);
    var nmiraname = document.createTextNode(miraname);
    var nmiramin = MiraLimitCell(miratype, miramin, 'min');
    var nmiramax = MiraLimitCell(miratype, miramax, 'max');
    var nmiraval = document.createTextNode(miraval);
    var nmiraunit = document.createTextNode(miraunit);

    var miratable = document.getElementById('miratable');
    var mirarow = miratable.insertRow();

    var miracelltype = mirarow.insertCell(0);
    var miracellstyle = mirarow.insertCell(1);
    var miracellemp = mirarow.insertCell(2);
    var miracellname = mirarow.insertCell(3);
    var miracellunit = mirarow.insertCell(4);
    var miracellmin = mirarow.insertCell(5);
    var miracellmax = mirarow.insertCell(6);
    var miracellval = mirarow.insertCell(7);
    var miracelllayout = mirarow.insertCell(8);
    var miracellsupp = mirarow.insertCell(9);

    miracelltype.dataset.value = miratype;
    miracelltype.appendChild(nmiratype);
    MiraAppendRowPresentation(miracellstyle,miratype,mirastyle,miralabelmode,miraicon);
    miracellemp.appendChild(nmiraemplacement);  
    MiraSetRowName(mirarow,miraname,miradisplayname);  
    miracellunit.appendChild(nmiraunit);
    MiraRefreshRowInfoSummary(mirarow);  
    miracellmin.appendChild(nmiramin);  
    miracellmax.appendChild(nmiramax);  
    miracellval.appendChild(nmiraval);
    miracelllayout.dataset.value = miralayout;
    miracelllayout.appendChild(MiraLayoutSelect(miratype, miralayout));
    mirarow.dataset.freeLayout = miralayout === 'free' ? '1' : '0';
    mirarow.dataset.thermoModes = MiraSafeText(obj["lvgl"][i]["modes"]) || 'Confort|Éco|Hors gel|Arrêt';
    mirarow.dataset.lx = MiraSafeText(obj["lvgl"][i]["lx"]);
    mirarow.dataset.ly = MiraSafeText(obj["lvgl"][i]["ly"]);
    mirarow.dataset.lw = MiraSafeText(obj["lvgl"][i]["lw"]);
    mirarow.dataset.lh = MiraSafeText(obj["lvgl"][i]["lh"]);
    miracellsupp.innerHTML='<i class="bt-supprimer fas fa-minus-circle"></i>';
    
    oBtSup = miratable.getElementsByClassName('bt-supprimer');
    oBtSup[i].addEventListener('click',  supprimerLigne); 
    nbr=i;
  }  
}


function supprimerLigne(oEvent){
  var oEleBt = oEvent.currentTarget,
  oTr = oEleBt.parentNode.parentNode ;
  oTr.remove();
  nbr--;
  $("#miraboutonenvoyer").show();
  if($('#miradesigner').is(':visible')) MiraDesignerRender();
}

$("#mirabuttonannuler").on('click', function (event) {
  $("#PopupDiv").hide();
  Vider();
  
})




function sleep(miliseconds) {
   var currentTime = new Date().getTime();
   while (currentTime + miliseconds >= new Date().getTime()) {
   }
}



$("#mirabuttonajouter").on('click', function (event) {
  var miraemplacement = $("#emplacementSelect").val();
  var miratype = $("#miratype").val();
  var mirastyle = 'standard';
  var miralayout = 'auto';
  var miralabelmode = $('#miralabelmode').val() || 'text';
  var miraicon = $('#miraicon').val() || 'auto';
  var miraname = $("#miraname").val();
  var miramin = $("#miramin").val();
  var miramax = $("#miramax").val();
  
  var miraval = '';
  if (miratype == 'list' || miratype == 'listbutton') miraval = $("#miralist").val();
  else miraval = $("#miraval").val();
  var miraunit = $("#miraunit").val(); 
  
  var miratable = document.getElementById('miratable');
  miraname=MiraSafeText(miraname).trim();
  var thermoModes=$('#mirathermomodes').val().trim() || 'Confort|Éco|Hors gel|Arrêt';
  if (miratype === 'camera') {
    miraval = String(miraval || '').trim();
    if (!/^http:\/\/[^\s]+$/i.test(miraval) || /[\r\n]/.test(miraval)) {
      alert('Caméra beta : saisir une URL HTTP BMP locale valide.');
      return;
    }
  }
  if(miratype==='thermostat'){
    if(!thermoModes.split('|').some(function(v){return v.trim();})){ alert('Il faut au moins un mode thermostat.'); return; }
    miraval=String(Math.round(Number(miraval)*2)/2);
    if(!Number.isFinite(Number(miraval))){ alert('Consigne thermostat invalide.'); return; }
  }
  var miradisplayname=miraname;
  var miratechnicalname=miratype==='datetime' ? '' : MiraUniqueTechnicalName(miraname);

  if (miratype==='nope'){ alert('Choisis un type de widget.'); return; }
  if (miraemplacement == 'nope'){
   	alert("Veuillez choisir un emplacement à l'écran pour la commande créée");
    return
  }
  if (miraname == '' && miratype != 'datetime'){
   	alert("Veuillez choisir un nom pour la commande créée");
    return
  }
  
  var nmiraemplacement = document.createTextNode(miraemplacement);
  var nmiratype = document.createTextNode(miratype);
  var nmiraname = document.createTextNode(miraname);
  var nmiramin = MiraLimitCell(miratype, miramin, 'min');
  var nmiramax = MiraLimitCell(miratype, miramax, 'max');
  var nmiraval = document.createTextNode(miraval);
  var nmiraunit = document.createTextNode(miraunit);
  
  var miratable = document.getElementById('miratable');
  var mirarow = miratable.insertRow();
  
  var miracelltype = mirarow.insertCell(0);
  var miracellstyle = mirarow.insertCell(1);
  var miracellemp = mirarow.insertCell(2);
  var miracellname = mirarow.insertCell(3);
  var miracellunit = mirarow.insertCell(4);
  var miracellmin = mirarow.insertCell(5);
  var miracellmax = mirarow.insertCell(6);
  var miracellval = mirarow.insertCell(7);
  var miracelllayout = mirarow.insertCell(8);
  var miracellsupp = mirarow.insertCell(9);
  
  miracelltype.dataset.value = miratype;
  miracelltype.appendChild(nmiratype);
  MiraAppendRowPresentation(miracellstyle,miratype,mirastyle,miralabelmode,miraicon);
  miracellemp.appendChild(nmiraemplacement);  
  MiraSetRowName(mirarow,miratechnicalname,miradisplayname);  
  miracellunit.appendChild(nmiraunit);
    MiraRefreshRowInfoSummary(mirarow);  
  miracellmin.appendChild(nmiramin);  
  miracellmax.appendChild(nmiramax);  
  miracellval.appendChild(nmiraval);
  miracelllayout.dataset.value = miralayout;
  miracelllayout.appendChild(MiraLayoutSelect(miratype, miralayout));
  mirarow.dataset.freeLayout = miralayout === 'free' ? '1' : '0';
  mirarow.dataset.thermoModes = miratype==='thermostat' ? thermoModes : '';
  mirarow.dataset.lx = '';
  mirarow.dataset.ly = '';
  mirarow.dataset.lw = '';
  mirarow.dataset.lh = '';
  miracellsupp.innerHTML='<i class="bt-supprimer fas fa-minus-circle"></i>';

  if(MiraDisplayInfo.freeLayout && miralayout==='auto' && MiraPageHasFreeLayout(miraemplacement, mirarow)) {
    if(!MiraPlaceRowAfterContent(mirarow)){
      mirarow.remove();
      alert('Plus de place disponible sur cette page. Ouvre le designer pour réorganiser les tuiles.');
      return;
    }
  }
    
  if (nbr<0) nbr=0;
  oBtSup = miratable.getElementsByClassName('bt-supprimer');
  oBtSup[nbr].addEventListener('click',  supprimerLigne); 
  nbr++;
  
  $("#miraname").val('');
  $("#miramin").val('');
  $("#miramax").val('');
  $("#miraval").val('');
  $("#miraunit").val(''); 
  $("#miralist").val('');
  MiraRefreshStyleSelector(miratype, 'standard');
  MiraRefreshLayoutSelector(miratype, 'auto');
  $('#mirathermomodes').val('Confort|Éco|Hors gel|Arrêt');
  $('#miralabelmode').val('text');
  $('#miraicon').val('auto');
  MiraRefreshInfoSelectors(miratype);
  $("#miraboutonenvoyer").show();
  if($('#miradesigner').is(':visible')) MiraDesignerRender();
  
})


$("#miratype").on('change',function(event) {
  
  var lvgl_type = $("#miratype").val();
  MiraRefreshStyleSelector(lvgl_type, 'standard');
  MiraRefreshLayoutSelector(lvgl_type, 'auto');
  MiraRefreshInfoSelectors(lvgl_type);
  $('#divmirathermomodes').toggle(lvgl_type==='thermostat');
  $('#miracamerahelp').toggle(lvgl_type==='camera');
  $('#miraval').attr('type', lvgl_type === 'camera' ? 'text' : 'number');
  if(lvgl_type === 'camera') $('#miraval').attr('placeholder','http://192.168.1.x:8098/snapshot.bmp');
  else $('#miraval').removeAttr('placeholder');
  if (lvgl_type === 'thermostat') {
    $('#miramin').val('5'); $('#miramax').val('30'); $('#miraval').val('20.5');
    $('#miraunit').val('°C'); $('#miralist').val('');
    $('#miraemplacement,#divmiraname,#divmiramin,#divmiramax,#divmiraval,#mirabuttonajouter').show();
    $('#divmiraunit,#divmiralist').hide();
    $('#titlemiraval').text('Consigne de départ :');
    $('#titlemiramin').text('Consigne mini :');
  } else if (lvgl_type === 'camera') {
    $('#miramin,#miramax,#miraunit,#miralist').val('');
    $('#miraemplacement,#divmiraname,#divmiraval,#mirabuttonajouter').show();
    $('#divmiramin,#divmiramax,#divmiraunit,#divmiralist').hide();
    $('#titlemiraval').text('URL image BMP :');
  } else if (lvgl_type == 'button' || lvgl_type == 'toggle' || lvgl_type == 'color' || lvgl_type == 'multiaction'){
  	$("#miramin").val('');
    $("#miramax").val('');
    $("#miraval").val('');
    $("#miraunit").val(''); 
    $("#miralist").val('');
 

    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").hide();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'shieldrelay' || lvgl_type == 'shieldalarm'){
  	$("#miraunit").val(''); 
    $("#miraval").val('');
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").show();
    $("#titlemiraval").html('GPIO:');    
    $("#divmiraunit").hide();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'shieldtoggle'){
  	$("#miraunit").val(''); 
    $("#miraval").val('');
    $("#miramin").val('');
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").show();
    $("#titlemiramin").html('Lien GPIO:');    
    $("#divmiramax").hide();
    $("#divmiraval").show();
    $("#titlemiraval").html('GPIO:');    
    $("#divmiraunit").hide();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'range'){
  	$("#miraunit").val(''); 
    $("#miralist").val('');
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").show();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").show();
    $("#divmiraval").show();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").hide();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'roundrange'){
  	$("#miralist").val('');
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").show();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").show();
    $("#divmiraval").show();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").show();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'info'){
  	$("#miramin").val('');
    $("#miramax").val('');
    $("#miraval").val('');
    $("#miralist").val('');
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").show();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'datetime'){
  	$("#miraname").val('');
    $("#miramin").val('');
    $("#miramax").val('');
    $("#miraval").val('');
    $("#miraunit").val(''); 
    $("#miralist").val('');
    
    $("#miraemplacement").show();
    $("#divmiraname").hide();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").hide();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'list'){
  	$("#miramin").val('');
    $("#miramax").val('');
    $("#miraval").val('');
    $("#miraunit").val(''); 
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").hide();
    $("#divmiralist").show();
    $("#mirabuttonajouter").show();
  }else if (lvgl_type == 'listbutton'){
  	$("#miramin").val('');
    $("#miramax").val('');
    $("#miraval").val('');
    $("#miraunit").val(''); 
    
    $("#miraemplacement").show();
    $("#divmiraname").show();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").hide();
    $("#divmiralist").show();
    $("#mirabuttonajouter").show();
  }else{
    $("#miraname").val('');
    $("#miramin").val('');
    $("#miramax").val('');
    $("#miraval").val('');
    $("#miraunit").val(''); 
    $("#miralist").val('');
    
    $("#mirabuttonajouter").hide();
    $("#miraemplacement").hide();
    $("#divmiraname").hide();
    $("#divmiramin").hide();
    $("#titlemiramin").html('Valeur mini:');    
    $("#divmiramax").hide();
    $("#divmiraval").hide();
    $("#titlemiraval").html('Valeur:');    
    $("#divmiraunit").hide();
    $("#divmiralist").hide();
    $("#mirabuttonajouter").hide();
  }
  
})



$("#table_cmd").delegate(".listEquipementInfo", 'click', function () {
  var el = $(this)
  jeedom.cmd.getSelectModal({ cmd: { type: 'info' } }, function (result) {
    var calcul = el.closest('tr').find('.cmdAttr[data-l1key=configuration][data-l2key=' + el.data('input') + ']')
    calcul.atCaret('insert', result.human)
  })
})

$("#table_cmd").delegate(".listEquipementAction", 'click', function () {
  var el = $(this)
  var subtype = $(this).closest('.cmd').find('.cmdAttr[data-l1key=subType]').value()
  jeedom.cmd.getSelectModal({ cmd: { type: 'action', subType: subtype } }, function (result) {
    var calcul = el.closest('tr').find('.cmdAttr[data-l1key=configuration][data-l2key=' + el.attr('data-input') + ']')
    calcul.atCaret('insert', result.human);
  })
})

$("#table_cmd").sortable({ axis: "y", cursor: "move", items: ".cmd", placeholder: "ui-state-highlight", tolerance: "intersect", forcePlaceholderSize: true })

/* Fonction permettant l'affichage des commandes dans l'équipement */
function miraListOptionKey(optionName) {
  /* Cle stable basee sur le libelle UTF-8 et non sur l'ID dynamique Mira.
   * Ainsi, ajouter/reordonner d'autres widgets ne casse pas les mappings.
   */
  var utf8 = unescape(encodeURIComponent(optionName || ''))
  var hex = ''
  for (var i = 0; i < utf8.length; i++) {
    hex += ('0' + utf8.charCodeAt(i).toString(16)).slice(-2)
  }
  return 'listAction_' + hex
}

function addCmdToTable(_cmd) {
            
                      
  
  if (!isset(_cmd)) {
    var _cmd = { configuration: {} }
  }
  if (!isset(_cmd.configuration)) {
    _cmd.configuration = {}
  }
  
      var tr ="";
      if (_cmd.configuration.visible_cmd == "1"){
        tr = '<tr class="cmd"  data-cmd_id="' + init(_cmd.id) + '">'      
      }else{
        tr = '<tr style="display:none;" class="cmd" data-cmd_id="' + init(_cmd.id) + '">'      
      }
  
  		
     
        
      //ID  
  	  tr += '<td class="hidden-xs">'  
      tr += '<span  class="cmdAttr" data-l1key="id"></span>'
      tr += '</td>'
  	  
      //Nom
      tr += '<td>'
      tr += '<div class="input-group">'
      tr += '<input class="cmdAttr form-control input-sm roundedLeft" data-l1key="name" placeholder="{{Nom de la commande}}">'
  	  tr += '<div style="display: none">'
      tr += _cmd.name;
      tr += '</div>'  	
      tr += '<span class="input-group-btn"><a class="cmdAction btn btn-sm btn-default" data-l1key="chooseIcon" title="{{Choisir une icône}}"><i class="fas fa-icons"></i></a></span>'
      tr += '<span class="cmdAttr input-group-addon roundedRight" data-l1key="display" data-l2key="icon" style="font-size:19px;padding:0 5px 0 0!important;"></span>'
      tr += '</div>'  
      tr += '</td>'
  
  	  //ID Mira
      var isMultiMaster = _cmd.configuration.type === 'multi_master';
      var isMultiAction = _cmd.configuration.type === 'multi_action';
      var miraLocalEligible = (_cmd.type === 'action' &&
          (_cmd.configuration.type === 'toggle' || _cmd.configuration.type === 'button')) ||
          (_cmd.type === 'info' && _cmd.configuration.type === 'toggle');
      var miraLocalAction = (_cmd.type === 'action' && miraLocalEligible);
      tr += '<td class="num">'
      if (isMultiMaster) tr += '<select class="cmdAttr mira-multi-trigger form-control input-sm" data-l1key="configuration" data-l2key="multi_trigger" style="min-width:110px"><option value="jeedom">Jeedom</option><option value="gpio">GPIO</option><option value="both">Les deux</option></select>';
      if (miraLocalEligible) tr += '<select class="cmdAttr mira-gpio-source form-control input-sm" data-l1key="configuration" data-l2key="mira_source" style="min-width:85px"><option value="jeedom">Jeedom</option><option value="gpio">GPIO</option></select>';
  	  
        if (_cmd.configuration.commande != "-1"){
          
            /*if ((_cmd.type == 'info' && _cmd.configuration.type == 'info') || _cmd.type == 'action'){
                tr += '<div disabled class="tooltips cmdAttr form-control input-sm" data-l1key="configuration" data-l2key="commande" placeholder="mira" title="{{Commande mira}}" style="display: none; width:50px;display:inline-block;margin-right:2px;"></div>'
            }else{
                tr += '<div style="display: none">'+_cmd.configuration.commande+'</div>'                    
            }*/
            	
          
        }
      
      tr += '</td>'
  
  	  //Unités
      tr += '<td>'
      if (_cmd.configuration.type == "info"){
        tr += '<div style="display: none">'
        tr += _cmd.unite;
        tr += '</div>'
  	  	tr += '<input class="tooltips cmdAttr form-control input-sm" data-l1key="unite" placeholder="Unité" title="{{Unité}}" style="width:60px; display:inline-block;margin-right:2px;">'        
      }
      tr += '</td>'
   
      //Afficher
      tr += '<td>'
  	  var value= "0";
  	  if (_cmd.isVisible == "1") value = "1";         
      tr += '<div sorttable_customkey='+value+' >' 
      tr += '<label   class="checkbox-inline"><input type="checkbox" class="cmdAttr" data-l1key="isVisible" checked/>{{Afficher}}</label> '
   	  tr += '</div></td>'
  
      //Historiser
      tr += '<td>'
       if (_cmd.type == "info"){
         if (_cmd.isHistorized == "1") value = "1"; 
         
        tr += '<div sorttable_customkey='+value+' >'         
        
  		tr += '<label class="checkbox-inline"><input type="checkbox" class="cmdAttr" data-l1key="isHistorized"/>{{Historiser}}</label> '
        tr += '</div>'      
       }      
      tr += '</td>'
  
      //Type
  	  tr += '<td>'
  	  tr += '<span class="cmdAttr">';  		
      tr += '<div>'
      tr += _cmd.type;
  	  tr += ' ('
  	  tr += _cmd.configuration.type;
      tr += ')'
      tr += '</div>'        
  	  tr += '</span>'
  	  tr += '</td>'
  
      // Column of gesture action types, to the LEFT of normal parameters.
      tr += '<td style="width:138px;vertical-align:middle">'
      if (isMultiAction) {
        tr += '<select class="cmdAttr mira-multi-kind form-control input-sm" data-l1key="configuration" data-l2key="multi_mode" style="width:125px;max-width:100%">' +
          '<option value="none">Aucune</option><option value="scenario">Scénario</option><option value="light">Lumière</option></select>'
      }
      tr += '</td>'

      //Paramètres
      tr += '<td>'
      if (isMultiMaster) {
        tr += '<div class="mira-multi-input" style="display:flex;align-items:center;gap:7px;flex-wrap:wrap">' +
          '<span>Entrée :</span><select class="cmdAttr form-control input-sm" data-l1key="configuration" data-l2key="mira_input_gpio" style="width:110px">' +
          '<option value="">-- GPIO --</option>' +
          [10,11,12,13,14,21].map(function(p){return '<option value="'+p+'">GPIO'+p+'</option>';}).join('') +
          '</select><small>Contact sec vers GND (pull-up).</small></div>';
      }

      if (isMultiAction) {
        // Same sizing/alignment as the regular Jeedom command selector above.
        // No extra labels: one field per line, ordered like a native toggle
        // (info state, OFF, ON); only the placeholder identifies an empty field.
        function multiField(key,kind,placeholder) {
          var html='<div style="display:flex;align-items:center;width:100%;margin-bottom:3px;">';
          html+='<textarea class="cmdAttr input-sm" data-l1key="configuration" data-l2key="'+key+
            '" style="height:35px;width:85%;min-width:0;flex:1 1 85%" placeholder="'+placeholder+'"></textarea>';
          html+='<a class="btn btn-default '+(kind==='info'?'listEquipementInfo':'listEquipementAction')+
            '" data-input="'+key+'" style="margin-left:2px;height:35px;width:14%;min-width:35px;flex:0 0 14%;display:flex;align-items:center;justify-content:center">'+
            '<i class="fas fa-list-alt"></i></a></div>';
          return html;
        }
        tr += '<div class="mira-multi-scenario">'+multiField('calcul','action','Envoyer vers')+'</div>';
        tr += '<div class="mira-multi-light" style="display:none">'+
          multiField('multi_state','info','Recevoir de (état)')+
          multiField('multi_off','action','Envoyer vers OFF')+
          multiField('multi_on','action','Envoyer vers ON')+'</div>';
      }
      if (miraLocalEligible) {
        tr += '<div class="mira-gpio-params" style="display:none;gap:5px;align-items:center">' +
            '<select class="cmdAttr form-control input-sm" data-l1key="configuration" data-l2key="mira_gpio" style="width:110px"><option value="">-- GPIO --</option>' +
            [10,11,12,13,14,21].map(function(p){ return '<option value="'+p+'">GPIO'+p+'</option>'; }).join('') + '</select>';
        if (miraLocalAction) tr += '<select class="cmdAttr form-control input-sm" data-l1key="configuration" data-l2key="mira_level" style="width:85px"><option value="high">HIGH</option><option value="low">LOW</option></select>';
        else tr += '<span style="font-size:12px">Lire état</span>';
        tr += '</div><div class="mira-jeedom-params">';
      }			
  	  if (_cmd.type == "info" && init(_cmd.logicalId) != 'Statut' ){
        tr += '<div style="display: none">'
        tr += _cmd.configuration.calcul;
        tr += '</div>'
    	tr += '<textarea class="cmdAttr  input-sm" data-l1key="configuration" data-l2key="calcul" style="height:35px;width:85%" placeholder="{{Recevoir de}}"></textarea>'
      	tr += '<a class="btn btn-default listEquipementInfo " data-input="calcul" style="margin-left:2px; height:35px; width:14%;"><i class="fas fa-list-alt"></i></a>'
      }
  	  if (_cmd.type == "action" && !isMultiMaster && !isMultiAction && init(_cmd.logicalId) != 'Redémarrer' && init(_cmd.logicalId) != 'refresh'){
        if (_cmd.configuration.type == 'list' && init(_cmd.configuration.listValue) != '') {
          // One Jeedom action target per Mira list option.
          var listItems = _cmd.configuration.listValue.split(';')
          for (var li = 0; li < listItems.length; li++) {
            if (listItems[li] == '') continue
            var sep = listItems[li].indexOf('|')
            if (sep < 0) continue
            var optionId = listItems[li].substring(0, sep)
            var optionName = listItems[li].substring(sep + 1)
            var cfgKey = miraListOptionKey(optionName)
            var legacyKey = 'listAction_' + optionId
            /* Migration transparente du premier patch base sur l'ID. */
            if (init(_cmd.configuration[cfgKey]) == '' && init(_cmd.configuration[legacyKey]) != '') {
              _cmd.configuration[cfgKey] = _cmd.configuration[legacyKey]
            }
            tr += '<div style="display:flex;align-items:center;margin-bottom:3px;">'
            tr += '<span style="width:90px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;" title="' + optionName + '">' + optionName + '</span>'
            tr += '<textarea class="cmdAttr input-sm" data-l1key="configuration" data-l2key="' + cfgKey + '" style="height:35px;width:72%" placeholder="{{Envoyer vers}} ' + optionName + '"></textarea>'
            tr += '<a class="btn btn-default listEquipementAction" data-input="' + cfgKey + '" style="margin-left:2px;height:35px;width:14%;"><i class="fas fa-list-alt"></i></a>'
            tr += '</div>'
          }
          // Hidden legacy target kept for old action/select mappings.
          tr += '<textarea class="cmdAttr" data-l1key="configuration" data-l2key="calcul" style="display:none"></textarea>'
        } else {
          tr += '<div style="display: none">'
          tr += _cmd.configuration.calcul;
          tr += '</div>'
          tr += '<textarea class="cmdAttr input-sm" data-l1key="configuration" data-l2key="calcul" style="height:35px;width:85%" placeholder="{{Envoyer vers}}"></textarea>'
          tr += '<a class="btn btn-default listEquipementAction" data-input="calcul" style="margin-left:2px;height:35px;width:14%;"><i class="fas fa-list-alt"></i></a>'
        }
      }
      if (miraLocalEligible) tr += '</div>';
      tr += '</td>'

      //Etat
      tr += '<td>';
      tr += '<span class="cmdAttr" data-l1key="htmlstate"></span>';
      tr += '</td>';
  
      //Actions
      tr += '<td>'
  	  tr += '<div style="display: none">'
      tr += _cmd.type;
      tr += '</div>'
      if (is_numeric(_cmd.id)) {
        tr += '<a class="btn btn-default btn-xs cmdAction" data-action="configure"><i class="fas fa-cogs"></i></a> '
        //tr += '<a class="btn btn-default btn-xs cmdAction" data-action="test"><i class="fas fa-rss"></i> {{Tester}}</a>'
      }
      tr += '<i class="fas fa-minus-circle pull-right cmdAction cursor" data-action="remove" title="{{Supprimer la commande}}"></i></td>'
      tr += '</tr>'
     
      $('#table_cmd tbody').append(tr)
      var tr = $('#table_cmd tbody tr').last()
      // Command identity stored safely on DOM; never embed names into HTML attributes.
      tr.attr('data-mira-logicalid',_cmd.logicalId || '');
      if (isMultiAction) tr.attr('data-mira-parent',_cmd.configuration.multi_parent || '');
      jeedom.eqLogic.buildSelectCmd({
        id: $('.eqLogicAttr[data-l1key=id]').value(),
        filter: { type: 'info' },
        error: function (error) {
          $('#div_alert').showAlert({ message: error.message, level: 'danger' })
        },
        success: function (result) {
          tr.find('.cmdAttr[data-l1key=value]').append(result)
          tr.setValues(_cmd, '.cmdAttr');
          miraRefreshGpioParams(tr);
          jeedom.cmd.changeType(tr, init(_cmd.subType))
          miraMultiRefreshRow(tr);
          if (isMultiMaster) miraMultiSourceChanged(tr);
        }
      })
  
}

/* Mira 1.2.13 — détection universelle du profil écran via /displayinfo. */
function MiraNormalizeDisplayInfo(info){
  if(!info || typeof info!=='object') throw new Error('profil écran absent');

  var width=parseInt(info.width,10), height=parseInt(info.height,10);
  var contentWidth=parseInt(info.contentWidth,10), contentHeight=parseInt(info.contentHeight,10);
  var navHeight=parseInt(info.navHeight,10);
  var logicalWidth=parseInt(info.logicalWidth,10), logicalHeight=parseInt(info.logicalHeight,10);

  if(!Number.isFinite(width) || !Number.isFinite(height) || width<160 || height<120 || width>4096 || height>4096){
    throw new Error('résolution écran invalide');
  }
  if(!Number.isFinite(contentWidth) || contentWidth<=0 || contentWidth>width) contentWidth=width;
  if(!Number.isFinite(contentHeight) || contentHeight<=0 || contentHeight>height) contentHeight=height;
  if(!Number.isFinite(navHeight) || navHeight<0 || navHeight>height) navHeight=Math.max(0,height-contentHeight);
  if(!Number.isFinite(logicalWidth) || logicalWidth<=0) logicalWidth=1000;
  if(!Number.isFinite(logicalHeight) || logicalHeight<=0) logicalHeight=1000;

  return $.extend({},info,{
    profile:MiraSafeText(info.profile).trim() || 'mira-generic',
    width:width,
    height:height,
    contentWidth:contentWidth,
    contentHeight:contentHeight,
    navHeight:navHeight,
    logicalWidth:logicalWidth,
    logicalHeight:logicalHeight,
    freeLayout:(info.freeLayout===true || info.freeLayout===1 || info.freeLayout==='1')
  });
}

function MiraLoadDisplayInfo(ip, mdp){
  var deferred=$.Deferred();
  MiraDisplayInfoLoaded=false;
  HTTPPost('http://'+ip+'/displayinfo?key='+encodeURIComponent(mdp)).then(function(raw){
    try {
      var parsed=(typeof raw === 'string') ? JSON.parse(raw) : raw;
      var info=MiraNormalizeDisplayInfo(parsed);
      MiraDisplayInfo=$.extend({},MiraDisplayInfo,info);
      MiraDisplayInfoLoaded=true;

      var landscape=info.contentWidth>info.contentHeight;
      $('#PopupDiv').toggleClass('mira-landscape-display',landscape);
      $('#miradisplayprofile').text(
        (info.profile || 'écran Mira')+' — '+info.width+'×'+info.height+
        ' / zone '+info.contentWidth+'×'+info.contentHeight+' — compatible'
      );
      $('#miradesignerbar').css('display','flex');

      if(info.freeLayout){
        $('#mirabuttondesigner').removeClass('disabled').css({'pointer-events':'','opacity':''}).attr('title','');
      }else{
        $('#mirabuttondesigner').addClass('disabled').css({'pointer-events':'none','opacity':'.45'}).attr('title','Placement libre non annoncé par ce firmware');
        $('#miradisplayprofile').append(' — placement libre indisponible');
      }
      deferred.resolve(info);
    } catch(e){ deferred.reject(e); }
  },function(err){ deferred.reject(err); });
  deferred.fail(function(err){
    MiraDisplayInfoLoaded=false;
    $('#PopupDiv').removeClass('mira-landscape-display');
    $('#miradesignerbar').hide();
    console.warn('Mira: /displayinfo indisponible ou invalide, éditeur classique conservé',err);
  });
  return deferred.promise();
}

function MiraGetRows(){
  var table=document.getElementById('miratable'), rows=[];
  if(!table) return rows;
  for(var i=2;i<table.rows.length;i++) rows.push(table.rows[i]);
  return rows;
}

function MiraRowType(row){ return row.cells[0].dataset.value || row.cells[0].innerText.trim(); }
function MiraRowPage(row){ return MiraSafeText(row.cells[2].innerText).trim() || 'accueil'; }
function MiraRowName(row){ return MiraDisplayName(row) || MiraRowType(row); }

function MiraMinWidgetSize(type){
  if(type==='thermostat') return {w:300,h:305};
  if(type==='roundrange' || type==='color') return {w:160,h:160};
  if(type==='range') return {w:180,h:60};
  if(type==='button' || type==='toggle') return {w:150,h:44};
  if(type==='list' || type==='listbutton') return {w:160,h:60};
  if(type==='datetime') return {w:180,h:60};
  if(type==='info') return {w:70,h:40};
  return {w:80,h:40};
}

function MiraDefaultWidgetSize(type){
  var W=MiraDisplayInfo.contentWidth || MiraDisplayInfo.width || 320;
  if(type==='thermostat') return {w:W>=800?786:304,h:W>=800?310:405};
  if(type==='datetime') return {w:Math.max(120,W-16),h:82};
  if(type==='info') return {w:Math.max(90,Math.floor((W-22)/2)),h:66};
  if(type==='toggle') return {w:Math.max(160,W-16),h:54};
  if(type==='button') return {w:Math.max(160,W-16),h:54};
  if(type==='range') return {w:Math.max(180,W-16),h:80};
  if(type==='roundrange') return {w:Math.min(W-16,238),h:238};
  if(type==='color') return {w:Math.min(W-16,210),h:196};
  if(type==='list' || type==='listbutton') return {w:Math.max(180,W-16),h:82};
  return {w:Math.max(140,W-16),h:60};
}

// LVGL 8: lv_obj_set_pos() is relative to the parent CONTENT origin.
// On all three programmable pages Mira sets 6 px horizontal padding and 8 px
// top padding (MiraPanelUI::begin). These offsets were missing from the editor.
// Absolute preview coordinates now describe what is actually visible on glass.
function MiraPageContentPadding(){ return {x:6,y:8}; }

function MiraLogicalToPixels(row){
  var W=MiraDisplayInfo.contentWidth||320, H=MiraDisplayInfo.contentHeight||434;
  var lW=MiraDisplayInfo.logicalWidth||1000, lH=MiraDisplayInfo.logicalHeight||1000;
  var p=MiraPageContentPadding();
  // Firmware: integer scaling, min size, then clamp x to W-w *before*
  // LVGL adds the page-content padding. Preserve existing widget values;
  // only make their on-screen preview correspond to the real firmware.
  var x=Math.max(0,Math.floor((parseInt(row.dataset.lx,10)||0)*W/lW));
  var y=Math.max(0,Math.floor((parseInt(row.dataset.ly,10)||0)*H/lH));
  var w=Math.min(W,Math.max(40,Math.floor((parseInt(row.dataset.lw,10)||0)*W/lW)));
  var h=Math.max(32,Math.floor((parseInt(row.dataset.lh,10)||0)*H/lH));
  if(x+w>W) x=W-w;
  return {x:x+p.x,y:y+p.y,w:w,h:h};
}

function MiraPixelsToLogical(row,g){
  var W=MiraDisplayInfo.contentWidth||320, H=MiraDisplayInfo.contentHeight||434;
  var lW=MiraDisplayInfo.logicalWidth||1000, lH=MiraDisplayInfo.logicalHeight||1000;
  var p=MiraPageContentPadding();
  // Ceil gives exact requested screen pixels when lW>=W and lH>=H.
  // Never silently change coordinates on already programmed widgets.
  row.dataset.lx=String(Math.ceil(Math.max(0,g.x-p.x)*lW/W));
  row.dataset.ly=String(Math.ceil(Math.max(0,g.y-p.y)*lH/H));
  row.dataset.lw=String(Math.ceil(g.w*lW/W));
  row.dataset.lh=String(Math.ceil(g.h*lH/H));
  row.dataset.freeLayout='1';
  var select=row.cells[8].querySelector('.mira-row-layout');
  if(select){ select.value='free'; }
  row.cells[8].dataset.value='free';
}

function MiraPageHasFreeLayout(page, exceptRow){
  return MiraGetRows().some(function(r){
    return r!==exceptRow && MiraRowPage(r)===page && r.dataset.freeLayout==='1';
  });
}

function MiraFreezePageLayout(page){
  var preview=MiraAutoPreviewGeometries(page);
  preview.forEach(function(it){
    if(it.row.dataset.freeLayout!=='1') MiraPixelsToLogical(it.row,it.g);
  });
}

function MiraPlaceRowAfterContent(row){
  // Mira 1.2.16 : premier emplacement réellement libre, au lieu de toujours
  // empiler après le dernier widget manuel (souvent hors écran sur WT32).
  var page=MiraRowPage(row), W=MiraDisplayInfo.contentWidth||320;
  var H=MiraDisplayInfo.contentHeight||434;
  var gap=Math.max(6,Math.round(W/53)), pad=Math.max(6,Math.round(W/53));
  var d=MiraDefaultWidgetSize(MiraRowType(row));
  var w=Math.min(d.w,W-pad*2), h=d.h;
  var others=MiraAutoPreviewGeometries(page).filter(function(it){return it.row!==row;});
  var firstScreenHeight=H, maxHeight=H*2;
  var xs=[Math.max(pad,Math.round((W-w)/2))];
  if(MiraRowType(row)==='info' && page==='accueil'){
    xs=[pad,Math.max(pad,W-pad-w)];
  }
  function free(x,y){
    return others.every(function(it){
      var r=it.g;
      return x+w+gap<=r.x || x>=r.x+r.w+gap || y+h+gap<=r.y || y>=r.y+r.h+gap;
    });
  }
  // Remplir le premier écran avant d'autoriser le deuxième.
  for(var limit of [firstScreenHeight,maxHeight]){
    for(var y=pad;y+h<=limit-pad;y+=gap){
      for(var x of xs){
        if(free(x,y)){
          MiraPixelsToLogical(row,{x:x,y:y,w:w,h:h});
          return true;
        }
      }
    }
  }
  return false;
}

function MiraEnsureRowGeometry(row){
  if(row.dataset.lx!=='' && row.dataset.ly!=='' && row.dataset.lw!=='' && row.dataset.lh!==''){
    row.dataset.freeLayout='1';
    return;
  }
  if(!MiraPlaceRowAfterContent(row)) {
    alert('Aucun espace libre. Déplace ou réduis un widget avant de passer celui-ci en Manuel.');
    return false;
  }
  return true;
}

function MiraAutoPreviewGeometries(page){
  var W=MiraDisplayInfo.contentWidth||320, H=MiraDisplayInfo.contentHeight||434;
  var gap=Math.max(6,Math.round(W/53)), pad=Math.max(6,Math.round(W/53));
  var x=pad,y=pad,rowH=0,out=[];
  MiraGetRows().filter(function(r){ return MiraRowPage(r)===page; }).forEach(function(row){
    if(row.dataset.freeLayout==='1' && row.dataset.lx!=='' && row.dataset.lw!==''){
      var fg=MiraLogicalToPixels(row);
      out.push({row:row,g:fg,free:true});
      x=pad; rowH=0; y=Math.max(y,fg.y+fg.h+gap);
      return;
    }
    var d=MiraDefaultWidgetSize(MiraRowType(row));
    var w=Math.min(d.w,W-pad*2), h=d.h;
    if(MiraRowType(row)==='info' && page==='accueil'){
      if(x+w>W-pad){ x=pad; y+=rowH+gap; rowH=0; }
      out.push({row:row,g:{x:x,y:y,w:w,h:h},free:false});
      x+=w+gap; rowH=Math.max(rowH,h);
    }else{
      if(x!==pad){ x=pad; y+=rowH+gap; rowH=0; }
      out.push({row:row,g:{x:Math.max(pad,Math.round((W-w)/2)),y:y,w:w,h:h},free:false});
      y+=h+gap;
    }
  });
  return out;
}

function MiraDesignerRender(){
  if(!$('#miradesigner').is(':visible')) return;
  var viewport=document.getElementById('mirascreenviewport'), canvas=document.getElementById('mirascreencanvas');
  if(!viewport || !canvas) return;
  var W=MiraDisplayInfo.contentWidth||320, H=MiraDisplayInfo.contentHeight||434;
  var stage=document.querySelector('.mira-designer-stage');
  var frame=document.getElementById('mirascreenframe');
  var maxPreviewWidth=(W>H) ? 600 : 400;
  var requestedWidth=Math.max(240,Math.min(maxPreviewWidth,(stage ? stage.clientWidth : maxPreviewWidth)-4));
  if(frame) frame.style.width=requestedWidth+'px';
  // Stable gutter prevents scrollbar appearance from changing the preview
  // scale after the initial render (important for tall pages like Réseau).
  var previewWidth=viewport.clientWidth || Math.max(1,requestedWidth-18);
  var naturalHeight=Math.round(previewWidth*H/W);
  var maxPageHeight=H*2;
  // Always show one complete screen. Scrolling starts only when content enters
  // the optional second screen, and never extends beyond two screen heights.
  viewport.style.height=naturalHeight+'px';
  var items=MiraAutoPreviewGeometries(MiraDesignerPage);
  var H=MiraDisplayInfo.contentHeight||434;
  var overflows=items.some(function(it){ return it.g.y+it.g.h>H; });
  var hint=document.getElementById('miradesignerhint');
  if(hint) hint.textContent=overflows
    ? 'Attention : certains widgets dépassent la zone visible. Déplace ou réduis les tuiles dans le designer : le firmware peut ne pas les afficher sans défilement.'
    : 'Glisse une tuile pour la placer, agrandis-la pour mettre sa valeur en avant.';
  var maxBottom=H;
  items.forEach(function(it){ maxBottom=Math.max(maxBottom,Math.min(maxPageHeight,it.g.y+it.g.h+8)); });
  maxBottom=Math.min(maxPageHeight,maxBottom);
  canvas.style.height=Math.round(previewWidth*maxBottom/W)+'px';
  // After setting scrollable height, measure again: actual scrollbar width
  // can change clientWidth on browsers without scrollbar-gutter support.
  var finalWidth=viewport.clientWidth || previewWidth;
  if(finalWidth!==previewWidth){
    previewWidth=finalWidth;
    naturalHeight=Math.round(previewWidth*H/W);
    viewport.style.height=naturalHeight+'px';
    canvas.style.height=Math.round(previewWidth*maxBottom/W)+'px';
  }
  MiraDesignerScale=previewWidth/W;
  canvas.style.setProperty('--mira-grid-size',(Math.max(4,Math.round(W/80))*MiraDesignerScale)+'px');
  canvas.innerHTML='';

  items.forEach(function(it){
    var row=it.row,g=it.g,block=document.createElement('div');
    block.className='mira-designer-widget'+(it.free?'':' mira-auto-widget');
    if(it.free && (g.x<0 || g.y<0 || g.x+g.w>W-6 || g.y+g.h>maxPageHeight)){
      block.classList.add('mira-widget-clipped');
      block.title='Attention : vérifie la marge droite de la tuile sur l’écran';
    }
    block.dataset.rowIndex=String(row.rowIndex);
    block.style.left=(g.x*MiraDesignerScale)+'px';
    block.style.top=(g.y*MiraDesignerScale)+'px';
    block.style.width=(g.w*MiraDesignerScale)+'px';
    block.style.height=(g.h*MiraDesignerScale)+'px';
    block.innerHTML='<div class="mira-widget-preview-content"><strong></strong><span></span></div><div class="mira-resize-handle" title="Redimensionner"></div>';
    block.querySelector('strong').textContent=MiraRowName(row);
    block.querySelector('span').textContent=MiraRowType(row)+(it.free?' · libre':' · auto');
    block.addEventListener('pointerdown',MiraDesignerPointerDown);
    block.addEventListener('click',function(ev){ ev.stopPropagation(); MiraDesignerSelect(block); });
    canvas.appendChild(block);
    if(MiraDesignerSelected && Number(MiraDesignerSelected.dataset.rowIndex)===row.rowIndex) MiraDesignerSelected=block;
  });
  canvas.onclick=function(){ MiraDesignerSelect(null); };
  if(MiraDesignerSelected && document.body.contains(MiraDesignerSelected)) MiraDesignerSelect(MiraDesignerSelected);
  else MiraDesignerSelect(null);
}

function MiraDesignerBlockGeometry(block){
  return {
    x:Math.round(parseFloat(block.style.left)/MiraDesignerScale),
    y:Math.round(parseFloat(block.style.top)/MiraDesignerScale),
    w:Math.round(parseFloat(block.style.width)/MiraDesignerScale),
    h:Math.round(parseFloat(block.style.height)/MiraDesignerScale)
  };
}

function MiraDesignerApplyBlockGeometry(block,g,commit,options){
  options=options||{};
  var W=MiraDisplayInfo.contentWidth||320, H=MiraDisplayInfo.contentHeight||434;
  var maxPageHeight=H*2, grid=Math.max(4,Math.round(W/80));
  var pad=MiraPageContentPadding();
  function snap(v){ return options.exact || !MiraDesignerGridEnabled ? Math.round(v) : Math.round(v/grid)*grid; }
  // Dragging can be constrained; manual editing is validated separately so
  // typing a new dimension never silently alters another field.
  g.x=Math.max(pad.x,snap(g.x)); g.y=Math.max(pad.y,snap(g.y));
  var table=document.getElementById('miratable'), row=table.rows[Number(block.dataset.rowIndex)];
  var min=MiraMinWidgetSize(row ? MiraRowType(row) : 'info');
  g.w=Math.max(min.w,snap(g.w)); g.h=Math.max(min.h,snap(g.h));
  if(g.w>W-pad.x*2) g.w=W-pad.x*2;
  if(g.h>maxPageHeight-pad.y) g.h=maxPageHeight-pad.y;
  // For a resize, keep its anchor fixed and shrink only the growing edge.
  if(options.resize){
    if(g.x+g.w>W-pad.x) g.w=W-pad.x-g.x;
    if(g.y+g.h>maxPageHeight) g.h=maxPageHeight-g.y;
  } else {
    if(g.x+g.w>W-pad.x) g.x=Math.max(pad.x,W-pad.x-g.w);
    if(g.y+g.h>maxPageHeight) g.y=Math.max(pad.y,maxPageHeight-g.h);
  }
  if(g.w<min.w || g.h<min.h) return;
  block.style.left=(g.x*MiraDesignerScale)+'px'; block.style.top=(g.y*MiraDesignerScale)+'px';
  block.style.width=(g.w*MiraDesignerScale)+'px'; block.style.height=(g.h*MiraDesignerScale)+'px';
  if(commit){
    if(row){
      if(row.dataset.freeLayout!=='1') MiraFreezePageLayout(MiraRowPage(row));
      MiraPixelsToLogical(row,g);
      // Show exactly what the firmware will draw (integer normalization).
      g=MiraLogicalToPixels(row);
      block.style.left=(g.x*MiraDesignerScale)+'px'; block.style.top=(g.y*MiraDesignerScale)+'px';
      block.style.width=(g.w*MiraDesignerScale)+'px'; block.style.height=(g.h*MiraDesignerScale)+'px';
      $('#miraboutonenvoyer').show();
    }
    block.classList.remove('mira-auto-widget');
    block.classList.toggle('mira-widget-clipped',g.x+g.w>W-pad.x || g.y+g.h>maxPageHeight);
    MiraDesignerNumericDirty=false;
    MiraDesignerUpdateInspector(block,g);
    var canvas=document.getElementById('mirascreencanvas'), frameW=canvas.clientWidth;
    var need=Math.min(H*2,Math.max(H,g.y+g.h+8));
    // Never trim the canvas below the other widgets' extents.
    var other=MiraAutoPreviewGeometries(MiraDesignerPage);
    other.forEach(function(it){ need=Math.max(need,Math.min(H*2,it.g.y+it.g.h+8)); });
    canvas.style.height=Math.round(frameW*Math.min(H*2,need)/(MiraDisplayInfo.contentWidth||320))+'px';
  }
}
function MiraDesignerPointerDown(ev){
  if(ev.button!==undefined && ev.button!==0) return;
  var block=ev.currentTarget, resize=ev.target.classList.contains('mira-resize-handle');
  var thermoRow=document.getElementById('miratable').rows[Number(block.dataset.rowIndex)];
  // Native thermostat chooses its own safe geometry. Manual shrinking breaks
  // the built-in arc + temperature + mode composition on WT32.
  if(thermoRow && MiraRowType(thermoRow)==='thermostat'){
    MiraDesignerSelect(block); return;
  }
  var viewport=document.getElementById('mirascreenviewport');
  MiraDesignerSelect(block);
  var start=MiraDesignerBlockGeometry(block), sx=ev.clientX, sy=ev.clientY;
  var startScrollTop=viewport ? viewport.scrollTop : 0;
  block.setPointerCapture(ev.pointerId);

  function autoScroll(pointerY){
    if(!viewport) return;
    var r=viewport.getBoundingClientRect(), edge=44, step=0;
    if(pointerY < r.top+edge) step=-Math.max(4,Math.ceil((r.top+edge-pointerY)/4));
    else if(pointerY > r.bottom-edge) step=Math.max(4,Math.ceil((pointerY-(r.bottom-edge))/4));
    if(step){
      var maxScroll=Math.max(0,viewport.scrollHeight-viewport.clientHeight);
      viewport.scrollTop=Math.max(0,Math.min(maxScroll,viewport.scrollTop+step));
    }
  }

  function move(e){
    autoScroll(e.clientY);
    var scrollDelta=viewport ? (viewport.scrollTop-startScrollTop) : 0;
    var dx=(e.clientX-sx)/MiraDesignerScale;
    var dy=((e.clientY-sy)+scrollDelta)/MiraDesignerScale;
    var g=$.extend({},start);
    if(resize){ g.w=start.w+dx; g.h=start.h+dy; }
    else { g.x=start.x+dx; g.y=start.y+dy; }
    MiraDesignerApplyBlockGeometry(block,g,false,{resize:resize});
  }
  function up(e){
    block.removeEventListener('pointermove',move); block.removeEventListener('pointerup',up); block.removeEventListener('pointercancel',up);
    MiraDesignerApplyBlockGeometry(block,MiraDesignerBlockGeometry(block),true,{resize:resize});
    try{ block.releasePointerCapture(e.pointerId); }catch(_e){}
  }
  block.addEventListener('pointermove',move); block.addEventListener('pointerup',up); block.addEventListener('pointercancel',up);
  ev.preventDefault();
}

function MiraDesignerSelect(block){
  document.querySelectorAll('.mira-designer-widget.selected').forEach(function(el){ el.classList.remove('selected'); });
  MiraDesignerSelected=block;
  if(!block){ MiraDesignerNumericDirty=false; $('#mirainspectempty').show(); $('#mirainspectfields').hide(); return; }
  block.classList.add('selected');
  var table=document.getElementById('miratable'), row=table.rows[Number(block.dataset.rowIndex)];
  if(!row) return;
  $('#mirainspectempty').hide(); $('#mirainspectfields').show();
  $('#mirainspectname').text(MiraRowName(row)); $('#mirainspecttype').text(MiraRowType(row));
  var min=MiraMinWidgetSize(MiraRowType(row));
  $('#mirageow').attr('min',min.w); $('#mirageoh').attr('min',min.h);
  MiraDesignerNumericDirty=false;
  MiraDesignerUpdateInspector(block,MiraDesignerBlockGeometry(block));
}


function MiraDesignerGeometryMessage(message,invalid){
  var status=$('#mirageovalidation');
  status.text(message||'').toggleClass('invalid',!!invalid);
}

function MiraDesignerUpdateInspector(block,g){
  if(!block || MiraDesignerNumericDirty) return;
  $('#mirageox').val(g.x); $('#mirageoy').val(g.y);
  $('#mirageow').val(g.w); $('#mirageoh').val(g.h);
  var H=MiraDisplayInfo.contentHeight||434;
  $('#mirageounit').text('Position réelle en px · '+(MiraDisplayInfo.contentWidth||320)+'×'+H+
    (g.y+g.h>H?' · zone de défilement':'')+' · jusqu’à '+(H*2)+' px.');
  MiraDesignerGeometryMessage('',false);
}

function MiraDesignerNumericChange(){
  if(!MiraDesignerSelected) return false;
  var row=document.getElementById('miratable').rows[Number(MiraDesignerSelected.dataset.rowIndex)];
  if(row && MiraRowType(row)==='thermostat') return false;
  var ids=['mirageox','mirageoy','mirageow','mirageoh'];
  var values=[];
  for(var i=0;i<ids.length;i++){
    var textValue=document.getElementById(ids[i]).value.trim();
    if(!/^(0|[1-9][0-9]*)$/.test(textValue)){
      MiraDesignerGeometryMessage('Entre un nombre entier positif dans chaque champ, puis applique.',true);
      return false;
    }
    values.push(Number(textValue));
  }
  var g={x:values[0],y:values[1],w:values[2],h:values[3]};
  var W=MiraDisplayInfo.contentWidth||320,H=MiraDisplayInfo.contentHeight||434;
  var p=MiraPageContentPadding(),min=MiraMinWidgetSize(row ? MiraRowType(row) : 'info');
  if(g.x<p.x || g.y<p.y || g.w<min.w || g.h<min.h ||
     g.x+g.w>W-p.x || g.y+g.h>H*2){
    MiraDesignerGeometryMessage(
      'Hors limites : X ≥ '+p.x+', Y ≥ '+p.y+', L ≥ '+min.w+', H ≥ '+min.h+
      ' ; X+L ≤ '+(W-p.x)+' et Y+H ≤ '+(H*2)+'. Rien n’a été changé.',true);
    return false;
  }
  // Hand-entered values always bypass grid snapping. Commit only on
  // change/Enter/Apply, never from an input event during partial typing.
  MiraDesignerNumericDirty=false;
  MiraDesignerApplyBlockGeometry(MiraDesignerSelected,g,true,{exact:true});
  MiraDesignerGeometryMessage('Coordonnées appliquées, sans aimantation à la grille.',false);
  return true;
}
$('#mirabuttondesigner').on('click',function(){
  if(!MiraDisplayInfo.freeLayout) return;
  $('#PopupDiv').addClass('designer-open'); $('#miraClassicEditor').hide(); $('#miradesigner').show();
  MiraDesignerPage='accueil'; $('.mira-page-tab').removeClass('active').filter('[data-page="accueil"]').addClass('active');
  setTimeout(MiraDesignerRender,0);
});
$('#miradesignerclose').on('click',function(){ $('#miradesigner').hide(); $('#miraClassicEditor').show(); $('#PopupDiv').removeClass('designer-open'); });
$('#miradesignersave').on('click',function(){
  var ip = $('#miraip').val();
  var mdp = $('#miramdp').val();
  if(!ip || !mdp){ alert("Renseigne l'adresse IP et le mot de passe de l'écran."); return; }
  HTTPTestModuleSend('http://'+ip+'/testmodule?key='+mdp,ip,mdp);
});
$(document).on('click','.mira-page-tab',function(){ MiraDesignerPage=$(this).data('page'); $('.mira-page-tab').removeClass('active'); $(this).addClass('active'); MiraDesignerSelected=null; MiraDesignerRender(); });
$('#miradesignergrid').on('click',function(){ MiraDesignerGridEnabled=!MiraDesignerGridEnabled; $(this).toggleClass('active',MiraDesignerGridEnabled); });
// Input: only mark the draft as edited. Never mutate widget position or the
// input's contents mid-keystroke (e.g. replacing 5 by 150).
$('#mirageox,#mirageoy,#mirageow,#mirageoh').on('input',function(){
  MiraDesignerNumericDirty=true;
  MiraDesignerGeometryMessage('Saisie en cours — Entrée ou Appliquer pour valider.',false);
}).on('keydown',function(ev){
  if(ev.key==='Enter'){
    ev.preventDefault();
    MiraDesignerNumericChange();
    this.blur();
  }
});
$('#mirageoapply').on('click',MiraDesignerNumericChange);
// Keep manual entry stable when tabbing among the four fields. Only apply
// after leaving the whole group, or via Enter/Apply.
$('#mirageox,#mirageoy,#mirageow,#mirageoh').on('focusout',function(){
  setTimeout(function(){
    var focused=document.activeElement;
    if(focused && focused.closest && focused.closest('.mira-geometry-grid')) return;
    if(MiraDesignerNumericDirty) MiraDesignerNumericChange();
  },0);
});
$('#miradesignerauto').on('click',function(){
  if(!MiraDesignerSelected) return;
  var row=document.getElementById('miratable').rows[Number(MiraDesignerSelected.dataset.rowIndex)];
  if(!row) return;
  row.dataset.freeLayout='0'; row.dataset.lx=row.dataset.ly=row.dataset.lw=row.dataset.lh='';
  var select=row.cells[8].querySelector('.mira-row-layout'); if(select) select.value='auto';
  row.cells[8].dataset.value='auto'; $('#miraboutonenvoyer').show(); MiraDesignerSelected=null; MiraDesignerRender();
});
$(window).on('resize',function(){ if($('#miradesigner').is(':visible')) MiraDesignerRender(); });

// Preview Jeedom's automatic icon detection while typing a new Info widget.
$('#miraname,#miraunit').on('input change',function(){
  if($('#miratype').val()==='info') MiraRefreshInfoSelectors('info');
});

$('#miralabelmode').on('change',function(){ MiraRefreshInfoSelectors($('#miratype').val()); });


var MiraQuickRow=null;
function MiraOpenQuickOptions(row){
  MiraQuickRow=row;
  var type=MiraRowType(row);
  $('#miraquicktitle').text('Modifier · '+MiraDisplayName(row));
  $('#miraquickname').val(MiraDisplayName(row));
  $('#miraquickunit').val(MiraCellValue(row.cells[4]));
  $('#miraquickthermomodes').val(row.dataset.thermoModes || 'Confort|Éco|Hors gel|Arrêt');
  $('#miraquickthermomodeswrap').toggle(type==='thermostat');
  $('#miraquickmin').val(MiraCellValue(row.cells[5]));
  $('#miraquickmax').val(MiraCellValue(row.cells[6]));
  $('#miraquickvalue').val(MiraCellValue(row.cells[7]));
  $('#miraquickunitwrap').toggle(type==='info' || type==='roundrange');
  $('#miraquickminmaxwrap').toggle(type==='range' || type==='roundrange' || type==='thermostat');
  $('#miraquickvaluewrap').toggle(type==='list' || type==='listbutton' || type==='range' || type==='roundrange' || type==='thermostat');
  $('#miraquickvaluetitle').text(type==='list' || type==='listbutton' ? 'Options séparées par |' : type==='thermostat' ? 'Consigne initiale (pas 0,5 °C)' : 'Valeur de départ');
  $('#miraquickoptions').css('display','flex');
}
function MiraCloseQuickOptions(){ MiraQuickRow=null; $('#miraquickoptions').hide(); }
$('#miraquickcancel').on('click',MiraCloseQuickOptions);
$('#miraquickoptions').on('click',function(ev){ if(ev.target===this) MiraCloseQuickOptions(); });
$('#miraquicksave').on('click',function(){
  var row=MiraQuickRow; if(!row) return;
  var display=$('#miraquickname').val().trim();
  if(!display){ alert('Le nom affiché ne peut pas être vide.'); return; }
  var type=MiraRowType(row), min=$('#miraquickmin').val(), max=$('#miraquickmax').val();
  if((type==='range' || type==='roundrange' || type==='thermostat') && (!Number.isFinite(Number(min)) || !Number.isFinite(Number(max)) || min==='' || max==='' || Number(min)>=Number(max))){
    alert('Le minimum doit être inférieur au maximum.'); return;
  }
  var value=$('#miraquickvalue').val();
  if((type==='list' || type==='listbutton') && !value.split('|').some(function(x){return x.trim();})){
    alert('Indique au moins une option de liste.'); return;
  }
  MiraSetRowName(row,MiraTechnicalName(row),display);
  if(type==='info' || type==='roundrange') row.cells[4].textContent=$('#miraquickunit').val();
  if(type==='thermostat'){
    var modes=$('#miraquickthermomodes').val().trim();
    if(!modes.split('|').some(function(x){return x.trim();})){ alert('Indique au moins un mode.'); return; }
    if(!Number.isFinite(Number(value))){ alert('Consigne invalide.'); return; }
    row.dataset.thermoModes=modes;
  }
  if(type==='range' || type==='roundrange' || type==='thermostat'){
    var minField=row.cells[5].querySelector('input'), maxField=row.cells[6].querySelector('input');
    if(minField) minField.value=min; else row.cells[5].textContent=min;
    if(maxField) maxField.value=max; else row.cells[6].textContent=max;
    row.cells[7].textContent=value;
  }
  if(type==='list' || type==='listbutton') row.cells[7].textContent=value;
  $('#miraboutonenvoyer').show();
  if($('#miradesigner').is(':visible')) MiraDesignerRender();
  MiraCloseQuickOptions();
});


// Favoris 1.2.23: Jeedom remplit les sélecteurs à chaque ouverture d'un
 // équipement. Les events synthétiques ne doivent jamais effacer les choix.
 // Supprimer aussi le vieux handler 1.2.20 non namespacé: Jeedom charge des
 // pages sans forcément recharger le document et peut garder ses événements.
 $(document).off('change', '.mira-nav-favorite');
 $(document).off('focusin.miraNavFavorites', '.mira-nav-favorite');
 $(document).on('focusin.miraNavFavorites', '.mira-nav-favorite', function(){
   $(this).data('miraPreviousFavorite', $(this).val());
 });
 $(document).on('change.miraNavFavorites', '.mira-nav-favorite', function(event){
   var nativeEvent = event.originalEvent;
   if (!nativeEvent || nativeEvent.isTrusted !== true) return;
   var choice = $(this).val();
   if (choice === '255' || choice === '') {
     $('#miraFavoritesSyncStatus').text('Enregistre l’équipement pour synchroniser les nouveaux favoris.');
     return;
   }
   var duplicate = false;
   $('.mira-nav-favorite').not(this).each(function(){
     if ($(this).val() === choice) duplicate = true;
   });
   if (duplicate) {
     var previous = $(this).data('miraPreviousFavorite');
     $(this).val(previous !== undefined && previous !== choice ? previous : '255');
     $('#miraFavoritesSyncStatus').text('Page déjà utilisée dans un autre favori. Choisis une autre page ou Aucun.');
   } else {
     $(this).data('miraPreviousFavorite', choice);
     $('#miraFavoritesSyncStatus').text('Enregistre l’équipement pour synchroniser les nouveaux favoris.');
   }
 });

$('#miraSyncFavorites').on('click', function(){
  var id = $('.eqLogicAttr[data-l1key="id"]').val();
  if (!id) {
    alert('Enregistre d’abord ton équipement Mira Panel.');
    return;
  }
  var button = $(this), status = $('#miraFavoritesSyncStatus');
  button.prop('disabled', true);
  status.text('Synchronisation des favoris enregistrés…');
  $.ajax({
    type:'POST',
    url:'plugins/mira/core/ajax/mira.ajax.php',
    data:{action:'SyncNavigationFavorites',id:id},
    dataType:'json',timeout:12000
  }).done(function(response){
    if(response && response.state === 'ok'){
      status.text('Favoris synchronisés avec l’écran.');
    }else{
      var err = response && response.result ? response.result : 'Réponse Jeedom inattendue';
      status.text('Synchronisation impossible : '+String(err).slice(0,170));
    }
  }).fail(function(xhr){
    var error = xhr.responseJSON && xhr.responseJSON.result || xhr.statusText || 'Écran injoignable';
    status.text('Synchronisation impossible : '+String(error).slice(0,170));
  }).always(function(){button.prop('disabled',false);});
});


// WT32 GPIO DEV3 — commandes ON et OFF independantes.
function miraRefreshGpioParams(tr) {
  var source=tr.find('.mira-gpio-source').val() || 'jeedom';
  tr.find('.mira-gpio-params').css('display',source==='gpio'?'flex':'none');
  tr.find('.mira-jeedom-params').toggle(source!=='gpio');
}
$(document).off('change.mira_gpio', '#table_cmd .mira-gpio-source')
.on('change.mira_gpio', '#table_cmd .mira-gpio-source', function() {
  miraRefreshGpioParams($(this).closest('tr'));
});



// Multi-actions beta.7 — classic Jeedom rows, no giant master-row editor.
function miraMultiRefreshRow(tr) {
  var mode=tr.find('.mira-multi-kind').val();
  if (!mode)return;
  tr.find('.mira-multi-scenario').toggle(mode==='scenario');
  tr.find('.mira-multi-light').toggle(mode==='light');
}
function miraMultiSourceChanged(tr){
  var trigger=tr.find('.mira-multi-trigger').val() || 'gpio';
  tr.find('.mira-multi-input').toggle(trigger!=='jeedom');
}
$(document).off('change.mira_multi','#table_cmd .mira-multi-trigger')
  .on('change.mira_multi','#table_cmd .mira-multi-trigger',function(){
    miraMultiSourceChanged($(this).closest('tr'));
  });
$(document).off('change.mira_multi_kind','#table_cmd .mira-multi-kind')
  .on('change.mira_multi_kind','#table_cmd .mira-multi-kind',function(){
    miraMultiRefreshRow($(this).closest('tr'));
  });
