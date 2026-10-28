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
const helper=block('function miraMediaPrefixes(rows) {','function miraRefreshMediaCommandRows() {');
const fakeJQuery=(tr)=>({attr:(key)=>tr[key]});
const getRows=(values)=>({each(fn){values.forEach(function(tr){fn.call(tr);});}});
const getHelpers=new Function('$',helper+'; return {miraMediaPrefixes,miraMediaFieldForRow};');
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
const refresh=block('function miraRefreshMediaCommandRows() {','// Energy battery rows are real indexed Mira infos.');
assert(refresh.includes('var auto=!!$(\'#miraMediaSourceEq\').val();'));
assert(refresh.includes("tr.toggle(visible);"));
assert(refresh.includes("tr.attr('data-mira-origin-hidden')==='1'"));
assert(refresh.includes("tr.find('.mira-media-manual-field').toggle(!auto);"));
assert(refresh.includes('details.forEach'));
assert(!/\.remove\(\)|\.empty\(\).*#table_cmd/.test(refresh),'Presentation must never delete Jeedom command rows');
assert(src.includes('data-l2key="camera_url"') && src.includes('mira-camera-url'));
assert(!src.includes('<input type="url" class="cmdAttr form-control input-sm mira-camera-url"'));
assert(page.includes('id="miraMediaAutoDetails"'));
assert(page.includes('id="miraMediaShowTechnical"'));
assert(page.includes('<option value="">{{Configuration manuelle}}</option>'));
assert(css.includes('#table_cmd .mira-camera-url'));
console.log('PASS: dynamic Spotify/manual command presentation preserves the complete command table');
console.log('PASS: a generic player only shows relevant media fields, without touching energy');
console.log('PASS: camera URL shares Jeedom textarea styling and automatic binding details stay accessible');
