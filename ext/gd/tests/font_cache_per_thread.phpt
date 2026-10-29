--TEST--
The FreeType font cache of one thread is not freed by another thread's request shutdown
--EXTENSIONS--
gd
--SKIPIF--
<?php
if (!PHP_ZTS) die('skip ZTS required');
if (!function_exists('Async\spawn_thread')) die('skip spawn_thread not available');
if (!function_exists('imagettfbbox')) die('skip FreeType support required');
?>
--FILE--
<?php
use function Async\{await, spawn, spawn_thread};

$font = __DIR__ . '/Tuffy.ttf';
// The main thread's cache entries come from its own request heap.
imagettfbbox(12, 0, $font, 'main thread');

spawn(function () use ($font) {
    // A thread that uses FreeType itself, and one that never does: both shut their request down.
    await(spawn_thread(static function () use ($font): void {
        imagettfbbox(12, 0, $font, 'child thread');
    }));
    await(spawn_thread(static function (): void {}));
    echo "threads done\n";
});

var_dump(count(imagettfbbox(12, 0, $font, 'main thread again')));
?>
--EXPECT--
int(8)
threads done
