// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gpushift/gpushift.h"

#include <QMainWindow>

class QButtonGroup;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;

// Thin presentation layer over libgpushift: every decision comes from the library.
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;

private:
	void refresh();
	QWidget *buildContent();
	QGroupBox *buildSystemBox();
	QGroupBox *buildGpuBox(size_t index);
	QGroupBox *buildModeBox();
	QWidget *notice(const QString &text, bool warning);
	void applySelected();
	void resetAll();
	void runHelper(const QString &busyText, gs_status (*task)(gs_mode), gs_mode mode);

	static QString modeTitle(gs_mode mode);
	static QString recoveryText();
	static QString modeDescription(gs_mode mode);
	static QString switchMessage(gs_switch sw);
	static QString statusMessage(gs_status st);

	gs_system *m_sys = nullptr;
	QScrollArea *m_scroll = nullptr;
	QButtonGroup *m_modes = nullptr;
	QPushButton *m_apply = nullptr;
	QPushButton *m_reset = nullptr;
	QLabel *m_busy = nullptr;
};
