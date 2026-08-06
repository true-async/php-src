--TEST--
A bailout while printing -i output shuts the request down instead of running a script
--SKIPIF--
<?php
if (!getenv('TEST_PHP_EXECUTABLE_ESCAPED')) die("skip TEST_PHP_EXECUTABLE_ESCAPED not defined");
if (!function_exists("proc_open")) die("skip no proc_open");
?>
--FILE--
<?php
$marker = __DIR__ . '/cli_info_bailout_marker.tmp';
@unlink($marker);

$stdin = '<?php file_put_contents(' . var_export($marker, true) . ', "ran"); ?>';

$proc = proc_open(
    getenv('TEST_PHP_EXECUTABLE_ESCAPED') . ' -n -i',
    [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
    $pipes
);

fwrite($pipes[0], $stdin);
fclose($pipes[0]);

// Close the output pipe while phpinfo() is still being written: the failing
// write aborts the connection and bails out of the option handler.
fclose($pipes[1]);

$stderr = stream_get_contents($pipes[2]);
fclose($pipes[2]);
proc_close($proc);

var_dump(file_exists($marker));
var_dump(str_contains($stderr, 'memory leaks detected'));

@unlink($marker);
?>
--EXPECT--
bool(false)
bool(false)
