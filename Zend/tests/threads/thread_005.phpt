--TEST--
Thread: Thread with nested arrays
--SKIPIF--
<?php
if (!Thread::isSupported()) {
    die('skip Thread support not available (requires ZTS build)');
}
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
?>
--FILE--
<?php
$thread = new Thread();

$thread->run(function($data) {
    echo "Name: {$data['name']}\n";
    echo "Age: {$data['age']}\n";
    echo "Skills: " . implode(', ', $data['skills']) . "\n";
}, [['name' => 'Bob', 'age' => 25, 'skills' => ['PHP', 'C', 'JavaScript']]]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Name: Bob
Age: 25
Skills: PHP, C, JavaScript
Done
