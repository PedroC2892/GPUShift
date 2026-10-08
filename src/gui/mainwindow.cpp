// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#include <memory>

namespace {

QString orUnknown(const char *s)
{
	return s ? QString::fromUtf8(s) : MainWindow::tr("Unknown");
}

QLabel *valueLabel(const QString &text)
{
	auto *label = new QLabel(text);
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	label->setWordWrap(true);
	return label;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	setWindowTitle(tr("GPUShift"));
	setWindowIcon(QIcon::fromTheme(QStringLiteral("gpushift")));
	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	setCentralWidget(m_scroll);
	resize(640, 760);
	refresh();
}

MainWindow::~MainWindow()
{
	gs_system_free(m_sys);
}

void MainWindow::refresh()
{
	gs_system_free(m_sys);
	m_sys = gs_detect();
	m_modes = nullptr;
	m_apply = m_reset = nullptr;
	m_busy = nullptr;
	if (!m_sys) {
		m_scroll->setWidget(notice(tr("Out of memory while detecting the GPUs."), true));
		return;
	}
	m_scroll->setWidget(buildContent()); // deletes the previous content
}

QWidget *MainWindow::buildContent()
{
	auto *content = new QWidget;
	auto *layout = new QVBoxLayout(content);

	for (size_t i = 0; i < gs_conflict_count(m_sys); i++)
		layout->addWidget(notice(tr("Conflicting tool active: %1. Remove or disable it before "
					    "using GPUShift; changes are blocked.")
					 .arg(QString::fromUtf8(gs_conflict_name(m_sys, i))), true));
	layout->addWidget(buildModeBox());
	layout->addWidget(buildSystemBox());
	for (size_t i = 0; i < gs_gpu_count(m_sys); i++)
		layout->addWidget(buildGpuBox(i));
	layout->addStretch();
	return content;
}

QWidget *MainWindow::notice(const QString &text, bool warning)
{
	auto *frame = new QFrame;
	frame->setFrameShape(QFrame::StyledPanel);
	auto *layout = new QHBoxLayout(frame);
	const int size = style()->pixelMetric(QStyle::PM_SmallIconSize);
	auto *icon = new QLabel;
	icon->setPixmap(style()->standardIcon(warning ? QStyle::SP_MessageBoxWarning
						     : QStyle::SP_MessageBoxInformation).pixmap(size, size));
	icon->setAlignment(Qt::AlignTop);
	auto *label = new QLabel(text);
	label->setWordWrap(true);
	layout->addWidget(icon);
	layout->addWidget(label, 1);
	return frame;
}

QGroupBox *MainWindow::buildSystemBox()
{
	auto *box = new QGroupBox(tr("System"));
	auto *form = new QFormLayout(box);
	const char *session = gs_sys_session_type(m_sys), *desktop = gs_sys_desktop(m_sys);

	form->addRow(tr("Distribution:"), valueLabel(QString::fromUtf8(gs_sys_distro(m_sys))));
	form->addRow(tr("Kernel:"), valueLabel(QString::fromUtf8(gs_sys_kernel(m_sys))));
	form->addRow(tr("Chassis:"), valueLabel(gs_sys_is_laptop(m_sys)
		? tr("%1 (laptop)").arg(QString::fromUtf8(gs_sys_chassis_name(m_sys)))
		: QString::fromUtf8(gs_sys_chassis_name(m_sys))));
	form->addRow(tr("Session:"), valueLabel(desktop
		? tr("%1 (%2)").arg(orUnknown(session), QString::fromUtf8(desktop))
		: orUnknown(session)));

	QString secureBoot;
	switch (gs_sys_secure_boot(m_sys)) {
	case GS_SECURE_BOOT_ENABLED: secureBoot = tr("Enabled"); break;
	case GS_SECURE_BOOT_DISABLED: secureBoot = tr("Disabled"); break;
	case GS_SECURE_BOOT_LEGACY_BIOS: secureBoot = tr("Not supported (legacy BIOS)"); break;
	default: secureBoot = tr("Unknown"); break;
	}
	form->addRow(tr("Secure Boot:"), valueLabel(secureBoot));
	if (const char *mux = gs_sys_mux_backend(m_sys))
		form->addRow(tr("Firmware MUX:"), valueLabel(QString::fromUtf8(mux)));
	if (gs_sys_switcheroo(m_sys))
		form->addRow(tr("switcheroo-control:"), valueLabel(tr("Running (compatible)")));
	return box;
}

QGroupBox *MainWindow::buildGpuBox(size_t index)
{
	const gs_gpu *g = gs_gpu_at(m_sys, index);
	auto *box = new QGroupBox(tr("GPU %1: %2").arg(index).arg(QString::fromUtf8(gs_gpu_device_name(g))));
	auto *form = new QFormLayout(box);

	form->addRow(tr("Vendor:"), valueLabel(QString::fromUtf8(gs_gpu_vendor_name(g))));
	form->addRow(tr("Model:"), valueLabel(QString::fromUtf8(gs_gpu_device_name(g))));
	form->addRow(tr("PCI address:"), valueLabel(QStringLiteral("%1 [%2:%3]")
		.arg(QString::fromUtf8(gs_gpu_address(g)))
		.arg(gs_gpu_vendor_id(g), 4, 16, QLatin1Char('0'))
		.arg(gs_gpu_device_id(g), 4, 16, QLatin1Char('0'))));

	QString kind;
	switch (gs_gpu_get_kind(g)) {
	case GS_GPU_INTEGRATED: kind = tr("Integrated", "GPU type"); break;
	case GS_GPU_DEDICATED: kind = tr("Dedicated", "GPU type"); break;
	default: kind = tr("Unknown"); break;
	}
	if (gs_gpu_boot_vga(g))
		kind = tr("%1 (boot VGA)").arg(kind);
	form->addRow(tr("Type:"), valueLabel(kind));

	if (gs_gpu_removed(g)) {
		form->addRow(tr("State:"), valueLabel(tr("Powered off and removed from the PCI bus by GPUShift (Integrated mode)")));
		return box;
	}
	form->addRow(tr("Driver:"), valueLabel(gs_gpu_driver(g) ? QString::fromUtf8(gs_gpu_driver(g))
								: tr("None (no driver loaded)")));
	if (gs_gpu_driver(g))
		form->addRow(tr("Driver version:"), valueLabel(orUnknown(gs_gpu_driver_version(g))));
	form->addRow(tr("Power:"), valueLabel(tr("%1, %2").arg(orUnknown(gs_gpu_runtime_status(g)),
							       orUnknown(gs_gpu_power_state(g)))));
	form->addRow(tr("VRAM:"), valueLabel(gs_gpu_vram_bytes(g)
		? QLocale().formattedDataSize(static_cast<qint64>(gs_gpu_vram_bytes(g)), 1, QLocale::DataSizeTraditionalFormat)
		: tr("Unknown")));
	form->addRow(tr("Internal display:"), valueLabel(gs_gpu_internal_display(g) ? tr("Yes") : tr("No")));
	return box;
}

QGroupBox *MainWindow::buildModeBox()
{
	auto *box = new QGroupBox(tr("Mode"));
	auto *layout = new QVBoxLayout(box);
	const gs_switch sw = gs_switchability(m_sys);
	const gs_mode current = gs_current_mode(m_sys), pending = gs_pending_mode(m_sys);
	gs_mode modes[GS_MODE_COUNT];
	const size_t count = gs_list_modes(m_sys, modes);

	if (sw != GS_SWITCH_OK)
		layout->addWidget(notice(switchMessage(sw), sw == GS_SWITCH_CONFLICT));
	if (current != GS_MODE_NONE)
		layout->addWidget(valueLabel(tr("Current mode: <b>%1</b>").arg(modeTitle(current))));
	if (gs_awaiting_confirmation(m_sys))
		layout->addWidget(notice(tr("This mode change is not confirmed yet (%1 of %2 boots). It is "
					    "reverted automatically unless confirmed: run 'gpushift confirm' "
					    "if the display works.")
					 .arg(gs_unconfirmed_boots(m_sys)).arg(GS_MAX_BOOT_ATTEMPTS), true));
	if (pending == GS_MODE_DEFAULT)
		layout->addWidget(notice(tr("Reboot pending: the GPUShift configuration was removed."), true));
	else if (pending != GS_MODE_NONE)
		layout->addWidget(notice(tr("Reboot pending: %1 mode becomes active after a reboot.")
					 .arg(modeTitle(pending)), true));
	if (count == 0)
		return box; // single GPU or desktop: information only, no controls

	const bool canChange = sw == GS_SWITCH_OK && gs_sys_initramfs_tool(m_sys);
	if (sw == GS_SWITCH_OK && !gs_sys_initramfs_tool(m_sys))
		layout->addWidget(notice(statusMessage(GS_ERR_NO_INITRAMFS), true));

	const gs_mode selected = pending != GS_MODE_NONE && pending != GS_MODE_DEFAULT ? pending : current;
	m_modes = new QButtonGroup(box);
	for (size_t i = 0; i < count; i++) {
		auto *radio = new QRadioButton(modeTitle(modes[i]));
		radio->setChecked(modes[i] == selected);
		radio->setEnabled(canChange);
		auto *description = new QLabel(modeDescription(modes[i]));
		description->setWordWrap(true);
		description->setIndent(style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth) +
				       style()->pixelMetric(QStyle::PM_RadioButtonLabelSpacing));
		description->setEnabled(canChange);
		m_modes->addButton(radio, static_cast<int>(modes[i]));
		layout->addWidget(radio);
		layout->addWidget(description);
	}

	auto *buttons = new QHBoxLayout;
	m_busy = new QLabel;
	m_reset = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), tr("&Reset"));
	m_reset->setToolTip(tr("Remove every change made by GPUShift and restore the original configuration"));
	m_reset->setEnabled(gs_sys_initramfs_tool(m_sys) != nullptr);
	m_apply = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), tr("&Apply"));
	m_apply->setEnabled(canChange);
	buttons->addWidget(m_busy, 1);
	buttons->addWidget(m_reset);
	buttons->addWidget(m_apply);
	layout->addLayout(buttons);
	connect(m_apply, &QPushButton::clicked, this, &MainWindow::applySelected);
	connect(m_reset, &QPushButton::clicked, this, &MainWindow::resetAll);
	return box;
}

void MainWindow::applySelected()
{
	const auto mode = static_cast<gs_mode>(m_modes->checkedId());
	if (m_modes->checkedId() < 0)
		return;
	QString text = tr("Switch to %1 mode?\n\nThe change takes effect after a reboot. "
			  "Administrator authentication is required.").arg(modeTitle(mode));
	if (mode == GS_MODE_INTEGRATED)
		text += QLatin1String("\n\n") + tr("The dedicated GPU will be powered off. Displays connected "
						 "to its outputs will not work in this mode.");
	text += QLatin1String("\n\n") + recoveryText();
	if (QMessageBox::question(this, tr("Apply GPU mode"), text) != QMessageBox::Yes)
		return;
	runHelper(tr("Applying %1 mode…").arg(modeTitle(mode)),
		  [](gs_mode m) { return gs_apply_mode(m); }, mode);
}

void MainWindow::resetAll()
{
	if (QMessageBox::question(this, tr("Reset GPUShift"),
				  tr("Remove every change made by GPUShift and restore the original "
				     "configuration?\n\nThe change takes effect after a reboot.")) != QMessageBox::Yes)
		return;
	runHelper(tr("Restoring the original configuration…"),
		  [](gs_mode) { return gs_reset(); }, GS_MODE_NONE);
}

// The helper can take minutes (initramfs), so it runs off the GUI thread.
void MainWindow::runHelper(const QString &busyText, gs_status (*task)(gs_mode), gs_mode mode)
{
	m_apply->setEnabled(false);
	m_reset->setEnabled(false);
	m_busy->setText(busyText);
	auto result = std::make_shared<gs_status>(GS_ERR_GENERIC);
	QThread *thread = QThread::create([task, mode, result] { *result = task(mode); });
	connect(thread, &QThread::finished, this, [this, thread, result] {
		thread->deleteLater();
		if (*result == GS_OK)
			QMessageBox::information(this, tr("Reboot required"),
						 tr("Done. Reboot the computer to apply the change."));
		else if (*result != GS_ERR_AUTH)
			QMessageBox::warning(this, tr("GPUShift"), statusMessage(*result));
		refresh();
	});
	thread->start();
}

// Translated version of gs_recovery_text().
QString MainWindow::recoveryText()
{
	return tr("If the screen stays black after rebooting:\n"
		  "1. Wait and reboot: after %1 boots without confirmation the previous mode is restored "
		  "automatically.\n"
		  "2. Press Ctrl+Alt+F3, log in and run: sudo gpushift reset && sudo reboot\n"
		  "3. In the boot menu press 'e', add gpushift.reset=1 to the 'linux' line and boot "
		  "with Ctrl+X or F10.\n"
		  "Full guide: %2").arg(GS_MAX_BOOT_ATTEMPTS)
		.arg(QStringLiteral(GPUSHIFT_DOC_DIR "/RECOVERY.md"));
}

QString MainWindow::modeTitle(gs_mode mode)
{
	switch (mode) {
	case GS_MODE_INTEGRATED: return tr("Integrated", "GPU mode");
	case GS_MODE_HYBRID: return tr("Hybrid", "GPU mode");
	case GS_MODE_DEDICATED: return tr("Dedicated", "GPU mode");
	case GS_MODE_DEFAULT: return tr("Default", "GPU mode");
	default: return tr("None", "GPU mode");
	}
}

QString MainWindow::modeDescription(gs_mode mode)
{
	switch (mode) {
	case GS_MODE_INTEGRATED:
		return tr("Only the integrated GPU is used. The dedicated GPU is powered off for the "
			  "lowest power use; outputs wired to it stop working.");
	case GS_MODE_HYBRID:
		return tr("The integrated GPU drives the display and the dedicated GPU is used on "
			  "demand (PRIME render offload).");
	case GS_MODE_DEDICATED:
		return tr("The firmware MUX routes the display to the dedicated GPU: best "
			  "performance, highest power use.");
	default:
		return QString();
	}
}

QString MainWindow::switchMessage(gs_switch sw)
{
	switch (sw) {
	case GS_SWITCH_SINGLE_GPU:
		return tr("This system has only one GPU, so there are no modes to switch.");
	case GS_SWITCH_NO_GPU:
		return tr("No GPU was detected.");
	case GS_SWITCH_DESKTOP:
		return tr("This is not a laptop. GPU modes are only managed on laptops; the information "
			  "below is shown for reference.");
	case GS_SWITCH_UNSUPPORTED:
		return tr("This GPU combination is not supported: GPUShift needs one integrated GPU "
			  "that can drive the internal display and one dedicated GPU.");
	case GS_SWITCH_CONFLICT:
		return tr("Another GPU switching tool is active. Changes are blocked until it is "
			  "removed or disabled.");
	default:
		return QString();
	}
}

QString MainWindow::statusMessage(gs_status st)
{
	switch (st) {
	case GS_OK: return tr("Success.");
	case GS_ERR_SINGLE_GPU: return switchMessage(GS_SWITCH_SINGLE_GPU);
	case GS_ERR_NOT_SWITCHABLE: return tr("GPU switching is not supported on this system.");
	case GS_ERR_MODE_UNAVAILABLE: return tr("The selected mode is not available on this system.");
	case GS_ERR_CONFLICT: return switchMessage(GS_SWITCH_CONFLICT);
	case GS_ERR_NO_INITRAMFS:
		return tr("No supported initramfs generator (update-initramfs, mkinitcpio, dracut or "
			  "booster) was found, so modes cannot be changed.");
	case GS_ERR_IO: return tr("Could not write the system configuration.");
	case GS_ERR_INITRAMFS_FAILED:
		return tr("Regenerating the initramfs failed. The previous configuration was restored.");
	case GS_ERR_MUX: return tr("Could not change the firmware GPU MUX.");
	case GS_ERR_PERMISSION: return tr("Administrator privileges are required, but pkexec was not found.");
	case GS_ERR_AUTH: return tr("Authentication was cancelled or denied.");
	case GS_ERR_NOT_AWAITING: return tr("There is no unconfirmed mode change since the last reboot.");
	default: return tr("Unexpected error (code %1).").arg(static_cast<int>(st));
	}
}
