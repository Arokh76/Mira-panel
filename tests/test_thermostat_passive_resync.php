<?php
// Thermostat passive feedback: verify only Info state commands can update
// the screen, never an ACTION command that could drive heating.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($source,'public function refreshPassiveWidgets() {');
$end=strpos($source,'/** Saved Jeedom equipment',$start);
if($start===false||$end===false)throw new RuntimeException('Passive refresh missing');
$method=substr($source,$start,$end-$start);
class eqLogic { public static function byId($id){return null;} }
class log { public static function add($a,$b,$c){throw new RuntimeException($c);} }
class FakePassiveCmd {
  private $logical; private $kind; private $type; private $calcul; private $value;
  public $calls=0;public $events=array();
  public function __construct($logical,$kind,$type,$calcul,$value){
    $this->logical=$logical;$this->kind=$kind;$this->type=$type;$this->calcul=$calcul;$this->value=$value;
  }
  public function getLogicalId(){return $this->logical;}
  public function getType(){return $this->kind;}
  public function getConfiguration($key,$default=null){
    if($key==='mira_media_source_cmd') return 0;
    if($key==='type') return $this->type;
    if($key==='calcul') return $this->calcul;
    return $default;
  }
  public function execute(){$this->calls++;return $this->value;}
  public function event($v){$this->events[]=$v;}
}
eval('class mira {
  private $cmds;
  public function __construct($cmds){$this->cmds=$cmds;}
  public function getCmd(){return $this->cmds;}
  public function getConfiguration($k,$d=0){return $d;}
  '.$method.'
}');
$tests=array(
  array('Thermostat__temperature','info','info','#12#',19.5,1),
  array('Value_Thermostat__consigne','info','range','#13#',19,1),
  array('Value_Thermostat__mode','info','list','#14#','Confort',1),
  array('Value_Thermostat__consigne','info','range','',19,0),
  array('Value_Thermostat__mode','action','list','#14#','Éco',0),
  array('Thermostat__consigne','action','range','#13#',18,0),
  array('Value_Lumiere','info','range','#15#',70,0),
  array('Value_Multimedia__volume_cmd','info','range','#16#',45,0),
  array('Value_ModeVentilo','info','list','#17#','Normal',0)
);
$cmds=array();$expected=0;
foreach($tests as $t){$cmds[]=new FakePassiveCmd($t[0],$t[1],$t[2],$t[3],$t[4]);$expected+=$t[5];}
$m=new mira($cmds);
$count=$m->refreshPassiveWidgets();
if($count!==$expected) throw new RuntimeException('Expected '.$expected.' passive updates, received '.$count);
foreach($cmds as $i=>$cmd){
  if($cmd->calls!==$tests[$i][5]) throw new RuntimeException('Unexpected execute() for '.$tests[$i][0]);
  if(count($cmd->events)!==$tests[$i][5])throw new RuntimeException('Unexpected event() for '.$tests[$i][0]);
}
$start=strpos($source,'private static function missingPassiveValue($value) {');
$end=strpos($source,'private function reportMissingPassiveValue()',$start);
if($start===false||$end===false)throw new RuntimeException('Null-like source validation missing');
$helper=substr($source,$start,$end-$start);
eval('class TestPassiveGuard {'.$helper.'
  public static function missing($value){return self::missingPassiveValue($value);}
}');
foreach(array(null,'null','NULL','Undefined','NaN','Erreur','') as $bad)
  if(!TestPassiveGuard::missing($bad)) throw new RuntimeException('Missing source accepted: '.var_export($bad,true));
foreach(array('0',0,19,'19.5','Confort','12 kWh') as $ok)
  if(TestPassiveGuard::missing($ok)) throw new RuntimeException('Valid source rejected: '.var_export($ok,true));
echo "PASS: thermostat setpoint and mode use passive Info commands, never a heater action\n";
echo "PASS: unrelated sliders, media controls and lists remain excluded\n";
echo "PASS: unavailable Jeedom values are detected without suppressing numeric zero\n";
