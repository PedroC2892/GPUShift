// SPDX-License-Identifier: GPL-3.0-or-later
#include "confirmdialog.h"

#include "gpushift/gpushift.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

namespace {

constexpr int kSeconds = 30;

} // namespace

int runConfirmation()
{
	gs_system *sys = gs_detect();
	const bool awaiting = sys && gs_awaiting_confirmation(sys);
	gs_system_free(sys);
	if (!awaiting)
		return 0;

	QMessageBox box(QMessageBox::Question, QCoreApplication::translate("Confirmation", "GPUShift"),
			QCoreApplication::translate("Confirmation", "The GPU mode was changed. Is the display working correctly?"));
	auto *keep = box.addButton(QCoreApplication::translate("Confirmation", "&Keep"), QMessageBox::AcceptRole);
	box.addButton(QCoreApplication::translate("Confirmation", "&Revert"), QMessageBox::RejectRole);
	box.setDefaultButton(keep);
	box.setWindowFlag(Qt::WindowStaysOnTopHint);

	int remaining = kSeconds;
	const auto updateText = [&] {
		box.setInformativeText(QCoreApplication::translate("Confirmation", "Reverting to the previous mode in %1 seconds.").arg(remaining));
	};
	updateText();
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, &box, [&] {
		if (--remaining <= 0)
			box.reject();
		else
			updateText();
	});
	timer.start(1000);
	box.exec();
	timer.stop();

	if (box.clickedButton() == keep) {
		const gs_status st = gs_confirm();
		return st == GS_OK ? 0 : st;
	}
	const gs_status st = gs_revert();
	if (st == GS_OK)
		QMessageBox::information(nullptr, QCoreApplication::translate("Confirmation", "GPUShift"),
					 QCoreApplication::translate("Confirmation", "The previous GPU mode was restored. Reboot to use it."));
	else
		QMessageBox::warning(nullptr, QCoreApplication::translate("Confirmation", "GPUShift"),
				     QCoreApplication::translate("Confirmation", "Could not revert the GPU mode (error %1). Run 'sudo gpushift reset' "
					"from a terminal.").arg(static_cast<int>(st)));
	return st;
}
