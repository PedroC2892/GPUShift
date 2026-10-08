// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("gpushift"));
	QApplication::setApplicationDisplayName(QStringLiteral("GPUShift"));
	QApplication::setApplicationVersion(QString::fromUtf8(gs_version()));
	QApplication::setDesktopFileName(QStringLiteral("gpushift"));

	QTranslator qtTranslator, appTranslator;
	if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
			      QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		QApplication::installTranslator(&qtTranslator);
	if (appTranslator.load(QLocale(), QStringLiteral("gpushift"), QStringLiteral("_"),
			       QStringLiteral(GPUSHIFT_TRANSLATIONS_DIR)))
		QApplication::installTranslator(&appTranslator);

	MainWindow window;
	window.show();
	return QApplication::exec();
}
