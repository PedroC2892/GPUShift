/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPUSHIFT_OPS_H
#define GPUSHIFT_OPS_H

#include "../lib/internal.h"

gs_status gs_op_init(void);
gs_status gs_op_apply(gs_system *sys, gs_mode mode);
/* force: emergency reset that goes on without a new initramfs or MUX change. */
gs_status gs_op_reset(gs_system *sys, bool force);
gs_status gs_op_confirm(gs_system *sys);
gs_status gs_op_revert(gs_system *sys);
/* Runs once per boot; reboot_fn is injected so tests never reboot. */
gs_status gs_op_boot_check(gs_system *sys, int (*reboot_fn)(void));
/* images.c: checks space, then regenerates with backup, validation and restore. */
gs_status gs_initramfs_preflight(void);
gs_status gs_regenerate_initramfs(void);
void gs_op_msg(const char *text);
void gs_op_log(const char *text); /* stderr (journal) and /var/lib/gpushift/log */

#endif
