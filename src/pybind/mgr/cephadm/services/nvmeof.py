import errno
import logging
import json
from typing import List, cast, Optional

from mgr_module import HandleCommandResult
from ceph.deployment.service_spec import NvmeofServiceSpec

from orchestrator import DaemonDescription, DaemonDescriptionStatus
from .cephadmservice import CephadmDaemonDeploySpec, CephService
from .. import utils

logger = logging.getLogger(__name__)


class NvmeofService(CephService):
    TYPE = 'nvmeof'
    PROMETHEUS_PORT = 10008

    def config(self, spec: NvmeofServiceSpec) -> None:  # type: ignore
        assert self.TYPE == spec.service_type
        if not spec.pool:
            self.mgr.log.error(f"nvmeof config pool should be defined: {spec.pool}")
            return False
        self.pool = spec.pool
        if spec.group is None:
            self.mgr.log.error(f"nvmeof config group should not be None: {spec.group}")
            return False
        self.group = spec.group
        # unlike some other config funcs, if this fails we can't
        # go forward deploying the daemon and then retry later. For
        # that reason we make no attempt to catch the OrchestratorError
        # this may raise
        self.mgr._check_pool_exists(spec.pool, spec.service_name())

    def prepare_create(self, daemon_spec: CephadmDaemonDeploySpec) -> CephadmDaemonDeploySpec:
        assert self.TYPE == daemon_spec.daemon_type

        spec = cast(NvmeofServiceSpec, self.mgr.spec_store[daemon_spec.service_name].spec)
        nvmeof_gw_id = daemon_spec.daemon_id
        host_ip = self.mgr.inventory.get_addr(daemon_spec.host)

        keyring = self.get_keyring_with_caps(self.get_auth_entity(nvmeof_gw_id),
                                             ['mon', 'profile rbd',
                                              'osd', 'profile rbd'])

        # TODO: check if we can force jinja2 to generate dicts with double quotes instead of using json.dumps
        transport_tcp_options = json.dumps(spec.transport_tcp_options) if spec.transport_tcp_options else None
        name = '{}.{}'.format(utils.name_to_config_section('nvmeof'), nvmeof_gw_id)
        rados_id = name[len('client.'):] if name.startswith('client.') else name
        context = {
            'spec': spec,
            'name': name,
            'addr': host_ip,
            'port': spec.port,
            'log_level': 'WARN',
            'rpc_socket': '/var/tmp/spdk.sock',
            'transport_tcp_options': transport_tcp_options,
            'rados_id': rados_id
        }
        gw_conf = self.mgr.template.render('services/nvmeof/ceph-nvmeof.conf.j2', context)

        daemon_spec.keyring = keyring
        daemon_spec.extra_files = {'ceph-nvmeof.conf': gw_conf}
        daemon_spec.final_config, daemon_spec.deps = self.generate_config(daemon_spec)
        daemon_spec.deps = []
        if not hasattr(self, 'gws'):
            self.gws = {} # id -> name map of gateways for this service.
        self.gws[nvmeof_gw_id] = name # add to map of service's gateway names
        return daemon_spec

    def daemon_check_post(self, daemon_descrs: List[DaemonDescription]) -> None:
        """ Overrides the daemon_check_post to add nvmeof gateways safely
        """
        self.mgr.log.info(f"nvmeof daemon_check_post {daemon_descrs}")
        # Assert configured
        if not self.pool or self.group is None:
            self.mgr.log.error(f"nvmeof daemon_check_post: invalid pool {self.pool} or group {self.group}")
        assert self.pool
        assert self.group is not None
        for dd in daemon_descrs:
            self.mgr.log.info(f"nvmeof daemon_descr {dd}")
            assert dd.daemon_id in self.gws
            name = self.gws[dd.daemon_id]
            self.mgr.log.info(f"nvmeof daemon name={name}")
            # Notify monitor about this gateway creation
            cmd = {
                'prefix': 'nvme-gw create',
                'id': name,
                'group': self.group,
                'pool': self.pool
            }
            self.mgr.log.info(f"create gateway: monitor command {cmd}")
            _, _, err = self.mgr.mon_command(cmd)
            if err:
                self.mgr.log.error(f"Unable to send monitor command {cmd}, error {err}")
        super().daemon_check_post(daemon_descrs)

    def config_dashboard(self, daemon_descrs: List[DaemonDescription]) -> None:
        # TODO: what integration do we need with the dashboard?
        pass

    def ok_to_stop(self,
                   daemon_ids: List[str],
                   force: bool = False,
                   known: Optional[List[str]] = None) -> HandleCommandResult:
        # if only 1 nvmeof, alert user (this is not passable with --force)
        warn, warn_message = self._enough_daemons_to_stop(self.TYPE, daemon_ids, 'Nvmeof', 1, True)
        if warn:
            return HandleCommandResult(-errno.EBUSY, '', warn_message)

        # if reached here, there is > 1 nvmeof daemon. make sure none are down
        warn_message = ('ALERT: 1 nvmeof daemon is already down. Please bring it back up before stopping this one')
        nvmeof_daemons = self.mgr.cache.get_daemons_by_type(self.TYPE)
        for i in nvmeof_daemons:
            if i.status != DaemonDescriptionStatus.running:
                return HandleCommandResult(-errno.EBUSY, '', warn_message)

        names = [f'{self.TYPE}.{d_id}' for d_id in daemon_ids]
        warn_message = f'It is presumed safe to stop {names}'
        return HandleCommandResult(0, warn_message, '')

    def post_remove(self, daemon: DaemonDescription, is_failed_deploy: bool) -> None:
        """
        Called after the daemon is removed.
        """
        logger.debug(f'Post remove daemon {self.TYPE}.{daemon.daemon_id}')
        # to clean the keyring up
        super().post_remove(daemon, is_failed_deploy=is_failed_deploy)

        # remove config for dashboard nvmeof gateways if any
        ret, out, err = self.mgr.mon_command({
            'prefix': 'dashboard nvmeof-gateway-rm',
            'name': daemon.hostname,
        })
        if not ret:
            logger.info(f'{daemon.hostname} removed from nvmeof gateways dashboard config')

        # Assert configured
        assert self.pool
        assert self.group is not None
        assert daemon.daemon_id in self.gws
        name = self.gws[daemon.daemon_id]
        self.gws.pop(daemon.daemon_id)
        # Notify monitor about this gateway deletion
        cmd = {
            'prefix': 'nvme-gw delete',
            'id': name,
            'group': self.group,
            'pool': self.pool
        }
        self.mgr.log.info(f"delete gateway: monitor command {cmd}")
        _, _, err = self.mgr.mon_command(cmd)
        if err:
            self.mgr.log.error(f"Unable to send monitor command {cmd}, error {err}")

    def purge(self, service_name: str) -> None:
        """Make sure no zombie gateway is left behind
        """
        # Assert configured
        assert self.pool
        assert self.group is not None
        for daemon_id in self.gws:
            name = self.gws[daemon_id]
            self.gws.pop(daemon_id)
            # Notify monitor about this gateway deletion
            cmd = {
                'prefix': 'nvme-gw delete',
                'id': name,
                'group': self.group,
                'pool': self.pool
            }
            self.mgr.log.info(f"purge delete gateway: monitor command {cmd}")
            _, _, err = self.mgr.mon_command(cmd)
            if err:
                self.mgr.log.error(f"Unable to send monitor command {cmd}, error {err}")
