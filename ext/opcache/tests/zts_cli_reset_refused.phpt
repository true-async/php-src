--TEST--
opcache_reset() in a ZTS CLI process is refused, and threads keep the cache
--EXTENSIONS--
opcache
--SKIPIF--
<?php
if (!PHP_ZTS) die('skip ZTS only');
if (!function_exists('Async\spawn_thread')) die('skip needs Async\spawn_thread');
?>
--INI--
opcache.enable=1
opcache.enable_cli=1
--FILE--
<?php
// The CLI request lasts as long as the process, so it never stops reading the
// shared memory and a restart could never run.
use function Async\spawn;
use function Async\spawn_thread;
use function Async\await;

spawn(function () {
    var_dump(opcache_reset());

    for ($i = 1; $i <= 2; $i++) {
        $s = await(spawn_thread(fn () => opcache_get_status(false)));
        echo "thread $i: enabled=", var_export($s['opcache_enabled'], true),
            " restart_pending=", var_export($s['restart_pending'], true), "\n";
    }

    echo "main: enabled=", var_export(opcache_get_status(false)['opcache_enabled'], true), "\n";
});
?>
--EXPECTF--
Warning: opcache_reset(): The opcode cache cannot be reset in a CLI process; restart the process, or invalidate scripts with opcache_invalidate() in %s on line %d
bool(false)
thread 1: enabled=true restart_pending=false
thread 2: enabled=true restart_pending=false
main: enabled=true
