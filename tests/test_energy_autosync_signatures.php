<?php
// Regression: Jeedom autosync must recognize the exact Energy index after
// adding ECS/EV modules and optional instantaneous W telemetry fields.
$src=file_get_contents(__DIR__.'/../core/ajax/mira.ajax.php');
$start=strpos($src,'$indexMatchesExpected = function ($actions, $widgets) {');
$end=$start===false ? false : strpos($src,'        $results = array();',$start);
if($start===false || $end===false)throw new RuntimeException('Autosync signature validator missing');
eval(substr($src,$start,$end-$start));
if(!isset($indexMatchesExpected))throw new RuntimeException('Closure was not extracted');
$base=array('solar','home','grid','battery','soc','soc2','soc3');
$aux=array('ecs_temp','ecs_power','ev_soc','ev_power');
$instant=array('solar_power','home_power','grid_power','battery_power');
$actions=function($fields) {
    $result=array();
    foreach($fields as $field)$result[]=array('type'=>'info','nom'=>'Energie__'.$field);
    return $result;
};
$legacy=array(array('type'=>'energy','name'=>'Energie'));
$modern=array(array('type'=>'energy','name'=>'Energie',
    'showSolar'=>true,'showBattery'=>true,'showEcs'=>true,'showEv'=>true));
$modernOff=array(array('type'=>'energy','name'=>'Energie',
    'showSolar'=>false,'showBattery'=>false,'showEcs'=>false,'showEv'=>false));
$ok=[
    ['legacy 7', $actions($base), $legacy, true],
    ['modern 11', $actions(array_merge($base,$aux)), $modern, true],
    ['modern 15', $actions(array_merge($base,$aux,$instant)), $modern, true],
    ['disabled modules still reserve slots', $actions(array_merge($base,$aux,$instant)), $modernOff, true],
    ['legacy index rejected after enabling ECS/EV', $actions($base), $modern, false],
    ['missing ECS power rejected', $actions(array_merge($base,array('ecs_temp','ev_soc','ev_power'))), $modern, false],
    ['incomplete instant power rejected', $actions(array_merge($base,$aux,array('solar_power'))), $modern, false],
    ['stale 15 index rejected for legacy widget', $actions(array_merge($base,$aux,$instant)), $legacy, false],
];
foreach($ok as [$name,$index,$widgets,$expected]){
    $actual=$indexMatchesExpected($index,$widgets);
    if($actual!==$expected)throw new RuntimeException($name.' expected '.($expected?'pass':'reject'));
}
echo "PASS: Jeedom Energy autosync legacy 7, ECS/VE 11, instantaneous 15 and negative cases\n";
