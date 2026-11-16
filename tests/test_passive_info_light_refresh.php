<?php
// Regression: a failed /getindex must never suppress Jeedom Info feedback,
// including read-only light-button states on the WT32 and Mira 7.
$src=file_get_contents(__DIR__.'/../core/class/mira.class.php');
function section($src,$begin,$end) {
    $start=strpos($src,$begin);
    $finish=$start===false?false:strpos($src,$end,$start+strlen($begin));
    if ($start===false||$finish===false) throw new RuntimeException('Missing '.$begin);
    return substr($src,$start,$finish-$start);
}
$cron=section($src,'public static function cron()','public static function cron15()');
$light=section($src,'public function refreshLightToggleStates(','/** Subscribe to actual Jeedom light Info events');
class eqLogic {
    public static $panels=array();
    public static function byType($type,$enabled=true){return self::$panels;}
}
class listener {
    public static function byClassAndFunction($a,$b){return new stdClass();}
}
class log {
    public static function add($type,$level,$msg){}
}
class cmd {
    public static $sources=array();
    public static function byString($id){return self::$sources[$id]??null;}
}
class jeedom {
    public static $values=array();
    public static function evaluateExpression($name){return self::$values[$name]??null;}
}
class FakePanel {
    public $cache=array(),$calls=array(),$index='Erreur';
    public function getConfiguration($k,$def=''){return $k==='adresseip'?'192.168.0.38':$def;}
    public function getCache($k,$def=0){return $this->cache[$k]??$def;}
    public function setCache($k,$v){$this->cache[$k]=$v;}
    public function GetIndex($raw=false,$force=false){$this->calls[]='index';return $this->index;}
    public function refreshPassiveWidgets($force=false){$this->calls[]='infos';}
    public function refreshLightToggleStates($sourceId=null,$force=false){$this->calls[]='light';}
    public function refreshClockWeather($force=false){$this->calls[]='weather';}
    public function syncGpioStates(){$this->calls[]='gpio';}
    public function repairScreenTransportIds($v){$this->calls[]='repair';}
    public function postUpdate($v=null){$this->calls[]='postUpdate';return true;}
}
eval('class MiraCronHarness {'.$cron.'}');
$p=new FakePanel();eqLogic::$panels=array($p);
MiraCronHarness::cron();
foreach(array('infos','light','weather') as $required)
  if(!in_array($required,$p->calls,true))
    throw new RuntimeException('Cron lost '.$required.' on failed /getindex');
if(array_search('index',$p->calls)<array_search('light',$p->calls))
  throw new RuntimeException('Metadata read still gates lights');
$p->calls=array();MiraCronHarness::cron();
if(in_array('index',$p->calls,true))
  throw new RuntimeException('Metadata retry is still sent every minute');
if(!in_array('light',$p->calls,true)||!in_array('infos',$p->calls,true))
  throw new RuntimeException('Passive states stopped during index cooldown');
$p->calls=array();
$p->cache['mira_pending_sync']=1;
$p->cache['mira_index_refresh_at']=time()-121;
$p->index=array('actions'=>array(array('type'=>'info','nom'=>'A','id'=>1)));
MiraCronHarness::cron();
if(!in_array('postUpdate',$p->calls,true) ||
   array_search('postUpdate',$p->calls)<array_search('light',$p->calls))
  throw new RuntimeException('Deferred metadata update delayed ordinary lights');

class FakeInfoSource {
    public function getType(){return 'info';}
    public function getId(){return 101;}
}
class FakeStateCommand {
    public $cache=array(),$sentState=0;
    public function getType(){return 'info';}
    public function getLogicalId(){return 'Lampe';}
    public function getConfiguration($key,$def=null){
        $m=array('type'=>'toggle','mira_source'=>'jeedom','calcul'=>'#101#',
                 'id_0'=>8,'id_1'=>9);
        return $m[$key]??$def;
    }
    public function getCache($key,$def=null){return $this->cache[$key]??$def;}
    public function setCache($values){$this->cache=array_merge($this->cache,$values);}
}
class LightPanel {
    public $cmd,$urls=array(),$fail=false;
    public function __construct($cmd){$this->cmd=$cmd;}
    public function getConfiguration($key,$def=''){
      return $key==='adresseip'?'192.168.0.38':($key==='password'?'secret':$def);
    }
    public function getCmd($type){return array($this->cmd);}
    public function panelHttpGet($url){$this->urls[]=$url;return $this->fail?false:'OK';}
    '.$light.'
}
cmd::$sources['#101#']=new FakeInfoSource();
jeedom::$values['#101#']='1';
$command=new FakeStateCommand();
$panel=new LightPanel($command);
if($panel->refreshLightToggleStates()!==1 ||
   !str_contains($panel->urls[0],'/sync/toggle?key=secret&id=9'))
   throw new RuntimeException('Light ON not synchronized passively');
$panel->refreshLightToggleStates();
if(count($panel->urls)!==1)
   throw new RuntimeException('Unchanged lights repeatedly flood HTTP');
jeedom::$values['#101#']='0';
$panel->refreshLightToggleStates(101);
if(count($panel->urls)!==2 ||
   !str_contains($panel->urls[1],'/sync/toggle?key=secret&id=8'))
   throw new RuntimeException('Light OFF not pushed immediately on source event');
$panel->refreshLightToggleStates(102);
if(count($panel->urls)!==2)throw new RuntimeException('Wrong source triggered light update');
$panel->refreshLightToggleStates(null,true);
if(count($panel->urls)!==3)
   throw new RuntimeException('Manual refresh cannot bypass light dedupe');
$command->cache['mira_light_sync_at']=microtime(true)-181;
$panel->refreshLightToggleStates();
if(count($panel->urls)!==4)
   throw new RuntimeException('Post-reboot periodic replay is absent');
$panel->fail=true;jeedom::$values['#101#']='1';
$panel->refreshLightToggleStates();
if((int)$command->getCache('mira_light_sync_id')!==8)
   throw new RuntimeException('Failed HTTP was wrongly acknowledged in state cache');
$panel->fail=false;$panel->refreshLightToggleStates();
if(!str_contains(end($panel->urls),'id=9'))
   throw new RuntimeException('Failed light state was not retried');

if(!str_contains($src,"public static function onLightStateSourceEvent(")||
   !str_contains($src,"public static function rebuildLightStateListener()")||
   !str_contains($src,"if (!is_object(listener::byClassAndFunction('mira','onLightStateSourceEvent')))"))
   throw new RuntimeException('Asynchronous light source feedback listener absent');
if(!str_contains($src,'public static function onThermostatSourceEvent('))
   throw new RuntimeException('Working thermostat listener accidentally removed');
if(!str_contains($src,'$eqLogic->refresh(true);') ||
   !str_contains($src,'$this->refreshPassiveWidgets(true);') ||
   !str_contains($src,'$this->refreshLightToggleStates(null,true);') ||
   !str_contains($src,"empty(\$_options['mira_force_refresh'])"))
   throw new RuntimeException('Forced manual refresh does not bypass passive caches');
echo "PASS passive Info + light state refresh independent of index; no actuators; source events, dedupe, replay, thermostat preserved\n";
