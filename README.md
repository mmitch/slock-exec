slock-exec
==========


what is this?
-------------

This is a small hack of [slock](http://tools.suckless.org/slock/) that
runs scripts on the lock/unlock events:

1. ``slock-script-lock`` is run on locking the screen.  
   This is rather pointless and could also be achieved simply by
   putting the ``slock`` invocation in a little script, but it was a
   nice simple task to practice some C stuff.

2. ``slock-script-unlock`` is run after unlocking the screen /and the
   password you entered to unlock the screen is passed to the script
   on STDIN/.


why is this?
------------

The reason for this is that I want to drop my cached identities from
ssh-agent on screen-lock and automatically re-add them after unlocking
the screen as described here:

* https://utcc.utoronto.ca/~cks/space/blog/linux/EncryptedSSHKeyMigration

* or this more general article:
  https://utcc.utoronto.ca/~cks/space/blog/sysadmin/SSHKeyGoodPractices


how to build
------------

Have a look at the ``README`` file.


copyright
---------

See the ``LICENSE`` file for the original copyright notice of ``slock``.


homepage
--------

``slock-exec`` is available at https://github.com/mmitch/slock-exec
