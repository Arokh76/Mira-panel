<?php
// Test Mira's real action/Info routing logic, without Jeedom or hardware.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$at=strpos($source,'Composite Spotify sliders are ACTION commands');
if ($at===false) throw new RuntimeException('New media range mapping absent');
$start=strrpos(substr($source,0,$at),"          if (\$json_index['actions'][\$i]['type'] == 'info'");
$end=strpos($source,'          $info = $this->getCmd(null, $name);',$at);
if ($start===false || $end===false || $end-$start>2400) throw new RuntimeException('Could not isolate range mapping');
$mapping=substr($source,$start,$end-$start);
$cases=array(
 array('range','Maison__volume_cmd','Maison__volume_cmd'),
 array('range','Maison__device_cmd','Maison__device_cmd'),
 array('range','Luminosité','Value_Luminosité'),
 array('info','Maison__volume','Maison__volume'),
 array('button','Maison__next','Maison__next'),
 array('list','Mode chauffage','Value_Mode chauffage'),
);
foreach ($cases as $case) {
 $json_index=array('actions'=>array(array('type'=>$case[0],'nom'=>$case[1],
                                            'nomliste'=>$case[1])));
 $i=0; $name=null;
 eval($mapping);
 if($name!==$case[2]) throw new RuntimeException($case[1].' mapped to '.$name.
      ' instead of '.$case[2]);
 echo 'PASS '.$case[0].' '.$case[1].' -> '.$name."\n";
}
// Passive refresh must not ever call action commands.
$marker='public function refreshPassiveWidgets() {';
$start=strpos($source,$marker);
$end=strpos($source,'/** Saved Jeedom equipment',$start);
if($start===false||$end===false) throw new RuntimeException('Passive refresh absent');
$body=substr($source,$start,$end-$start);
if(strpos($body,"$"."target->getType()!=='info'")===false ||
   strpos($body,"$"."target->getConfiguration('type')==='info'")===false ||
   strpos($body,'$mapped->execCmd(')!==false)
  throw new RuntimeException('Passive refresh must only evaluate Info values');
echo "PASS refresh ignores action and range commands\n";
