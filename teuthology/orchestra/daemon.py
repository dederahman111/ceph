import logging
import struct

from . import run

log = logging.getLogger(__name__)


class DaemonState(object):
    """
    Daemon State.  A daemon exists for each instance of each role.
    """
    def __init__(self, remote, role, id_, *command_args, **command_kwargs):
        """
        Pass remote command information as parameters to remote site

        :param remote: Remote site
        :param role: Role (osd, rgw, mon, mds)
        :param id_: Id within role (osd.1, osd.2, for eaxmple)
        :param command_args: positional arguments (used in restart commands)
        :param command_kwargs: keyword arguments (used in restart commands)
        """
        self.remote = remote
        self.command_args = command_args
        self.command_kwargs = command_kwargs
        self.role = role
        self.id_ = id_
        self.log = command_kwargs.get('logger', log)
        self.proc = None

    def stop(self, timeout=300):
        """
        Stop this daemon instance.

        Note: this can raise a run.CommandFailedError,
        run.CommandCrashedError, or run.ConnectionLostError.

        :param timeout: timeout to pass to orchestra.run.wait()
        """
        if not self.running():
            self.log.error('tried to stop a non-running daemon')
            return
        self.proc.stdin.close()
        self.log.debug('waiting for process to exit')
        run.wait([self.proc], timeout=timeout)
        self.proc = None
        self.log.info('Stopped')

    def restart(self, *args, **kwargs):
        """
        Restart with a new command passed in the arguments

        :param args: positional arguments passed to remote.run
        :param kwargs: keyword arguments passed to remote.run
        """
        self.log.info('Restarting daemon')
        if self.proc is not None:
            self.log.info('Stopping old one...')
            self.stop()
        cmd_args = list(self.command_args)
        cmd_args.extend(args)
        cmd_kwargs = self.command_kwargs
        cmd_kwargs.update(kwargs)
        self.proc = self.remote.run(*cmd_args, **cmd_kwargs)
        self.log.info('Started')

    def restart_with_args(self, extra_args):
        """
        Restart, adding new paramaters to the current command.

        :param extra_args: Extra keyword arguments to be added.
        """
        self.log.info('Restarting daemon')
        if self.proc is not None:
            self.log.info('Stopping old one...')
            self.stop()
        cmd_args = list(self.command_args)
        # we only want to make a temporary mod of the args list
        # so we shallow copy the dict, and deepcopy the args list
        cmd_kwargs = self.command_kwargs.copy()
        from copy import deepcopy
        cmd_kwargs['args'] = deepcopy(self.command_kwargs['args'])
        cmd_kwargs['args'].extend(extra_args)
        self.proc = self.remote.run(*cmd_args, **cmd_kwargs)
        self.log.info('Started')

    def signal(self, sig):
        """
        Send a signal to associated remote commnad

        :param sig: signal to send
        """
        self.proc.stdin.write(struct.pack('!b', sig))
        self.log.info('Sent signal %d', sig)

    def running(self):
        """
        Are we running?
        :return: True if remote run command value is set, False otherwise.
        """
        return self.proc is not None

    def reset(self):
        """
        clear remote run command value.
        """
        self.proc = None

    def wait_for_exit(self):
        """
        clear remote run command value after waiting for exit.
        """
        if self.proc:
            try:
                run.wait([self.proc])
            finally:
                self.proc = None


class DaemonGroup(object):
    """
    Collection of daemon state instances
    """
    def __init__(self):
        """
        self.daemons is a dictionary indexed by role.  Each entry is a
        dictionary of DaemonState values indexed by an id parameter.
        """
        self.daemons = {}

    def add_daemon(self, remote, role, id_, *args, **kwargs):
        """
        Add a daemon.  If there already is a daemon for this id_ and role, stop
        that daemon and.  Restart the damon once the new value is set.

        :param remote: Remote site
        :param role: Role (osd, mds, mon, rgw,  for example)
        :param id_: Id (index into role dictionary)
        :param args: Daemonstate positional parameters
        :param kwargs: Daemonstate keyword parameters
        """
        if role not in self.daemons:
            self.daemons[role] = {}
        if id_ in self.daemons[role]:
            self.daemons[role][id_].stop()
            self.daemons[role][id_] = None
        self.daemons[role][id_] = DaemonState(remote, role, id_, *args,
                                              **kwargs)
        self.daemons[role][id_].restart()

    def get_daemon(self, role, id_):
        """
        get the daemon associated with this id_ for this role.

        :param role: Role (osd, mds, mon, rgw,  for example)
        :param id_: Id (index into role dictionary)
        """
        if role not in self.daemons:
            return None
        return self.daemons[role].get(str(id_), None)

    def iter_daemons_of_role(self, role):
        """
        Iterate through all daemon instances for this role.  Return dictionary
        of daemon values.

        :param role: Role (osd, mds, mon, rgw,  for example)
        """
        return self.daemons.get(role, {}).values()
