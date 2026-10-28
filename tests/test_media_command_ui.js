// Regression: compact media presentation must not change command identity.
// Run with: node tests/test_media_command_ui.js
const fs=require('fs');
const assert=require('assert');
const src=fs.readFileSync(__dirname+'/../desktop/js/mira.js','utf8');
const page=fs.readFileSync(__dirname+'/../desktop/php/mira.php','utf8');
const css=fs.readFileSync(__dirname+'/../desktop/css/mira.css','utf8');
function block(begin,end){
  const i=src.indexOf(begin),j=src.indexOf(end,i);
  assert(i>=0 && j>i,'Missing source block '+begin);
  return src.slice(i,j);
}
const helper=block('function miraMediaPrefixes(rows) {','var miraMediaExpanded={};');
const fakeJQuery=(tr)=>({attr:(key)=>tr[key]});
const getRows=(values)=>({each(fn){values.forEach(function(tr){fn.call(tr);});}});
const getHelpers=new Function('$',helper+'; return {miraMediaPrefixes,miraMediaFieldForRow,miraMediaRowVisible};');
const h=getHelpers(fakeJQuery);
const row=(logical)=>({'data-mira-logicalid':logical});
const rows=getRows(['Maison__title','Maison__artist','Maison__cover',
  'Value_Maison__volume_cmd','Maison__next','Home__solar',
  'Autre__title','Autre__artist'].map(row));
const prefixes=h.miraMediaPrefixes(rows);
assert.deepStrictEqual(prefixes,['Maison','Autre']);
assert.strictEqual(h.miraMediaFieldForRow('Maison__title',prefixes),'title');
assert.strictEqual(h.miraMediaFieldForRow('Value_Maison__volume_cmd',prefixes),'volume_cmd');
assert.strictEqual(h.miraMediaFieldForRow('Maison__device_cmd',prefixes),'device_cmd');
assert.strictEqual(h.miraMediaFieldForRow('Home__solar',prefixes),'');
assert.strictEqual(h.miraMediaFieldForRow('Autre__artist',prefixes),'artist');
assert.strictEqual(h.miraMediaRowVisible('title',true,false),true);
assert.strictEqual(h.miraMediaRowVisible('artist',true,false),true);
assert.strictEqual(h.miraMediaRowVisible('cover',true,false),true);
assert.strictEqual(h.miraMediaRowVisible('playpause',true,false),false);
assert.strictEqual(h.miraMediaRowVisible('playpause',true,true),true);
assert.strictEqual(h.miraMediaRowVisible('device_cmd',true,true),true);
assert.strictEqual(h.miraMediaRowVisible('device_cmd',false,false),false);
assert.strictEqual(h.miraMediaRowVisible('playpause',false,false),true);
const refresh=block('function miraRefreshMediaCommandRows() {','// Energy battery rows are real indexed Mira infos.');
assert(refresh.includes("tr.toggle(!originallyHidden && miraMediaRowVisible(field,auto,expanded));"));
assert(refresh.includes("tr.find('.mira-media-manual-field').toggle(!auto);"));
assert(refresh.includes("tr.children('td').eq(8)"));
assert(refresh.includes("miraMediaExpanded[prefix]=!miraMediaExpanded[prefix]"));
assert(refresh.includes("aria-expanded"));
assert(!/\\.remove\\(\\)|\\.empty\\(\\).*#table_cmd/.test(refresh),'Never delete Jeedom command rows');
assert(!page.includes('id="miraMediaAutoDetails"'));
assert(!page.includes('id="miraMediaShowTechnical"'));
assert(!page.includes('id="miraMediaCommandBar"'));
assert(page.includes('<option value="">{{Configuration manuelle}}</option>'));
assert(css.includes('#table_cmd .mira-media-toggle-links'));
assert(src.includes('data-l2key="camera_url"') && src.includes('mira-camera-url'));
console.log('PASS: Spotify keeps full title/artist/cover rows and expands the other existing commands inline');
console.log('PASS: manual configuration displays essential command rows with normal Jeedom associations');
console.log('PASS: no extra equipment or command-panel settings; identities and camera styling preserved');
