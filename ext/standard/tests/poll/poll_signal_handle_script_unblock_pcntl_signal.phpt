--TEST--
Io\Poll\SignalHandle: pcntl_signal() during the watch of a blocked signal unblocks it at the last removal
--EXTENSIONS--
posix
pcntl
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
// Without Zend signals, installing a handler leaves the mask as it is
if (ini_get('zend.signal_check') === false) die("skip Zend signals are disabled");
?>
--FILE--
<?php
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$ctx = new Io\Poll\Context();
$watcher = $ctx->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);
pcntl_signal(SIGUSR1, function () { echo "pcntl handler ran\n"; });
$watcher->remove();
posix_kill(posix_getpid(), SIGUSR1);
pcntl_signal_dispatch();
?>
--EXPECT--
pcntl handler ran
