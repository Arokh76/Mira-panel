<?php
// Regression: pure successful-send deduplication and snapshot ordering.
// This test executes production Mira PHP methods with Jeedom stand-ins.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($source,'private static function mediaRecentlyPushed(');
$end=strpos($source,'private static function mediaPushInfo(',$start);
if($start===false || $end===false)throw new RuntimeException('HTTP dedup helper absent');
$dedup=substr($source,$start,$end-$start);
$start=strpos($source,'private static function mediaFlushSnapshot(');
$end=strpos($source,'public static function onMediaSourceEvent(',$start);
if($start===false || $end===false)throw new RuntimeException('Snapshot helper absent');
$snapshot=substr($source,$start,$end-$start);
class FakeCacheInfo {
  private $cache;
  public function __construct($cache=array()){$this->cache=$cache;}
  public function getCache($key,$default=null){return $this->cache[$key]??$default;}
}
class FakeSourceInfo {
  private $v;
  public function __construct($v){$this->v=$v;}
  public function getType(){return 'info';}
  public function execCmd(){return $this->v;}
}
class FakeScreenCmd {
  private $logical;
  private $sourceId;
  private $type;
  public function __construct($logical,$sourceId,$type='info'){
    $this->logical=$logical;$this->sourceId=$sourceId;$this->type=$type;
  }
  public function getLogicalId(){return $this->logical;}
  public function getType(){return $this->type;}
  public function getConfiguration($key,$default=0){
    return $key==='mira_media_source_cmd' ? $this->sourceId : $default;
  }
}
class FakeScreen {
  private $cmds;
  public function __construct($cmds){$this->cmds=$cmds;}
  public function getCmd(){return $this->cmds;}
}
class cmd {
  public static $sources=array();
  public static function byId($id){return self::$sources[$id]??null;}
}
eval('class mira { '.$dedup.$snapshot.'
  public static $sent=array();
  private static function mediaPushInfo($eq,$cmd,$value){
    self::$sent[]=array($cmd->getLogicalId(),$value);
    return true;
  }
  public static function check($cmd,$value,$now) {
    return self::mediaRecentlyPushed($cmd,$value,$now);
  }
  public static function flush($eq){return self::mediaFlushSnapshot($eq);}
}');
$cache=new FakeCacheInfo(array('mira_media_http_value'=>'A track','mira_media_http_at'=>1000.0));
if(!mira::check($cache,'A track',1179.9))throw new RuntimeException('Acknowledged duplicate must be skipped');
if(mira::check($cache,'Other track',1100.0))throw new RuntimeException('Changed value must be delivered');
if(mira::check($cache,'A track',1180.0))throw new RuntimeException('Unchanged info must resync at 180 seconds');
if(mira::check($cache,'A track',999.0))throw new RuntimeException('Future timestamp must not suppress updates');
if(mira::check(new FakeCacheInfo(),'A track',1000.0))throw new RuntimeException('Unsent info must never be skipped');
$ids=array('cover'=>10,'progress'=>11,'artist'=>12,'title'=>13,'album'=>14,'state'=>15);
cmd::$sources=array(
  10=>new FakeSourceInfo(''), // Empty cover must clear old image.
  11=>new FakeSourceInfo('Track 4'), // Not a progress percentage.
  12=>new FakeSourceInfo('Artist'),
  13=>new FakeSourceInfo('Track 4'),
  14=>new FakeSourceInfo('Album'),
  15=>new FakeSourceInfo('PLAYING'),
);
$screen=new FakeScreen(array(
  new FakeScreenCmd('media__cover',10),
  new FakeScreenCmd('media__progress',11),
  new FakeScreenCmd('media__artist',12),
  new FakeScreenCmd('media__next',13,'action'),
  new FakeScreenCmd('media__state',15),
  new FakeScreenCmd('media__album',14),
  new FakeScreenCmd('media__title',13),
));
$count=mira::flush($screen);
$names=array_map(function($entry){return $entry[0];},mira::$sent);
$want=array('media__title','media__artist','media__album','media__state','media__cover');
if($names!==$want || $count!==count($want))
  throw new RuntimeException('Snapshot must send title first, cover last, no action or invalid progress: '.json_encode($names));
if(mira::$sent[count(mira::$sent)-1][1]!=='')
  throw new RuntimeException('Empty cover value must be forwarded to clear stale artwork');
if(strpos($source,'flock($lock,LOCK_EX|LOCK_NB)')===false ||
   strpos($source,'file_put_contents($signal,$token,LOCK_EX)')===false)
  throw new RuntimeException('Concurrent Spotify bursts must use a per-screen dirty signal and nonblocking lock');
echo "PASS: acknowledged metadata is deduplicated, but new values and periodic resync are not\n";
echo "PASS: media burst is ordered title -> artist -> album -> state -> artwork\n";
echo "PASS: empty cover, invalid progress, action safety, and per-screen coalescing guard\n";
