<?php
// Regression guards for Jeedom saves while the Mira HTTP service is unresponsive.
$root=dirname(__DIR__);
$source=file_get_contents($root.'/core/class/mira.class.php');
$ajax=file_get_contents($root.'/core/ajax/mira.ajax.php');
$method=function($begin,$end)use($source) {
    $a=strpos($source,$begin);
    $b=$a!==false?strpos($source,$end,$a+strlen($begin)):false;
    if($a===false||$b===false)throw new RuntimeException('Missing method boundary: '.$begin);
    return substr($source,$a,$b-$a);
};
$pre=$method('public function preUpdate()', 'public function postUpdate()');
if(strpos($pre,'GetModule(')!==false || strpos($pre,'GetIndex(')!==false)
    throw new RuntimeException('Saving Jeedom config must never require live HTTP in preUpdate');
$post=$method('public function postUpdate()', 'public function repairScreenTransportIds(');
if(strpos($post,'[SyncCmd]')===false || strpos($post,'commandes existantes conservees')===false ||
   strpos($post,'sauvegarde Jeedom conservee')===false)
    throw new RuntimeException('Post-update should preserve edits when the screen is offline');
$event=$method('public static function event()', 'public function preUpdate()');
if(strpos($event,"'slider'=>\$sentValue")===false || strpos($event,"'value'=>\$sentValue")===false)
    throw new RuntimeException('7-inch media event must pass actual range value to Jeedom');
if(strpos($ajax,'/testmodule?key=')!==false)
    throw new RuntimeException('ProgramUI must not be blocked by a brief preflight probe');
if(strpos($ajax,'/programui')===false || strpos($ajax,'$miraVerifyProgramUI(')===false)
    throw new RuntimeException('ProgramUI must POST once and verify on lost ACK');
$executeStart=strpos($source,'public function execute($_options = array())');
if($executeStart===false)throw new RuntimeException('Missing command execute method');
$execute=substr($source,$executeStart);
if(strpos($execute,'mira_info_http_value')===false || strpos($execute,'45.0')===false)
    throw new RuntimeException('Unchanged Info HTTP needs bounded deduplication');
if(strpos($execute,'if ($output!==false)')===false)
    throw new RuntimeException('Failed Info HTTP sends must remain retryable');
echo "PASS offline saves, no fragile ProgramUI preflight, WT32 action fallback, live volume and Info dedupe\n";
