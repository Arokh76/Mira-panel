<?php
// Regression: execute the actual naming blocks from mira.class.php against
// stand-in commands, without a running Jeedom environment.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
class MiraAutoNameMock {
  public $name;
  public function __construct($name) { $this->name=$name; }
  public function getName() { return $this->name; }
  public function setName($name) { $this->name=$name; }
  public function setConfiguration($key,$value) {}
}
function extractNamingBlock($source, $first, $last) {
  $start=strpos($source,$first);
  if($start===false) throw new RuntimeException('Cannot find naming block: '.$first);
  $end=strpos($source,$last,$start);
  if($end===false) throw new RuntimeException('Cannot find naming block end: '.$last);
  return substr($source,$start,$end-$start);
}
$begin="if (preg_match('/^(.*)__(volume_cmd|device_cmd)\$/D',(string)\$name,\$miraRange)) {";
$infoBlock=extractNamingBlock($source,$begin,'$info->save();');
$actionBlock=extractNamingBlock($source,$begin,'$cmd->setValue($info_id);');
$names=array();
foreach(array('Maison__volume_cmd','Maison__device_cmd',
              'Salle__volume_cmd','Salle__device_cmd') as $logical) {
  $name=$logical;
  $info=new MiraAutoNameMock('Value_'.$name);
  eval($infoBlock);
  $cmd=new MiraAutoNameMock($name);
  eval($actionBlock);
  $names[]=$info->getName();
  $names[]=$cmd->getName();
}
if(count(array_unique($names))!==count($names)) {
  throw new RuntimeException('Duplicate Jeedom names among two media widgets: '.json_encode($names));
}
$name='Maison__device_cmd';
$info=new MiraAutoNameMock('Volume actuel (interne)'); // beta.12 legacy bug
eval($infoBlock);
if($info->getName()!=='Maison · Enceinte sélectionnée (interne)')
  throw new RuntimeException('Failed to migrate beta.12 duplicate internal name');
echo "PASS: distinct media command names across speaker/volume and multiple widgets\n";
echo "PASS: existing beta.12 internal-name collision gets corrected\n";
