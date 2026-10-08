if [ -x /usr/bin/systemctl ]; then
	systemctl daemon-reload >/dev/null 2>&1 || :
	systemctl enable gpushift-boot-check.service >/dev/null 2>&1 || :
fi
