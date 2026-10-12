--TEST--
Io\Poll\SignalHandle: a signal unblocked before the watch is unblocked at the last removal after the mask is saved and restored during it
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
?>
--FILE--
<?php
// SIGCHLD only because the call refuses an empty list
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    pcntl_sigprocmask(SIG_SETMASK, $mask);
    return in_array($signo, $mask, true);
}

$ctx = new Io\Poll\Context();
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1, SIGUSR2]);
$watcher = $ctx->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR2], $saved);
pcntl_sigprocmask(SIG_SETMASK, $saved);
$watcher->remove();
var_dump(is_blocked(SIGUSR1), is_blocked(SIGUSR2));
?>
--EXPECT--
bool(false)
bool(false)
