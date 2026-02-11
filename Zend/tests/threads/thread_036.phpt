--TEST--
Thread: Thread class reflection
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
$reflection = new ReflectionClass('Thread');

echo "Class name: " . $reflection->getName() . "\n";
echo "Is final: " . ($reflection->isFinal() ? 'yes' : 'no') . "\n";

$methods = $reflection->getMethods();
echo "Methods:\n";
foreach ($methods as $method) {
    echo "  - " . $method->getName() . "\n";
}
?>
--EXPECT--
Class name: Thread
Is final: yes
Methods:
  - __construct
  - run
  - join
  - kill
  - isSupported
