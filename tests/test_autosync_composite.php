<?php
// No Jeedom installation required: exercise the EXACT autosync signature
// matcher from core/ajax/mira.ajax.php with synthetic ESP32 /getindex data.
$source = file_get_contents(__DIR__ . '/../core/ajax/mira.ajax.php');
$begin = strpos($source, '        $indexMatchesExpected = function ($actions, $widgets) {');
$end = strpos($source, ";\n\n        \$results", $begin);
if ($begin === false || $end === false) {
    throw new RuntimeException('Autosync signature matcher not found');
}
eval(substr($source, $begin, $end - $begin + 1));
if (!isset($indexMatchesExpected) || !is_callable($indexMatchesExpected)) {
    throw new RuntimeException('Autosync matcher could not be loaded');
}
function expectMatch($ok, $description) {
    if (!$ok) throw new RuntimeException('FAILED: ' . $description);
    echo "PASS: " . $description . "\n";
}
$energyName = 'Maison Energie';
$energy = array();
foreach (array('solar','home','grid','battery','soc','soc2','soc3') as $field)
    $energy[] = array('type' => 'info', 'nom' => $energyName . '__' . $field);
$energyWidget = array(array('type' => 'energy', 'name' => $energyName));
expectMatch($indexMatchesExpected($energy, $energyWidget),
            'Energy card expands to seven info commands');
expectMatch(!$indexMatchesExpected(array_slice($energy, 0, 6), $energyWidget),
            'Energy: missing last command cannot validate stale index');

$mediaName = 'Salon';
$media = array();
foreach (array('title','artist','album','state','progress','volume','cover','device','devices') as $field)
    $media[] = array('type' => 'info', 'nom' => $mediaName . '__' . $field);
foreach (array('previous','playpause','next') as $field)
    $media[] = array('type' => 'button', 'nom' => $mediaName . '__' . $field);
$media[] = array('type' => 'range', 'nom' => $mediaName . '__volume_cmd');
$media[] = array('type' => 'range', 'nom' => $mediaName . '__device_cmd');
$mediaWidget = array(array('type' => 'media', 'name' => $mediaName));
expectMatch($indexMatchesExpected($media, $mediaWidget),
            'Media card expands to nine info, three buttons and two ranges');
expectMatch(!$indexMatchesExpected(array_slice($media, 0, 13), $mediaWidget),
            'Media: missing device command does not validate');
expectMatch($indexMatchesExpected(array_merge($energy, $media),
    array_merge($energyWidget, $mediaWidget)),
            'Combined Energy + Media index validates');
expectMatch(!$indexMatchesExpected(array_merge($energy, $media),
    array(array('type' => 'info', 'name' => 'Old device'))),
            'Unrelated/stale index never validates');
