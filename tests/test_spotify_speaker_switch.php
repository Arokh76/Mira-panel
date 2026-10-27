<?php
// Exercise the production equipment-switcher with mock Jeedom speakers.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($source,'public function selectMediaReaderFromPicker(');
$end=$start===false ? false : strpos($source,'private static function mediaPushInfo(',$start);
if($start===false || $end===false)throw new RuntimeException('Speaker switching method missing');
$method=substr($source,$start,$end-$start);
class log {public static function add($a,$b,$c) {}}
class eqLogic {
  public static $items=array();
  public static function byId($id){return self::$items[$id]??null;}
  public static function byType($type) {
    if(strtolower($type)!=='spotifyconnect')return array();
    return array_values(self::$items);
  }
}
class TestSpeaker {
  public $id; public $name; public $enabled=true;
  public function __construct($id,$name){$this->id=$id;$this->name=$name;}
  public function getId(){return $this->id;}
  public function getName(){return $this->name;}
  public function getIsEnable(){return $this->enabled;}
  public function getEqType_name(){return 'spotifyConnect';}
}
eqLogic::$items=array(1=>new TestSpeaker(1,'Salle'),2=>new TestSpeaker(2,'Ampli'));
$harness='class TestMiraController {
  public $configuration=array("media_source_eqid"=>1);
  public $saves=0; public $refreshes=0; public $rebuilt=0;
  public function getConfiguration($key,$default=null){return $this->configuration[$key]??$default;}
  public function setConfiguration($key,$value){$this->configuration[$key]=$value;}
  public function save(){$this->saves++;$this->configureMediaSource();self::rebuildMediaListener();}
  public function configureMediaSource(){$this->rebuilt++;}
  public static function rebuildMediaListener(){}
  public function refreshPassiveWidgets(){$this->refreshes++;}
  public function getCmd(){return array();} // no state Info configured in this isolated mock
  private static function mediaDestinationNames($source) {
    $names=array();$seen=array();
    foreach(eqLogic::byType("spotifyconnect") as $item){
      if(!$item->getIsEnable())continue;
      $name=$item->getName();
      if(isset($seen[$name]))continue;
      $seen[$name]=true;$names[]=$name;
    }
    return $names;
  }
  '.$method.'
}';
eval($harness);
$m=new TestMiraController();
if(!$m->selectMediaReaderFromPicker(1,'Salle|Ampli') ||
  (int)$m->getConfiguration('media_source_eqid')!==2 ||
  $m->saves!==1 || $m->refreshes!==1)
  throw new RuntimeException('Switch to amplifier failed to rebind and refresh');
echo "PASS: dropdown switches from Salle to Ampli and refreshes info\n";
if($m->selectMediaReaderFromPicker(0,'Salle|Wrong')) 
  throw new RuntimeException('Stale device list was accepted');
if((int)$m->getConfiguration('media_source_eqid')!==2)
  throw new RuntimeException('Stale list modified speaker');
echo "PASS: rejects stale or tampered source names\n";
eqLogic::$items[1]->enabled=false;
if($m->selectMediaReaderFromPicker(0,'Salle|Ampli'))
  throw new RuntimeException('Disabled source accepted');
echo "PASS: disabled speaker never selected\n";
if(!$m->selectMediaReaderFromPicker(0,'Ampli') ||
  $m->saves!==1 || $m->refreshes!==2)
  throw new RuntimeException('Refresh of selected source failed');
echo "PASS: selecting same speaker refreshes but does not save or transfer\n";
