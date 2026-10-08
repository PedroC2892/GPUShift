if [ "$1" -eq 0 ]; then
	rm -rf /var/lib/gpushift /run/gpushift
fi
if [ -x /usr/bin/systemctl ]; then
	systemctl daemon-reload >/dev/null 2>&1 || :
fi
