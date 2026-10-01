--TEST--
Io\Poll\SignalHandle: a pending signal nothing handles is discarded when the handle goes, a handled one is delivered
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
?>
--FILE--
<?php
pcntl_async_signals(true);
pcntl_signal(SIGUSR2, function ($signo) { echo "handler ", $signo === SIGUSR2 ? "SIGUSR2" : $signo, "\n"; });

$h = new Io\Poll\SignalHandle([SIGUSR1, SIGUSR2, SIGTERM]);
posix_kill(posix_getpid(), SIGUSR1);
posix_kill(posix_getpid(), SIGUSR2);
posix_kill(posix_getpid(), SIGTERM);
echo "pending\n";
unset($h);
pcntl_signal_dispatch();
echo "alive\n";

// Unblocked again: the default action applies to later deliveries
var_dump(pcntl_sigprocmask(SIG_UNBLOCK, [SIGCHLD], $old), in_array(SIGUSR1, $old), in_array(SIGTERM, $old));
?>
--EXPECT--
pending
handler SIGUSR2
alive
bool(true)
bool(false)
bool(false)
