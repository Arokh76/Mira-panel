<?php
// Run the exact Jeedom command-label translation block with lightweight stubs.
$source=file_get_contents(__DIR__.'/../core/class/mira.class.php');
$start=strpos($source,"if (preg_match('/^(.*)__(solar|home|grid|battery|soc|soc2|soc3)\$/D'");
$end=$start===false ? false : strpos($source,'$info->save();',$start);
if($start===false || $end===false)throw new RuntimeException('Translation block missing');
$block=substr($source,$start,$end-$start);
class MiraFrenchNameCmd {
  public $name;
  public function __construct($value){$this->name=$value;}
  public function getName(){return $this->name;}
  public function setName($name){$this->name=$name;}
}
$cases=[
  'Home__solar'=>'Home · Solaire',
  'Home__home'=>'Home · Consommation maison',
  'Home__grid'=>'Home · Réseau',
  'Home__battery'=>'Home · Batterie',
  'Home__soc'=>'Home · Stockage · SoC',
  'Home__soc2'=>'Home · SoC batterie 2',
  'Maison__device'=>'Maison · Enceinte active',
  'Maison__devices'=>'Maison · Enceintes disponibles',
  'Maison__title'=>'Maison · Titre',
  'Maison__state'=>'Maison · État de lecture'
];
foreach($cases as $id=>$expected){
  $name=$id;
  $info=new MiraFrenchNameCmd($id);
  eval($block);
  if($info->getName()!==$expected)
    throw new RuntimeException('French translation failed '.$id.' -> '.$info->getName());
  $info=new MiraFrenchNameCmd('Nom personnalisé');
  eval($block);
  if($info->getName()!=='Nom personnalisé')
    throw new RuntimeException('Custom label overwritten: '.$id);
}
echo "PASS: French Energy/Media automatic command names\n";
echo "PASS: custom user labels preserved\n";
