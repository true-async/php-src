--TEST--
A full cache in a ZTS CLI process stays full instead of waiting for a restart that cannot run
--EXTENSIONS--
opcache
--SKIPIF--
<?php
if (!PHP_ZTS) die('skip ZTS only');
if (!function_exists('Async\spawn_thread')) die('skip needs Async\spawn_thread');
?>
--INI--
memory_limit=1G
opcache.enable=1
opcache.enable_cli=1
opcache.memory_consumption=24
opcache.interned_strings_buffer=8
opcache.jit=disable
opcache.max_accelerated_files=10000
opcache.file_update_protection=0
--FILE--
<?php
// What a hot reload does after enough saves: the cache fills, every script is
// invalidated, and the next compile finds no room with too much memory wasted.
use function Async\spawn;
use function Async\spawn_thread;
use function Async\await;

$dir = sys_get_temp_dir() . '/opcache-oom-' . getmypid();
@mkdir($dir);
register_shutdown_function(function () use ($dir) {
    array_map('unlink', glob("$dir/*.php"));
    @rmdir($dir);
});

for ($i = 0; $i < 100; $i++) {
    file_put_contents("$dir/f$i.php", "<?php\nfunction f{$i}(\$a, \$b) {\n"
        . str_repeat("    \$a = (\$a * 31 + \$b) % 1000003; \$b = \$b + \$a;\n", 1500)
        . "    return \$a + \$b;\n}\n");
}

spawn(function () use ($dir) {
    $files = [];
    for ($i = 0; $i < 100; $i++) {
        opcache_compile_file("$dir/f$i.php");
        if (!opcache_is_script_cached("$dir/f$i.php")) {
            break;
        }
        $files[] = "$dir/f$i.php";
    }

    echo "filled: ", count($files) > 10 && count($files) < 95 ? 'yes' : 'no: ' . count($files), "\n";

    foreach ($files as $f) {
        opcache_invalidate($f, true);
    }
    opcache_compile_file("$dir/f" . (count($files) + 2) . ".php");

    $s = opcache_get_status(false);
    echo "main: restart_pending=", var_export($s['restart_pending'], true),
        " cache_full=", var_export($s['cache_full'], true), "\n";

    $t = await(spawn_thread(fn () => opcache_get_status(false)));
    echo "thread: enabled=", var_export($t['opcache_enabled'], true),
        " restart_pending=", var_export($t['restart_pending'], true), "\n";
});
?>
--EXPECT--
filled: yes
main: restart_pending=false cache_full=true
thread: enabled=true restart_pending=false
