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

// Follow-up hardening: JavaScript used to repeat the /testmodule GET even
// after the AJAX relay probe was removed. Both programming entry points
// must go straight to a SINGLE authenticated POST.
$js=file_get_contents($root.'/desktop/js/mira.js');
if(strpos($js,'HTTPTestModuleSend(')!==false)
    throw new RuntimeException('Programming JS still has a preflight /testmodule gate');
if(strpos($js,'$("#miraboutonenvoyer").on(\'click\'')===false ||
   strpos($js,"SendListHTTP(ip,mdp);")===false ||
   strpos($js,"$('#miradesignersave').on('click'")===false)
    throw new RuntimeException('Classic and designer must both program directly');
$ajaxSyncStart=strpos($ajax,"if (init('action') == 'SyncByScreen')");
$ajaxSyncEnd=strpos($ajax,"if (init('action') == 'PostHTTPJson')");
$sync=substr($ajax,$ajaxSyncStart,$ajaxSyncEnd-$ajaxSyncStart);
if(strpos($sync,'postUpdate($decoded)')===false ||
   strpos($sync,'usleep(1500000)')===false ||
   strpos($sync,"'status' => $synced===true ? 'synced' : 'deferred'")===false)
    throw new RuntimeException('Autosync must reuse read-only index, throttle reads and report deferred sync');
$getIndex=$method('public function GetIndex(', 'public function AddListLvgl(');
if(strpos($getIndex,'mira_index_retry_after')===false ||
   strpos($getIndex,'$force)')===false ||
   strpos($getIndex,"$"."this->panelHttpRequest(")===false)
    throw new RuntimeException('Index reads are not rate-limited with manual override');
if(strpos($post,'GetModule();')!==false &&
   strpos($post,"trim((string)$"."this->getConfiguration('module'")===false)
    throw new RuntimeException('Module metadata is a mandatory Save gate again');
if(strpos($post,'mira_transport_checked_sig')===false ||
   strpos($post,'mira_transport_checked_at')===false ||
   strpos($post,'mira_pending_sync')===false ||
   strpos($post,'Init_mira_Module($indexDocument)')===false)
    throw new RuntimeException('Post-update lacks safe deduplication and deferred resync');
if(strpos($source,'$eqLogic->postUpdate($index);')===false)
    throw new RuntimeException('Minute cron must repair deferred sync when index returns');
if(strpos($ajax,'$metadata?5500:1500')===false)
    throw new RuntimeException('Editor GetHTTP metadata still times out at 1.5 seconds');
echo "PASS both editor routes, throttled metadata, deferred recovery, and unchanged transport refresh\n";
