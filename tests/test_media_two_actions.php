<?php
// Exercise the actual Play and Pause auto-link branch without a Jeedom database.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$needle="} elseif (\$field==='play' || \$field==='pause') {";
$start=strpos($source,$needle);
$end=$start===false ? false : strpos($source,"} elseif (\$field==='playpause') {",$start);
if($start===false||$end===false)throw new RuntimeException('Separate actions block not found');
$branch=substr($source,$start+strlen($needle),$end-$start-strlen($needle));
class MiraTestMediaAction {
  private $id; public function __construct($id){$this->id=$id;}
  public function getId(){return $this->id;}
}
class MiraTestMediaTarget {
  public $configuration=array(); public $saves=0;
  public function setConfiguration($key,$value){$this->configuration[$key]=$value;}
  public function save(){$this->saves++;}
}
$actions=array('play'=>new MiraTestMediaAction(101),'pause'=>new MiraTestMediaAction(102));
$configured=0;
foreach(array('play'=>101,'pause'=>102) as $field=>$expectedId) {
  $target=new MiraTestMediaTarget();
  eval($branch);
  if($target->configuration['mira_media_target_cmd']!==strval($expectedId))
    throw new RuntimeException('Wrong Spotify action for '.$field);
  if($target->saves!==1)throw new RuntimeException('Missing auto save for '.$field);
  echo "PASS: ".$field." links to action ".$expectedId." without Playing Info\n";
}
if($configured!==2)throw new RuntimeException('Both actions not configured');
unset($actions['pause']); $field='pause'; $target=new MiraTestMediaTarget();
eval($branch);
if($target->configuration['mira_media_target_cmd']!=='')
  throw new RuntimeException('Missing Pause should not retain stale target');
echo "PASS: missing Pause has no stale auto action\n";
