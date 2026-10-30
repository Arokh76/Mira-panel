<?php
// Test the actual Mira energy formatter, without requiring a Jeedom instance.
$source = file_get_contents(__DIR__ . '/../core/class/mira.class.php');
if (!preg_match('/private static function formatEnergyValue\\(\\$value\\)\\s*\\{(.*?)\\n  \\}/s', $source, $parts)) {
    throw new RuntimeException('Mira energy formatter not found');
}
$format = eval('return function ($value) {' . $parts[1] . '};');
if (!is_callable($format)) throw new RuntimeException('Energy formatter not callable');
$cases = array(
    '4.9199999999999' => '4.92',
    '10.1000000000001' => '10.1',
    '7.000000000000' => '7',
    '2.02' => '2.02',
    '13.00000' => '13',
    '0.000000001' => '0',
    '-0.00001' => '0',
    '100000.999999999' => '100001',
    '5.005' => '5.01',
    '7 kW' => '7 kW',
    'inconnu' => 'inconnu',
    '10,2' => '10,2'
);
foreach ($cases as $input => $expected) {
    $actual = $format($input);
    if ($actual !== $expected) {
        throw new RuntimeException('FAILED: ' . var_export($input,true) . ' -> ' .
            var_export($actual,true) . ' (expected ' . var_export($expected,true) . ')');
    }
    echo "PASS: " . var_export($input,true) . " -> " . $actual . "\n";
}
