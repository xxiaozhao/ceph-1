"""
Thrash mds by simulating failures
"""
import logging
import contextlib

from gevent import sleep, GreenletExit
from gevent.greenlet import Greenlet
from gevent.event import Event
from teuthology import misc as teuthology

from tasks import ceph_manager
from tasks.cephfs.filesystem import MDSCluster, Filesystem
from tasks.thrasher import Thrasher

import random
import math
import errno
import json

from io import StringIO

log = logging.getLogger(__name__)

class Quiescer(Thrasher, Greenlet):
    """
    The Quiescer does periodic quiescing of the configured paths, by default - the root '/'.

    quiesce_timeout: maximum time in seconds to wait for the quiesce to succeed
    quiesce_factor: the percentage of time we allow the system to stay quiesced
    min_quiesce: the minimum pause time in seconds
    max_quiesce: the maximum pause time in seconds
    initial_delay: the time in seconds before the first quiesce
    """

    MAX_QUIESCE_FACTOR = 0.5    # 50%
    MIN_QUIESCE_FACTOR = 0.005  # 0.5%

    def __init__(self, fs, quiesce_timeout=30, quiesce_factor=0.1, min_quiesce=10, max_quiesce=60, initial_delay=120, **unused_kwargs):
        super(Quiescer, self).__init__()

        self.logger = log.getChild('fs.[{f}]'.format(f=fs.name))
        self.fs = fs
        self.name = 'quiescer.fs.[{f}]'.format(f=fs.name)
        self.stopping = Event()

        self.quiesce_timeout = quiesce_timeout

        if (quiesce_factor > self.MAX_QUIESCE_FACTOR):
            self.logger.warn("Capping the quiesce factor at %f (requested: %f)" % (self.MAX_QUIESCE_FACTOR, quiesce_factor))
            quiesce_factor = self.MAX_QUIESCE_FACTOR

        if quiesce_factor < self.MIN_QUIESCE_FACTOR:
            self.logger.warn("setting the quiesce factor to %f (requested: %f)" % (self.MIN_QUIESCE_FACTOR, quiesce_factor))
            quiesce_factor = self.MIN_QUIESCE_FACTOR


        self.quiesce_factor = math.max(0.01, quiesce_factor)
        self.min_quiesce = min_quiesce
        self.max_quiesce = max_quiesce
        self.initial_delay = initial_delay

    def next_quiesce_duration(self):
        mu = (self.min_quiesce + self.max_quiesce) / 2
        sigma = 3 * math.sqrt(self.max_quiesce - self.min_quiesce)
        duration = random.gauss(mu, sigma)
        duration = math.max(duration, self.min_quiesce)
        duration = math.min(duration, self.max_quiesce)
        return duration

    def tell_quiesce_leader(self, *args):
        leader = None
        rc = None
        stdout = None

        while leader is None and not self.stopping.is_set():
            leader = self.fs.get_var('qdb_leader')
            if leader is None:
                self.logger.warn("Couldn't get quiesce db leader from the mds map")
                self.stopping.wait(5)

        while leader is not None and not self.stopping.is_set():
            # We use the one_shot here to cover for cases when the mds crashes
            # without this parameter the client may get stuck awaiting response from a dead MDS
            command = ['tell', f"mds.{leader}", 'quiesce', 'db']
            command.extend(*args)
            self.logger.info("Running ceph command: '%s'" % " ".join(command))
            result = self.fs.run_ceph_cmd(args=command, stdout=StringIO())
            rc, stdout = result.exitstatus, result.stdout.getvalue()
            if rc == -errno.ENOTTY:
                try:
                    resp = json.loads(stdout)
                    leader = int(resp['leader'])
                    self.logger.info("Retrying a quiesce db command with leader %d" % leader)
                except Exception as e:
                    self.logger.error("Couldn't parse ENOTTY response from an mds with error: %s\n%s" % (str(e), stdout))
                    self.stopping.wait(5)
            else:
                break

        return (rc, stdout)

    
    def do_quiesce(self, duration):

        # quiesce the root
        rc, stdout = self.tell_quiesce_leader(
            "/", # quiesce at the root
            "--timeout", str(self.quiesce_timeout), 
            "--expiration", str(duration + 60), # give us a minute to run the release command
            "--await" # block until quiesced (or timedout)
        )

        if self.stopping.is_set():
            return
        
        if rc != 0:
            rcinfo = f"{-rc} ({errno.errorcode.get(-rc, 'Unknown')})"
            self.logger.error(f"Coulding quiesce root with rc: {rcinfo}, stdout:\n{stdout}")
            raise RuntimeError(f"Error quiescing root: {rcinfo}")
        
        try:
            response = json.loads(stdout)
            set_id = response["sets"].keys()[0]
        except Exception as e:
            self.logger.error(f"Coulding parse response with error {e}; stdout:\n{stdout}")
            raise RuntimeError(f"Error parsing quiesce response: {e}")

        self.logger.info(f"Successfully quiesced, set_id: {set_id}, quiesce duration: {duration}")
        self.stopping.wait(duration)

        # release the root
        rc, stdout = self.tell_quiesce_leader(
            "--set-id", set_id,
            "--release",
            "--await"
        )
        
        if rc != 0:
            rcinfo = f"{-rc} ({errno.errorcode.get(-rc, 'Unknown')})"
            self.logger.error(f"Coulding release root with rc: {rcinfo}, stdout:\n{stdout}")
            raise RuntimeError(f"Error releasing root: {rcinfo}")


    def _run(self):
        try:
            self.stopping.wait(self.initial_delay)

            while not self.stopping.is_set():
                duration = self.next_quiesce_duration()
                self.do_quiesce(duration)
                # now we sleep to maintain the quiesce factor
                self.stopping.wait((duration/self.quiesce_factor) - duration)

        except Exception as e:
            self.set_thrasher_exception(e)
            self.logger.exception("exception:")
            # allow successful completion so gevent doesn't see an exception...

    def stop(self):
        self.tell_quiesce_leader( "--cancel", "--all" )
        self.stopping.set()


def stop_all_quiescers(thrashers):
    for thrasher in thrashers:
        if not isinstance(thrasher, Quiescer):
            continue
        thrasher.stop()
        thrasher.join()
        if thrasher.exception is not None:
            raise RuntimeError(f"error during scrub thrashing: {thrasher.exception}")


@contextlib.contextmanager
def task(ctx, config):
    """
    Stress test the mds by randomly quiescing the whole FS while another task/workunit
    is running.
    Example config:

    - fwd_scrub:
      scrub_timeout: 300
      sleep_between_iterations: 1
    """

    mds_cluster = MDSCluster(ctx)

    if config is None:
        config = {}
    assert isinstance(config, dict), \
        'quiescer task only accepts a dict for configuration'
    mdslist = list(teuthology.all_roles_of_type(ctx.cluster, 'mds'))
    assert len(mdslist) > 0, \
        'quiescer task requires at least 1 metadata server'

    (first,) = ctx.cluster.only(f'mds.{mdslist[0]}').remotes.keys()
    manager = ceph_manager.CephManager(
        first, ctx=ctx, logger=log.getChild('ceph_manager'),
    )

    # make sure everyone is in active, standby, or standby-replay
    log.info('Wait for all MDSs to reach steady state...')
    status = mds_cluster.status()
    while True:
        steady = True
        for info in status.get_all():
            state = info['state']
            if state not in ('up:active', 'up:standby', 'up:standby-replay'):
                steady = False
                break
        if steady:
            break
        sleep(2)
        status = mds_cluster.status()

    log.info('Ready to start quiesce thrashing')

    manager.wait_for_clean()
    assert manager.is_clean()

    if 'cluster' not in config:
        config['cluster'] = 'ceph'

    for fs in status.get_filesystems():
        quiescer = Quiescer(Filesystem(ctx, fscid=fs['id']), **config)
        quiescer.start()
        ctx.ceph[config['cluster']].thrashers.append(quiescer)

    try:
        log.debug('Yielding')
        yield
    finally:
        log.info('joining Quiescers')
        stop_all_quiescers(ctx.ceph[config['cluster']].thrashers)
        log.info('done joining Quiescers')
