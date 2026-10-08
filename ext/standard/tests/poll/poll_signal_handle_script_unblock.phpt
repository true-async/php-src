--TEST--
Io\Poll\SignalHandle: an unblock the script asks for during the watch is done at the last removal unless it blocks the signal again
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
?>
--FILE--
<?php
use Io\Poll\{Context, Event, SignalHandle};

// SIGCHLD only because the call refuses an empty list
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    pcntl_sigprocmask(SIG_SETMASK, $mask);
    return in_array($signo, $mask, true);
}

function watch(Context $ctx): Io\Poll\Watcher
{
    return $ctx->add(new SignalHandle([SIGUSR1]), [Event::Signal]);
}

$ctx = new Context();

echo "Blocked before the watch, SIG_UNBLOCK during it\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = watch($ctx);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
$watcher->remove();
var_dump(is_blocked(SIGUSR1));

echo "Blocked before the watch, SIG_SETMASK without it during the watch\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = watch($ctx);
pcntl_sigprocmask(SIG_SETMASK, []);
$watcher->remove();
var_dump(is_blocked(SIGUSR1));

echo "Blocked before the watch, mask saved and restored during it\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = watch($ctx);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR2], $saved);
pcntl_sigprocmask(SIG_SETMASK, $saved);
$watcher->remove();
var_dump(is_blocked(SIGUSR1), is_blocked(SIGUSR2));
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);

echo "Unblocked before the watch, mask saved and restored during it\n";
$watcher = watch($ctx);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR2], $saved);
pcntl_sigprocmask(SIG_SETMASK, $saved);
$watcher->remove();
var_dump(is_blocked(SIGUSR1), is_blocked(SIGUSR2));

echo "Two handles, SIG_UNBLOCK while both watch\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$first = watch($ctx);
$second = watch($ctx);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
$first->remove();
$second->remove();
var_dump(is_blocked(SIGUSR1));

echo "Blocked before the watch, unblocked and blocked again during it\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = watch($ctx);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher->remove();
var_dump(is_blocked(SIGUSR1));

echo "Blocked before the watch, mask cleared and restored during it\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = watch($ctx);
pcntl_sigprocmask(SIG_SETMASK, [], $saved);
pcntl_sigprocmask(SIG_SETMASK, $saved);
$watcher->remove();
var_dump(is_blocked(SIGUSR1));

echo "Blocked before the watch, pcntl_signal() and a block during it\n";
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = watch($ctx);
pcntl_signal(SIGUSR1, SIG_DFL);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher->remove();
var_dump(is_blocked(SIGUSR1));
?>
--EXPECT--
Blocked before the watch, SIG_UNBLOCK during it
bool(false)
Blocked before the watch, SIG_SETMASK without it during the watch
bool(false)
Blocked before the watch, mask saved and restored during it
bool(true)
bool(false)
Unblocked before the watch, mask saved and restored during it
bool(false)
bool(false)
Two handles, SIG_UNBLOCK while both watch
bool(false)
Blocked before the watch, unblocked and blocked again during it
bool(true)
Blocked before the watch, mask cleared and restored during it
bool(true)
Blocked before the watch, pcntl_signal() and a block during it
bool(true)
