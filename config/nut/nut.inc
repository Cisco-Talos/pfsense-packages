<?php
/*
	nut.inc
	part of pfSense (https://www.pfsense.org/)
	Copyright (C) 2007 Ryan Wagoner <rswagoner@gmail.com>
	Copyright (C) 2015 ESF, LLC
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice,
	   this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright
	   notice, this list of conditions and the following disclaimer in the
	   documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
	AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
	AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
	OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

	require_once("config.inc");
	require_once("functions.inc");
	require_once("util.inc");

	define('NUT_RCFILE', '/usr/local/etc/rc.d/nut.sh');

	global $pfs_version;
	$pfs_version = substr(trim(file_get_contents("/etc/version")),0,3);
	if ($pfs_version == "2.1" || $pfs_version == "2.2") {
		define('NUT_DIR', '/usr/pbi/nut-' . php_uname("m") . '/etc/nut');
	} else {
		define('NUT_DIR','/usr/local/etc/nut');
	}

	function nut_config($name) {
		global $config;
		if ($config['installedpackages']['nut']['config'][0]["{$name}"]) {
			return $config['installedpackages']['nut']['config'][0]["{$name}"];
		} else {
			return null;
		}
	}

	function nut_config_sub($name,$len) {
		global $config;
		if (nut_config($name)) {
			return substr(nut_config($name),0,strlen(nut_config($name))-$len);
		} else {
			return null;
		}
	}

	function nut_write_config($file, $text, $mask = null, $uid = null) {
		$conf = fopen($file, "w");
		if (!$conf) {
			log_error("[nut] ERROR: Could not open {$file} for writing.");
			exit;
		}
		fwrite($conf, $text);
		fclose($conf);
		if ($mask) {
			chmod($file, $mask);
		}
		if ($uid) {
			chown($file, $uid);
		}
	}

	function before_form_nut(&$pkg) {
		/* return available serial ports */

		$serial_types = array("sio", "cua", "tty");
		$ignore_files = array(".lock", ".init");

		foreach ($serial_types as $st) {
			$devices = glob("/dev/{$st}*");
			foreach ($devices as $line) {
				if ($line != '') {
					$ignore = false;
					foreach ($ignore_files as $if) {
						if (strstr($line, $if)) {
							$ignore = true;
							continue;
						}
					}
					if ($ignore == false) {
						$names[] = str_replace("/dev/", "", $line);
						$values[] = $line;
					}
				}
			}
		}

		/* find correct field */
		$i = 0;
		foreach ($pkg['fields']['field'] as $field) {
			if ($field['fieldname'] == 'port') {
				break;
			} else {
				$i++;
			}
		}
		$field = &$pkg['fields']['field'][$i];

		/* add available serial ports */
		for ($i = 0; $i < count($values); $i++) {
			$field['options']['option'][] = array('name' => $names[$i], 'value' => $values[$i]);
		}
	}

	function validate_form_nut($post, &$input_errors) {
		global $config;

		/* monitor remote validation */
		if ($post['monitor'] == 'remote') {
			if (!$post['remotename']) {
				$input_errors[] = 'You must specify a name in the \'Remote UPS Name\' field';
			}
			if ($post['remotename'] && !preg_match("/^[a-zA-Z0-9-_]+$/", $post['remotename'])) {
				$input_errors[] = 'Only [Aa-Zz], [0-9], and [-_] accepted in the \'Remote UPS Name\' field';
			}
			if (!$post['remoteaddr'] || !is_ipaddr($post['remoteaddr'])) {
				$input_errors[] = 'You must specify a valid address \'Remote UPS Address\' field';
			}
			if (!$post['remoteuser']) {
				$input_errors[] = 'You must specify a name in the \'Remote UPS User\' field';
			}
			if (!$post['remotepass']) {
				$input_errors[] = 'You must specify a name in the \'Remote UPS Password\' field';
			}
		} elseif ($post['monitor'] == 'local') {
			/* monitor local validation */
			if (!$post['name']) {
				$input_errors[] = 'You must specify a name in the \'Local UPS Name\' field';
			}
			if ($post['name'] && !preg_match("/^[a-zA-Z0-9-_]+$/", $post['name'])) {
				$input_errors[] = 'Only [Aa-Zz], [0-9], and [-_] accepted in the \'Local UPS Name\' field';
			}
			if (!$post['driver']) {
				$input_errors[] = 'You must select a driver in the \'Local UPS Driver\' field';
			}
			if (!$post['port']) {
				$input_errors[] = 'You must select a port in the \'Local UPS Port\' field';
			}
		}
	}

	function deinstall_package_nut() {
		unlink_if_exists(NUT_DIR.'/upsmon.conf');
		unlink_if_exists(NUT_DIR.'/ups.conf');
		unlink_if_exists(NUT_DIR.'/upsd.conf');
		unlink_if_exists(NUT_DIR.'/upsd.users');

		exec('/bin/rm -rf /var/db/nut');
	}

	function sync_package_nut_remote() {
		$remotename = nut_config('remotename');
		$remoteaddr = nut_config('remoteaddr');
		$remoteuser = nut_config('remoteuser');
		$remotepass = nut_config('remotepass');
		$shutdownflag = (nut_config('powerdown') == 'on') ? '-p' : '-h';
		$custom_upsmonconf = str_replace(";", "\n", nut_config('custom_upsmonconf'));

		if (!($remotename && $remoteaddr && $remoteuser && $remotepass)) {
			return false;
		}

		/* upsmon.conf */
		$upsmon_conf = <<<EOD
MONITOR {$remotename}@{$remoteaddr} 1 {$remoteuser} {$remotepass} slave
MINSUPPLIES 1
SHUTDOWNCMD "/sbin/shutdown {$shutdownflag} +0"
POWERDOWNFLAG /etc/killpower
{$custom_upsmonconf}
EOD;

		$stop = <<<EOD
if [ `/bin/pgrep upsmon | /usr/bin/wc -l` != 0 ]; then
		/usr/bin/killall upsmon
		while [ `/bin/pgrep upsmon | /usr/bin/wc -l` != 0 ]; do
			sleep 1
		done
	fi
EOD;

		$start = $stop."\n\t/usr/local/sbin/upsmon {$remotename}@{$remoteaddr}\n";

		/* write out configuration */
		conf_mount_rw();
		nut_write_config(NUT_DIR.'/upsmon.conf', $upsmon_conf, 0640, 'uucp');
		write_rcfile(array(
			'file' => 'nut.sh',
			'start' => $start,
			'stop' => $stop
			)
		);
		conf_mount_ro();

		return true;
	}

	function sync_package_nut_local() {
		global $pfs_version;
		$pfs_version = substr(trim(file_get_contents("/etc/version")),0,3);

		$name = nut_config('name');
		$driver = nut_config_sub('driver', 2);
		$port = nut_config('port');
		$upstype = nut_config_sub('upstype', 3);
		$cable = nut_config_sub('cable', 3);
		$allowuser = nut_config('allowuser');
		$allowpass = nut_config('allowpass');
		$shutdownflag = (nut_config('powerdown') == 'on') ? '-p' : '-h';
		$custom_upsconf = str_replace(";", "\n", nut_config('custom_upsconf'));
		$custom_upsdconf = str_replace(";", "\n", nut_config('custom_upsdconf'));
		$custom_upsdusers = str_replace(";", "\n", nut_config('custom_upsdusers'));
		$custom_upsmonconf = str_replace(";", "\n", nut_config('custom_upsmonconf'));


		if (!($name && $driver && $port)) {
			return false;
		}

		/* some installs are missing the lock dir */
		if (!is_dir('/var/spool/lock')) {
			safe_mkdir('/var/spool');
			safe_mkdir('/var/spool/lock');
			chown('/var/spool/lock', 'uucp');
			chgrp('/var/spool/lock', 'dialer');
		}

		/* determine permissions for port */
		if ($port != 'auto') {
			$port_rel = "chown root {$port}";
			$port_set = "chown uucp {$port}";
		}

		/* ups.conf */
		/* for USB ups run as root */
		if ($port == 'auto') {
			$ups_conf = "user=root\n";
			$ovr_user = '-u root';
		}
		$ups_conf .= "[{$name}]\n";
		$ups_conf .= "driver={$driver}\n";
		$ups_conf .= "port={$port}\n";
		if ($cable) {
			$ups_conf .= "cable={$cable}\n";
		}
		if ($upstype) {
			$ups_conf .= "upstype={$upstype}\n";
		}
		$ups_conf .= "{$custom_upsconf}";

		/* upsd.conf */
		$upsd_conf = "LISTEN 127.0.0.1\n";
		$upsd_conf .= "LISTEN ::1\n";
		$upsd_conf .= "{$custom_upsdconf}";

		$password = uniqid("nut");
		/* upsd.users */
		$upsd_users = "[monuser]\n";
		$upsd_users .= "password = {$password}\n";
		$upsd_users .= "upsmon master\n";
		if ($allowuser && $allowpass) {
			$upsd_users .= "\n[$allowuser]\n";
			$upsd_users .= "password = $allowpass\n";
			$upsd_users .= "upsmon master\n";
		}
		$upsd_users .= "{$custom_upsdusers}";

		if ($pfs_version == "2.1") {
			$upsdrvctl = "/usr/local/libexec/nut/upsdrvctl";
		} else {
			$upsdrvctl = "/usr/local/sbin/upsdrvctl";
		}

		/* upsmon.conf */
		$upsmon_conf = <<<EOD
MONITOR {$name}@localhost 1 monuser {$password} master
MINSUPPLIES 1
SHUTDOWNCMD "/sbin/shutdown {$shutdownflag} +0"
POWERDOWNFLAG /etc/killpower
{$custom_upsmonconf}
EOD;

		$stop = <<<EOD
	if [ `/bin/pgrep upsmon | /usr/bin/wc -l` != 0 ]; then
		echo stopping upsmon
		/usr/bin/killall upsmon
		while [ `/bin/pgrep upsmon | /usr/bin/wc -l` != 0 ]; do
			sleep 1
		done
	fi
	if [ `/bin/pgrep upsd | /usr/bin/wc -l` != 0 ]; then
		echo stopping upsd
		/usr/bin/killall upsd
	fi
	if [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; then
		echo stopping {$driver}
		{$upsdrvctl} stop
	fi
	sleep 1
	if [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; then
		echo forcing {$driver} termination
		/usr/bin/killall {$driver}
		while [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; do
			sleep 1
		done
	fi
	{$port_rel}
EOD;

		$start = <<<EOD
if [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; then
	{$stop}
	fi
	{$port_set}
	echo starting {$driver}
	if {$upsdrvctl} start; then
		echo starting upsd
		/usr/local/sbin/upsd {$ovr_user}
		echo starting upsmon
		/usr/local/sbin/upsmon {$name}@localhost
	else
		echo {$driver} failed to start
	fi
EOD;

		/* write out configuration */
		conf_mount_rw();
		nut_write_config(NUT_DIR.'/ups.conf', $ups_conf);
		nut_write_config(NUT_DIR.'/upsd.conf', $upsd_conf, 0640, 'uucp');
		nut_write_config(NUT_DIR.'/upsd.users', $upsd_users, 0640, 'uucp');
		nut_write_config(NUT_DIR.'/upsmon.conf', $upsmon_conf, 0640, 'uucp');
		write_rcfile(array(
			'file' => 'nut.sh',
			'start' => $start,
			'stop' => $stop
			)
		);
		conf_mount_ro();

		return true;
	}

	function sync_package_nut_snmp() {
		$name = nut_config('snmpname');
		$driver = "snmp-ups";
		$port = nut_config('snmpaddr');
		$shutdownflag = (nut_config('powerdown') == 'on') ? '-p' : '-h';
		$snmpmib = nut_config('snmpmib');
		$snmpversion = nut_config('snmpversion');
		$snmpcommunity = nut_config('snmpcommunity');
		$snmpfreq = nut_config('snmpfreq');
		$snmpdisabletransfer = (nut_config('snmpdisabletransfer') == 'on');
		$allowuser = nut_config('allowuser');
		$allowpass = nut_config('allowpass');
		$custom_upsconf = str_replace(";", "\n", nut_config('custom_upsconf'));
		$custom_upsdconf = str_replace(";", "\n", nut_config('custom_upsdconf'));
		$custom_upsdusers = str_replace(";", "\n", nut_config('custom_upsdusers'));
		$custom_upsmonconf = str_replace(";", "\n", nut_config('custom_upsmonconf'));

		if (!($name && $driver && $port)) {
			return false;
		}

		/* ups.conf */
		$ups_conf = "user=root\n";
		$ovr_user = '-u root';
		$ups_conf .= "[{$name}]\n";
		$ups_conf .= "driver={$driver}\n";
		$ups_conf .= "port={$port}\n";
		if ($snmpmib) {
			$ups_conf .= "mibs={$snmpmib}\n";
		}
		if ($snmpversion) {
			$ups_conf .= "snmp_version={$snmpversion}\n";
		}
		if ($snmpcommunity) {
			$ups_conf .= "community={$snmpcommunity}\n";
		}
		if ($snmpfreq) {
			$ups_conf .= "pollfreq={$snmpfreq}\n";
		}
		if ($snmpdisabletransfer) {
			$ups_conf .= "notransferoids=true\n";
		}
		$ups_conf .= "{$custom_upsconf}";

		/* upsd.conf */
		$upsd_conf = "LISTEN 127.0.0.1\n";
		$upsd_conf .= "LISTEN ::1\n";
		$upsd_conf .= "{$custom_upsdconf}";

		$password = uniqid("nut");
		/* upsd.users */
		$upsd_users = "[monuser]\n";
		$upsd_users .= "password = {$password}\n";
		$upsd_users .= "upsmon master\n";
		if ($allowuser && $allowpass) {
			$upsd_users .= "\n[$allowuser]\n";
			$upsd_users .= "password = $allowpass\n";
			$upsd_users .= "upsmon master\n";
		}
		$upsd_users .= "{$custom_upsdusers}";

		$pfs_version = substr(trim(file_get_contents("/etc/version")),0,3);
		if ($pfs_version == "2.1")
			$upsdrvctl = "/usr/local/libexec/nut/upsdrvctl";
		else
			$upsdrvctl = "/usr/local/sbin/upsdrvctl";

		/* upsmon.conf */
		$upsmon_conf = <<<EOD
MONITOR {$name}@localhost 1 monuser {$password} master
MINSUPPLIES 1
SHUTDOWNCMD "/sbin/shutdown {$shutdownflag} +0"
POWERDOWNFLAG /etc/killpower
{$custom_upsmonconf}
EOD;

		$stop = <<<EOD
	if [ `/bin/pgrep upsmon | /usr/bin/wc -l` != 0 ]; then
		echo stopping upsmon
		/usr/bin/killall upsmon
		while [ `/bin/pgrep upsmon | /usr/bin/wc -l` != 0 ]; do
			sleep 1
		done
	fi
	if [ `/bin/pgrep upsd | /usr/bin/wc -l` != 0 ]; then
		echo stopping upsd
		/usr/bin/killall upsd
	fi
	if [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; then
		echo stopping {$driver}
		{$upsdrvctl} stop
	fi
	sleep 1
	if [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; then
		echo forcing {$driver} termination
		/usr/bin/killall {$driver}
		while [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; do
			sleep 1
		done
	fi
	{$port_rel}
EOD;

		$start = <<<EOD
if [ `/bin/pgrep {$driver} | /usr/bin/wc -l` != 0 ]; then
	{$stop}
	fi
	{$port_set}
	echo starting {$driver}
	if {$upsdrvctl} start; then
		echo starting upsd
		/usr/local/sbin/upsd {$ovr_user}
		echo starting upsmon
		/usr/local/sbin/upsmon {$name}@localhost
	else
		echo {$driver} failed to start
	fi
EOD;

		/* write out configuration */
		conf_mount_rw();
		nut_write_config(NUT_DIR.'/ups.conf', $ups_conf);
		nut_write_config(NUT_DIR.'/upsd.conf', $upsd_conf, 0640, 'uucp');
		nut_write_config(NUT_DIR.'/upsd.users', $upsd_users, 0640, 'uucp');
		nut_write_config(NUT_DIR.'/upsmon.conf', $upsmon_conf, 0640, 'uucp');
		write_rcfile(array(
			'file' => 'nut.sh',
			'start' => $start,
			'stop' => $stop
			)
		);
		conf_mount_ro();

		return true;
	}

	function sync_package_nut() {
		global $g, $config, $input_errors;

		stop_service("nut");

		/* create state path */
		safe_mkdir("/var/db/nut", 0700);
		chown('/var/db/nut', 'uucp');

		if (nut_config('monitor') == 'remote') {
			$return = sync_package_nut_remote();
		} elseif (nut_config('monitor') == 'local') {
			$return = sync_package_nut_local();
		} elseif (nut_config('monitor') == 'snmp') {
			$return = sync_package_nut_snmp();
		}

		/* only start if enabled and system is not booting */
		/* this prevents service from starting / stopping / starting on boot */
		if ((function_exists("platform_booting")) && (!platform_booting()) && (nut_config('monitor'))) {
			log_error("[nut] INFO: Starting service");
			start_service("nut");
		} elseif ((!($g['booting'])) && (nut_config('monitor'))) {
			log_error("[nut] INFO: Starting service");
			start_service("nut");
		} elseif (!$return && file_exists(NUT_RCFILE)) {
			/* no parameters user does not want nut running */
			/* lets stop the service and remove the rc file */

			if (!nut_config('monitor')) {
				log_error("[nut] INFO: Service stopped: nut disabled");
			} else {
				log_error("[nut] WARNING: Service stopped: check configuration.");
			}

			conf_mount_rw();
			unlink_if_exists(NUT_RCFILE);
			unlink_if_exists(NUT_DIR.'/upsmon.conf');
			unlink_if_exists(NUT_DIR.'/ups.conf');
			unlink_if_exists(NUT_DIR.'/upsd.conf');
			unlink_if_exists(NUT_DIR.'/upsd.users');
			exec('/bin/rm -rf /var/db/nut');
			conf_mount_ro();
		}
	}
?>
