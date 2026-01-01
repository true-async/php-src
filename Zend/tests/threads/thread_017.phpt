--TEST--
Thread: Deep copy of nested arrays
--EXTENSIONS--
zts
--SKIPIF--
<?php
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
?>
--FILE--
<?php
$thread = new Thread();

$thread->run(function($data) {
    echo "Level 1: " . $data['a'] . "\n";
    echo "Level 2: " . $data['b']['c'] . "\n";
    echo "Level 3: " . $data['b']['d']['e'] . "\n";
}, [
    [
        'a' => 'value1',
        'b' => [
            'c' => 'value2',
            'd' => [
                'e' => 'value3'
            ]
        ]
    ]
]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Level 1: value1
Level 2: value2
Level 3: value3
Done
