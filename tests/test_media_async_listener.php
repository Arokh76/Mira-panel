<?php
// Execute the actual Mira listener registration against Jeedom stand-ins.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($source,'public static function rebuildMediaListener() {');
$end=$start===false ? false : strpos($source,'public static function onMediaSourceEvent(',$start);
if($start===false||$end===false)throw new RuntimeException('Listener code not found');
$method=substr($source,$start,$end-$start);
class FakeSpotifyInfo {
  private $sourceId;
  public function __construct($id){$this->sourceId=$id;}
  public function getConfiguration($key,$default=0) {
    return $key==='mira_media_source_cmd' ? $this->sourceId : $default;
  }
}
class FakeMiraEquipment {
  public function getCmd(){return array(new FakeSpotifyInfo(1201),new FakeSpotifyInfo(1202));}
}
class eqLogic {
  public static function byType($kind){return $kind==='mira' ? array(new FakeMiraEquipment()) : array();}
}
class listener {
  public static $last;
  public $events=array();public $options=array();public $saved=false;public $id=0;
  public function __construct(){self::$last=$this;}
  public static function byClassAndFunction($class,$fn){return null;}
  public function setClass($v){}
  public function setFunction($v){}
  public function emptyEvent(){$this->events=array();}
  public function addEvent($id){$this->events[]=(int)$id;}
  public function setOption($options){$this->options=$options;}
  public function save(){$this->saved=true;}
  public function getId(){return $this->id;}
  public function remove(){}
}
eval('class mira { '.$method.' }');
mira::rebuildMediaListener();
$l=listener::$last;
sort($l->events);
if(!$l->saved || $l->events!==array(1201,1202))
  throw new RuntimeException('Mira does not register exact Spotify info IDs');
if(($l->options['background']??null)!==true)
  throw new RuntimeException('Spotify listener must run in background');
echo "PASS: Spotify title/artwork events subscribe to exact source IDs\n";
echo "PASS: asynchronous event relay cannot block Spotify source updater\n";
