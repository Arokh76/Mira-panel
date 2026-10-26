<?php
// Executes the exact PHP dropdown builder with simulated Jeedom equipment
// families that overlap when looked up with different capitalization.
class MiraTestSpeaker {
  private $id; private $name;
  public function __construct($id,$name) {$this->id=$id;$this->name=$name;}
  public function getId() {return $this->id;}
  public function getHumanName() {return $this->name;}
}
class eqLogic {
  public static function byType($type) {
    $room=new MiraTestSpeaker(51,'[Salle][Salle]');
    $office=new MiraTestSpeaker(52,'[Aucun][Bureau]');
    $kitchen=new MiraTestSpeaker(53,'[Aucun][Cuisine]');
    if ($type==='spotify') return array();
    if ($type==='spotifyconnect') return array($room,$office);
    if ($type==='spotifyConnect') return array($room,$office,$kitchen);
    return array();
  }
}
$html=file_get_contents(__DIR__.'/../desktop/php/mira.php');
$start=strpos($html,'$miraSeenMediaSourceIds=array();');
$end=$start===false ? false : strpos($html,'?>',$start);
if ($start===false || $end===false) throw new RuntimeException('Cannot find Spotify dropdown builder');
$code=substr($html,$start,$end-$start);
ob_start();
eval($code);
$out=ob_get_clean();
foreach (array(51,52,53) as $id) {
  $needle='<option value="'.$id.'">';
  if (substr_count($out,$needle)!==1)
    throw new RuntimeException("Spotify speaker ".$id." not unique in source dropdown: ".$out);
}
if (substr_count($out,'<option')!==3)
  throw new RuntimeException('Wrong number of unique Spotify Connect entries');
echo "PASS: Mira source menu deduplicates Jeedom Spotify Connect equipments by ID\n";
