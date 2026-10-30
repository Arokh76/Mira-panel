<?php
// Regression without Jeedom: run production index-binding and remap methods
// against synthetic WT32 /getindex data after a firmware version change.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($source,'    public static function screenTransportBindings(');
$end=strpos($source,'    public function Init_mira_Module() {',$start);
if($start===false || $end===false) throw new RuntimeException('Production remap not found');
$production=substr($source,$start,$end-$start);
class MiraRebindCmd {
  public $cfg,$saves=0;
  function __construct($id,$calcul='') {$this->cfg=array('commande'=>$id,'calcul'=>$calcul);}
  function getConfiguration($field,$default=null) {return $this->cfg[$field]??$default;}
  function setConfiguration($field,$value) {$this->cfg[$field]=$value;}
  function save() {++$this->saves;}
}
class log {static function add($class,$level,$msg) {}}
eval('class MiraRebindHarness { public $commands=array(); '.$production.
  ' public function getCmd($type,$logical) {return $this->commands[$logical]??null;} }');
$h=new MiraRebindHarness();
$h->commands['Humidite']=new MiraRebindCmd(3,'#[Salon][Capteur][Humidite]#');
$h->commands['__mira_weather_condition']=new MiraRebindCmd(4);
$h->commands['Salon__title']=new MiraRebindCmd(5,'#[Spotify][Lecteur][Titre]#');
$h->commands['Value_Salon__volume_cmd']=new MiraRebindCmd(6);
$h->commands['Salon__volume_cmd']=new MiraRebindCmd(6);
$h->commands['Lumiere']=new MiraRebindCmd(7,'#[Salon][Lampe][Etat]#');
$h->commands['On_Lumiere']=new MiraRebindCmd(8);
$h->commands['Off_Lumiere']=new MiraRebindCmd(7);
$h->commands['NonModifie']=new MiraRebindCmd(18);
$actions=array(
  array('type'=>'info','nom'=>'Humidite','id'=>5),
  array('type'=>'info','nom'=>'__mira_weather_condition','id'=>8),
  array('type'=>'info','nom'=>'Salon__title','id'=>9),
  array('type'=>'range','nom'=>'Salon__volume_cmd','id'=>14),
  array('type'=>'toggle','nom'=>'Lumiere','id0'=>21,'id1'=>22),
  array('type'=>'list','nom'=>'VolumeOption','nomliste'=>'Chambre','id'=>25),
  array('type'=>'info','nom'=>'Malformed','id'=>0)
);
$n=$h->repairScreenTransportIds($actions);
if($n!==8) throw new RuntimeException('Expected 8 existing commands repaired, got '.$n);
foreach(array('Humidite'=>5,'__mira_weather_condition'=>8,'Salon__title'=>9,
  'Value_Salon__volume_cmd'=>14,'Salon__volume_cmd'=>14,'Lumiere'=>21,
  'Off_Lumiere'=>21,'On_Lumiere'=>22) as $key=>$want) {
  if($h->commands[$key]->getConfiguration('commande')!==$want)
    throw new RuntimeException('Wrong remap for '.$key);
}
if($h->commands['Humidite']->getConfiguration('calcul')!=='#[Salon][Capteur][Humidite]#' ||
   $h->commands['Salon__title']->getConfiguration('calcul')!=='#[Spotify][Lecteur][Titre]#')
  throw new RuntimeException('Jeedom associations were changed');
if($h->commands['Lumiere']->getConfiguration('id_0')!==21 ||
   $h->commands['Lumiere']->getConfiguration('id_1')!==22)
  throw new RuntimeException('Toggle IDs not restored');
if($h->commands['NonModifie']->saves!==0)
  throw new RuntimeException('Unrelated commands must not be saved');
if($h->repairScreenTransportIds($actions)!==0)
  throw new RuntimeException('Second pass must be idempotent');
if($h->repairScreenTransportIds(array())!==0)
  throw new RuntimeException('Empty index must not alter anything');
$ambiguous=MiraRebindHarness::screenTransportBindings(array(
  array('type'=>'info','nom'=>'Duplicate','id'=>3),
  array('type'=>'info','nom'=>'Duplicate','id'=>4)
));
if(isset($ambiguous['Duplicate']))
  throw new RuntimeException('Ambiguous names must not be rebound');
if(strpos($source,'[SyncCmd] index ecran vide : conservation des commandes existantes')===false)
  throw new RuntimeException('Empty-index destructive sync guard missing');
$syncStart=strpos($source,'  public function postUpdate() {');
$syncEnd=strpos($source,'    public function Init_mira_Module() {',$syncStart);
if($syncStart===false || $syncEnd===false) throw new RuntimeException('Cannot inspect postUpdate');
$postUpdate=substr($source,$syncStart,$syncEnd-$syncStart);
if(strpos($postUpdate,'$existingCmd->remove()')!==false)
  throw new RuntimeException('Automatic sync must never prune existing Jeedom commands');
if(strpos($postUpdate,'[SyncCmd] Synchronisation non destructive')===false)
  throw new RuntimeException('Non-destructive sync mode not found');
echo "PASS: boot remaps numeric screen IDs without changing Jeedom associations\n";
echo "PASS: toggle, media, weather, empty-index, ambiguity and idempotence checks\n";
