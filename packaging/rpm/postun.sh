if [ "$1" -eq 0 ]; then
	# A state left behind means the changes were not undone: keep the backups.
	[ -e /var/lib/gpushift/state ] || rm -rf /var/lib/gpushift
	rm -rf /run/gpushift
fi
if [ -x /usr/bin/systemctl ]; then
	systemctl daemon-reload >/dev/null 2>&1 || :
fi
