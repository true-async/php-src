--TEST--
Thread: Recursive array structure (simple)
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

$data = [
    'items' => [
        ['id' => 1, 'children' => [['id' => 2], ['id' => 3]]],
        ['id' => 4, 'children' => [['id' => 5]]]
    ]
];

$thread->run(function($arr) {
    echo "Items count: " . count($arr['items']) . "\n";
    echo "First item children: " . count($arr['items'][0]['children']) . "\n";
}, [$data]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Items count: 2
First item children: 2
Done
