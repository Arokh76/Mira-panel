// Jeedom navigates between equipment pages without a full page refresh.
// Executing the module declaration again must never throw SyntaxError.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('desktop/js/mira.js','utf8');
const match=source.match(/var MiraEnergyModuleFlags=\[[\s\S]*?\];/);
assert.ok(match,'Safe, redeclarable module flags definition must exist');
const context=vm.createContext({});
vm.runInContext(match[0],context);
vm.runInContext(match[0],context);
assert.equal(context.MiraEnergyModuleFlags.length,4);
assert.match(source,/expectedWidget\[pair\[0\]\]=item\[pair\[0\]\]===true/);
assert.match(source,/\$\("#miraboutonenvoyer"\)\.off\('click'\)/);
console.log('PASS: Mira JavaScript energy editor survives repeated script injection');
