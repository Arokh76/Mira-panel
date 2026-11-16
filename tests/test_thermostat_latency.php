<?php
// Thermostat 7-inch path must carry the selected choice ID straight to
// Jeedom, ACK without a screen HTTP roundtrip, and report source feedback
// independently of the minute cron.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$must=array(
  "if (\$cmd->getConfiguration('type')==='list'",
  "\$cmd->execute(array('select'=>\$choice,'mira_from_panel'=>true))",
  "if (empty(\$_options['mira_from_panel']))",
  "public static function rebuildThermostatListener()",
  "public static function onThermostatSourceEvent(\$_option)",
  "listener::byClassAndFunction('mira','onThermostatSourceEvent')",
  "\$listener->setOption(array('background'=>true))",
  "preg_match('/^Value_.+__(mode|consigne)\$/D'",
  "\$target->execute();",
  "'mira_list_http_value'",
  "if (!is_object(listener::byClassAndFunction('mira','onThermostatSourceEvent')))"
);
foreach($must as $needle) {
  if(strpos($source,$needle)===false)
    throw new RuntimeException('Thermostat latency regression: missing '.$needle);
}
$eventStart=strpos($source,'public static function event()');
$actionBranch=substr($source,$eventStart,strpos($source,'public function preUpdate()',$eventStart)-$eventStart);
if(strpos($actionBranch,"\$cmd->execute(array('select'=>\$choice")===false)
  throw new RuntimeException('Jeedom event still drops list option ID');
$actionStart=strpos($source,"}else if (\$type == 'list'){",strpos($source,'public function execute('));
$actionEnd=strpos($source,"}else if (\$type == 'button'){",$actionStart);
if($actionStart===false||$actionEnd===false)throw new RuntimeException('Missing list action');
$action=substr($source,$actionStart,$actionEnd-$actionStart);
if(strpos($action,'mira_from_panel')===false ||
   strpos($action,'listMappedAction($selected)')===false)
  throw new RuntimeException('Direct Jeedom action or old WT32 list mapping changed');
echo "PASS thermostat mode ID, immediate ACK, passive info listener, old WT32 mapping\n";
