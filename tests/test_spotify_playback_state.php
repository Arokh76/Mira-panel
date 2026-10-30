<?php
// Test production parsing and discovery of Spotify playback Info independently
// of the proprietary Spotify Connect plugin.
$src=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($src,'private static function mediaNormalize(');
$end=strpos($src,'public function configureMediaSource()',$start);
if($start===false||$end===false)throw new RuntimeException('Playback helper code not found');
$methods=substr($src,$start,$end-$start);
class FakePlaybackCmd {
  private $name;private $id;private $val;private $generic;
  public function __construct($name,$id,$value,$generic='') {
    $this->name=$name;$this->id=$id;$this->val=$value;$this->generic=$generic;
  }
  public function getName(){return $this->name;}
  public function getLogicalId(){return $this->id;}
  public function getType(){return 'info';}
  public function getGeneric_type(){return $this->generic;}
  public function execCmd(){return $this->val;}
}
class FakePlaybackSource {
  private $cmds;
  public function __construct($cmds){$this->cmds=$cmds;}
  public function getCmd(){return $this->cmds;}
}
eval('class mira { '.$methods.'
  public static function pick($s) {return self::findMediaPlaybackStateCmd($s);}
  public static function state($v) {return self::mediaPlaybackStateValue($v);}
}');
$values=array('PLAYING'=>'1','PAUSED_PLAYBACK'=>'0','En lecture'=>'1',
  'En pause'=>'0','1'=>'1','0'=>'0','Lecture'=>'1','Arrêté'=>'0',true=>'1');
foreach(array(
 ['input'=>'PLAYING','want'=>'1'],['input'=>'PAUSED_PLAYBACK','want'=>'0'],
 ['input'=>'En lecture','want'=>'1'],['input'=>'En pause','want'=>'0'],
 ['input'=>true,'want'=>'1'],['input'=>false,'want'=>'0'],
 ['input'=>1,'want'=>'1'],['input'=>0,'want'=>'0'],
 ['input'=>'Idle','want'=>'0'],['input'=>'Connecting','want'=>null],
 ['input'=>'A musical title','want'=>null],['input'=>45,'want'=>null],
) as $case) {
  $actual=mira::state($case['input']);
  if($actual!==$case['want'])
    throw new RuntimeException('Invalid state normalization: '.var_export($case,true).' = '.var_export($actual,true));
}
echo "PASS: playback state normalization is strict\n";
$cases=array(
 [['Status','status','PLAYING',''],true],
 [['Playback Status','playback_state','PAUSED_PLAYBACK',''],true],
 [['Custom State','state_value',1,'MEDIA_STATE'],true],
 [['Statut','status',1,''],false], // an equipment online indicator is not playback
 [['State','state','Connected',''],false],
 [['Volume','volume',40,''],false]
);
foreach($cases as $case) {
  $args=$case[0];
  $cmd=new FakePlaybackCmd(...$args);
  $actual=mira::pick(new FakePlaybackSource(array($cmd)));
  if((is_object($actual))!==$case[1])
    throw new RuntimeException('False playback-source detection on '.$args[0]);
}
echo "PASS: MEDIA_STATE/Playback Status accepted, connectivity status rejected\n";
